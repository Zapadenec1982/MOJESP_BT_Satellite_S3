/* Adapted from rbouteiller/airplay-esp32 at 811d5f8 (Non-Commercial License).
 * Runtime AirPlay mode and checked RTSP task lifecycle; original in upstream/.
 */
#include "rtsp_server.h"
#include "rtsp_lifecycle.h"
#include "airplay2_port.h"
#include "mode_policy.h"

#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "audio_receiver.h"
#include "audio_output.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "rtsp_conn.h"
#include "rtsp_crypto.h"
#include "rtsp_handlers.h"
#include "rtsp_message.h"

#include "ntp_clock.h"
#include "ptp_clock.h"
#include "rtsp_events.h"
#include "dacp_client.h"

static const char *TAG = "rtsp_server";

#define RTSP_PORT           airplay_mode_rtsp_port(airplay2_port_legacy())
#define RTSP_BUFFER_INITIAL 4096
#define RTSP_BUFFER_LARGE   ((size_t)256 * 1024)
// What the receive loops keep free for the next read: one encrypted block
// (RTSP_ENCRYPTED_BLOCK_MAX). So the largest message a buffer can complete is
// RTSP_BUFFER_LARGE less this; a bigger one is skipped (process_rtsp_buffer).
#define RTSP_RECV_HEADROOM  1024
#define RTSP_MESSAGE_MAX    (RTSP_BUFFER_LARGE - RTSP_RECV_HEADROOM)

#define CLIENT_STACK_SIZE 8192
#define SERVER_STACK_SIZE 4096

static int server_socket = -1;
static atomic_bool server_task_active = false;
static atomic_bool server_running = false;
static atomic_bool server_stop_requested = false;
static atomic_int server_start_result = ESP_FAIL;

#define RTSP_START_PENDING (-2147483647 - 1)
#define RTSP_START_WAIT_TICKS 100 /* 5 seconds */
#define RTSP_STOP_WAIT_TICKS 100  /* 5 seconds */
#define RTSP_RELEASE_WAIT_TICKS 60 /* 3 seconds */

// Public start/stop calls are serialized. Socket ownership is separate: stop
// only shuts a socket down; the task owning it alone closes it. The socket lock
// prevents shutdown from seeing a descriptor after its owner has closed it and
// another service has reused that number.
static _Atomic(SemaphoreHandle_t) lifecycle_mutex = NULL;
static SemaphoreHandle_t socket_mutex = NULL;
static SemaphoreHandle_t cleanup_mutex = NULL;
static _Atomic(SemaphoreHandle_t) conn_mutex = NULL;

static SemaphoreHandle_t lifecycle_get_mutex(void) {
  SemaphoreHandle_t lock = atomic_load(&lifecycle_mutex);
  if (!lock) {
    SemaphoreHandle_t candidate = xSemaphoreCreateMutex();
    if (!candidate) return NULL;
    SemaphoreHandle_t expected = NULL;
    if (!atomic_compare_exchange_strong(&lifecycle_mutex, &expected, candidate)) {
      vSemaphoreDelete(candidate);
      lock = expected;
    } else {
      lock = candidate;
    }
  }
  return lock;
}

static void shutdown_owned_socket(int *socket_fd) {
  xSemaphoreTake(socket_mutex, portMAX_DELAY);
  if (*socket_fd >= 0) shutdown(*socket_fd, SHUT_RDWR);
  xSemaphoreGive(socket_mutex);
}

static void close_owned_socket(int *socket_fd) {
  xSemaphoreTake(socket_mutex, portMAX_DELAY);
  if (*socket_fd >= 0) {
    int fd = *socket_fd;
    *socket_fd = -1;
    close(fd);
  }
  xSemaphoreGive(socket_mutex);
}

// RTSP tasks are restartable. Use dynamic TCB allocation so
// reconnect/start-stop paths cannot reuse static task memory before FreeRTOS
// idle finishes deletion.

// Client slot for tracking connections
typedef struct {
  rtsp_conn_t *conn;
  atomic_bool active;
  int socket;
  atomic_bool should_stop;
  atomic_bool is_old; // Marked as old client being killed
  // Its cleanup has stopped the shared receiver/decoder and NTP clock.
  atomic_bool audio_released;
  // Bytes still to drop of a message over RTSP_MESSAGE_MAX, and its CSeq.
  size_t skip;
  int skip_cseq;
} client_slot_t;

static client_slot_t clients[2] = {{.socket = -1}, {.socket = -1}}; // Current and old
static atomic_int current_slot = 0;

// Flag set by the play/pause button to tell the grace period loop
// to send a DACP resume command and keep waiting for reconnect.
static volatile bool s_resume_requested = false;

// A short lock protects the published connection from volume access during
// teardown. It is never held across a grace period or a task join.
void airplay_set_volume(float volume_db) {
  if (!conn_mutex) return;
  xSemaphoreTake(conn_mutex, portMAX_DELAY);
  client_slot_t *c = &clients[atomic_load(&current_slot)];
  if (c->conn && !c->is_old) rtsp_conn_set_volume(c->conn, volume_db);
  xSemaphoreGive(conn_mutex);
}

int32_t airplay_get_volume_q15(void) {
  int32_t volume = 16384; // 50% volume for new clients
  if (!conn_mutex) return volume;
  xSemaphoreTake(conn_mutex, portMAX_DELAY);
  client_slot_t *c = &clients[atomic_load(&current_slot)];
  if (c->conn && !c->is_old) volume = rtsp_conn_get_volume_q15(c->conn);
  xSemaphoreGive(conn_mutex);
  return volume;
}

static void client_free_conn(client_slot_t *slot, rtsp_conn_t *conn) {
  xSemaphoreTake(conn_mutex, portMAX_DELAY);
  slot->conn = NULL;
  rtsp_conn_free(conn); // Includes rtsp_conn_cleanup().
  xSemaphoreGive(conn_mutex);
}

void rtsp_server_request_resume(void) {
  s_resume_requested = true;
}

// Helper to grow buffer. new_size is the usable capacity; one byte more is
// allocated so a message that fills it can still be NUL-terminated.
static uint8_t *grow_buffer(uint8_t *old_buf, size_t old_size, size_t new_size,
                            size_t data_len) {
  (void)old_size;
  uint8_t *new_buf =
      heap_caps_malloc(new_size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!new_buf) {
    new_buf = malloc(new_size + 1);
  }
  if (!new_buf) {
    return NULL;
  }
  if (old_buf && data_len > 0) {
    memcpy(new_buf, old_buf, data_len);
  }
  free(old_buf);
  return new_buf;
}

// Process buffered RTSP requests.
//
// A message over RTSP_MESSAGE_MAX cannot be completed in the buffer, and
// waiting for it closed the connection - the whole session, for one cover a
// sender sent too big. So its bytes are dropped as they arrive, the buffer
// never grows for it, and once the last of them is in the sender gets its 200,
// as if the request had been handled; the request itself goes nowhere.
static void process_rtsp_buffer(client_slot_t *slot, uint8_t *buffer,
                                size_t *buf_len) {
  while (!slot->should_stop) {
    if (slot->skip) {
      const size_t n = *buf_len < slot->skip ? *buf_len : slot->skip;
      if (*buf_len > n) {
        memmove(buffer, buffer + n, *buf_len - n);
      }
      *buf_len -= n;
      slot->skip -= n;
      if (slot->skip) {
        break;
      }
      rtsp_send_ok(slot->socket, slot->conn, slot->skip_cseq);
      continue;
    }
    if (*buf_len == 0) {
      break;
    }

    const uint8_t *header_end = rtsp_find_header_end(buffer, *buf_len);
    if (!header_end) {
      break;
    }

    size_t header_len = (size_t)(header_end - buffer) + 4;
    char *header_str = malloc(header_len + 1);
    if (!header_str) {
      *buf_len = 0;
      break;
    }
    memcpy(header_str, buffer, header_len);
    header_str[header_len] = '\0';

    int content_len = rtsp_parse_content_length(header_str);
    if (content_len < 0) {
      content_len = 0;
    }

    size_t total_len = header_len + (size_t)content_len;
    if (total_len > RTSP_MESSAGE_MAX) {
      const char *space = strchr(header_str, ' ');
      const int method_len = space ? (int)(space - header_str) : 0;
      ESP_LOGW(TAG, "%.*s of %u bytes is over the %u a message may take: skipped",
               method_len < 32 ? method_len : 32, header_str,
               (unsigned)total_len, (unsigned)RTSP_MESSAGE_MAX);
      slot->skip = total_len;
      slot->skip_cseq = rtsp_parse_cseq(header_str);
      free(header_str);
      continue;
    }
    if (*buf_len < total_len) {
      free(header_str);
      break;
    }

    // Null-terminate so strcasestr in parse_raw_header won't read past
    // the message boundary. total_len <= *buf_len <= capacity, and every
    // buffer is allocated capacity + 1 (grow_buffer), so the terminator fits.
    uint8_t saved = buffer[total_len];
    buffer[total_len] = '\0';
    rtsp_dispatch(slot->socket, slot->conn, buffer, total_len);
    buffer[total_len] = saved;
    free(header_str);

    if (*buf_len > total_len) {
      memmove(buffer, buffer + total_len, *buf_len - total_len);
    }
    *buf_len -= total_len;
  }
}

// Client task
static void client_task(void *pvParameters) {
  int slot_idx = (int)(intptr_t)pvParameters;
  client_slot_t *slot = &clients[slot_idx];

  // Create connection state
  rtsp_conn_t *conn = rtsp_conn_create();
  if (!conn) {
    ESP_LOGE(TAG, "Failed to create connection state");
    close_owned_socket(&slot->socket);
    atomic_store_explicit(&slot->active, false, memory_order_release);
    vTaskDelete(NULL);
    return;
  }
  xSemaphoreTake(conn_mutex, portMAX_DELAY);
  slot->conn = conn;
  xSemaphoreGive(conn_mutex);

  // Get client IP address for timing requests
  struct sockaddr_in peer_addr;
  socklen_t peer_len = sizeof(peer_addr);
  if (getpeername(slot->socket, (struct sockaddr *)&peer_addr, &peer_len) ==
      0) {
    conn->client_ip = peer_addr.sin_addr.s_addr;
    ESP_LOGI(TAG, "Client IP: %u.%u.%u.%u",
             (unsigned int)(conn->client_ip & 0xFF),
             (unsigned int)((conn->client_ip >> 8) & 0xFF),
             (unsigned int)((conn->client_ip >> 16) & 0xFF),
             (unsigned int)((conn->client_ip >> 24) & 0xFF));
  }

  // Allocate buffer
  size_t buf_capacity = RTSP_BUFFER_INITIAL;
  uint8_t *buffer = grow_buffer(NULL, 0, buf_capacity, 0);
  if (!buffer) {
    ESP_LOGE(TAG, "Failed to allocate buffer");
    client_free_conn(slot, conn);
    close_owned_socket(&slot->socket);
    atomic_store_explicit(&slot->active, false, memory_order_release);
    vTaskDelete(NULL);
    return;
  }

  size_t buf_len = 0;
  slot->skip = 0;

  // A replaced session stops the one receiver, decoder and NTP clock in its
  // own cleanup, which the server does not wait for. This connection's
  // ANNOUNCE/SETUP reconfigure the same objects: freeing the decoder under the
  // old stream task, or having it freed under ours. Hold requests until the
  // old slot has released them; after the bound, behaviour is as before.
  client_slot_t *other = &clients[1 - slot_idx];
  for (int i = 0; i < RTSP_RELEASE_WAIT_TICKS && server_running &&
                  !slot->should_stop && other->is_old &&
                  !other->audio_released &&
                  atomic_load_explicit(&other->active, memory_order_acquire);
       ++i) {
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  // Socket timeout for stop signal responsiveness
  struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
  setsockopt(slot->socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  // Disable Nagle: RTSP control commands (volume, pause) are tiny and must
  // not wait for coalescing/delayed-ACK, which adds tens to hundreds of ms of
  // latency to every command on this connection.
  int nodelay = 1;
  setsockopt(slot->socket, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

  while (server_running && !slot->should_stop) {
    if (conn->encrypted_mode) {
      // Encrypted mode
      while (server_running && conn->encrypted_mode && !slot->should_stop) {
        if (buf_len >= buf_capacity - RTSP_RECV_HEADROOM) {
          size_t new_cap = buf_capacity < RTSP_BUFFER_LARGE ? RTSP_BUFFER_LARGE
                                                            : buf_capacity * 2;
          if (new_cap > RTSP_BUFFER_LARGE) {
            goto cleanup;
          }
          uint8_t *new_buf =
              grow_buffer(buffer, buf_capacity, new_cap, buf_len);
          if (!new_buf) {
            goto cleanup;
          }
          buffer = new_buf;
          buf_capacity = new_cap;
        }

        int block_len = rtsp_crypto_read_block(
            slot->socket, conn, buffer + buf_len, buf_capacity - buf_len);
        if (block_len <= 0) {
          if (slot->should_stop || (errno != EAGAIN && errno != EWOULDBLOCK)) {
            goto cleanup;
          }
          continue;
        }

        buf_len += (size_t)block_len;
        process_rtsp_buffer(slot, buffer, &buf_len);
      }
      goto cleanup;
    }

    // Plain-text mode
    if (buf_len >= buf_capacity - RTSP_RECV_HEADROOM) {
      size_t new_cap = buf_capacity < RTSP_BUFFER_LARGE ? RTSP_BUFFER_LARGE
                                                        : buf_capacity * 2;
      if (new_cap > RTSP_BUFFER_LARGE) {
        break;
      }
      uint8_t *new_buf = grow_buffer(buffer, buf_capacity, new_cap, buf_len);
      if (!new_buf) {
        break;
      }
      buffer = new_buf;
      buf_capacity = new_cap;
    }

    ssize_t recv_len =
        recv(slot->socket, buffer + buf_len, buf_capacity - buf_len, 0);
    if (recv_len <= 0) {
      if (recv_len < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        continue;
      }
      break;
    }
    buf_len += (size_t)recv_len;
    process_rtsp_buffer(slot, buffer, &buf_len);
  }

cleanup:
  // Both slots may be stopping together. Their teardown touches one global
  // decoder/receiver, clocks and event listener, so it must not overlap.
  xSemaphoreTake(cleanup_mutex, portMAX_DELAY);
  ESP_LOGI(TAG, "Client slot %d disconnected", slot_idx);
  free(buffer);
  close_owned_socket(&slot->socket);

  // Immediate: stop audio and NTP
  audio_receiver_stop();
  audio_output_flush();
  ntp_clock_stop();
  slot->audio_released = true; // A replacing client may now configure them.

  bool has_dacp_remote = conn && conn->protocol_version == 1 &&
                         conn->dacp_id[0] != '\0' &&
                         conn->active_remote[0] != '\0';

  // iOS v1 pause handling needs DACP to distinguish pause from disconnect.
  // Third-party RAOP clients often have no DACP remote; disconnect them
  // immediately instead of delaying slot cleanup with an iOS-only grace path.
  if (has_dacp_remote) {
    if (!slot->should_stop) {
      s_resume_requested = false;
      rtsp_events_emit(RTSP_EVENT_PAUSED, NULL);

      // Phase 1: let mDNS settle (3 s), but exit early on resume or reconnect
      for (int i = 0; i < 6 && !slot->should_stop; i++) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (s_resume_requested) {
          ESP_LOGI(TAG,
                   "Resume requested during Phase 1 — skipping to Phase 2");
          break;
        }
      }

      // Phase 2: wait for reconnect as long as DACP service is advertised.
      // Re-probe every ~5 s. The phone unadvertises the service when the
      // user switches away, so disappearance = genuine disconnect.
      if (!slot->should_stop) {
        bool stay = dacp_probe_service() || s_resume_requested;
        if (s_resume_requested) {
          s_resume_requested = false;
          ESP_LOGI(TAG, "Resume requested via button — waiting for reconnect");
          stay = true;
        }
        if (stay) {
          ESP_LOGI(TAG, "DACP still advertised — waiting for reconnect");
        }
        while (stay && !slot->should_stop) {
          // Wait 5 s between probes (10 × 500 ms), checking flags each tick
          for (int i = 0; i < 10 && !slot->should_stop; i++) {
            vTaskDelay(pdMS_TO_TICKS(500));
            if (s_resume_requested) {
              s_resume_requested = false;
              ESP_LOGI(TAG,
                       "Resume requested via button — extending grace period");
            }
          }
          if (slot->should_stop) {
            break;
          }
          // Re-probe: still advertised?
          stay = dacp_probe_service();
          if (stay) {
            ESP_LOGD(TAG, "DACP still advertised — continuing wait");
          } else {
            ESP_LOGI(TAG, "DACP service gone — genuine disconnect");
          }
        }
      }

      if (slot->is_old) {
        // New client connected during grace period — treat as reconnect
        ESP_LOGI(TAG, "Client reconnected during grace period");
      } else {
        ESP_LOGI(TAG, "Grace period expired — full disconnect");
        dacp_clear_session();
        rtsp_events_emit(RTSP_EVENT_DISCONNECTED, NULL);
      }
    } else {
      // Forcefully stopped (server shutdown or replaced by new client)
      dacp_clear_session();
      rtsp_events_emit(RTSP_EVENT_DISCONNECTED, NULL);
    }
  } else {
    // v2 / unknown — no grace period, clear immediately.
    dacp_clear_session();
    rtsp_events_emit(RTSP_EVENT_DISCONNECTED, NULL);
  }

  // When being replaced by a new client (is_old), skip global state changes —
  // the new session's SETUP already manages PTP and the event port task.
  if (!slot->is_old) {
    ptp_clock_init(); // Restart PTP (stopped during v1 SETUP to free sockets)
    rtsp_stop_event_port_task();
  } else if (rtsp_event_port_listen_socket() >= 0 &&
             rtsp_event_port_listen_socket() == conn->event_socket) {
    // Old task still using our socket — stop it before closing
    rtsp_stop_event_port_task();
  }

  if (conn->event_socket >= 0) {
    close(conn->event_socket);
    conn->event_socket = -1;
  }

  client_free_conn(slot, conn);
  slot->should_stop = false;
  slot->is_old = false;
  xSemaphoreGive(cleanup_mutex);
  // Last access to this slot: a new server may reuse it after this store.
  atomic_store_explicit(&slot->active, false, memory_order_release);

  vTaskDelete(NULL);
}

// Signal old client to stop (non-blocking).
static void signal_client_stop(int index, bool replaced) {
  client_slot_t *slot = &clients[index];
  if (!atomic_load_explicit(&slot->active, memory_order_acquire)) return;
  if (replaced) slot->is_old = true;
  slot->should_stop = true;
  shutdown_owned_socket(&slot->socket);
}

static bool rtsp_clients_stopped(void) {
  for (int i = 0; i < 2; ++i) {
    if (atomic_load_explicit(&clients[i].active, memory_order_acquire)) return false;
  }
  return true;
}

static void server_task(void *pvParameters) {
  (void)pvParameters;
  struct sockaddr_in server_addr, client_addr;
  int fd = -1;
  esp_err_t result = ESP_FAIL;

  // start() already joined every old client. Never reset live slots here.
  if (server_stop_requested) goto cleanup;
  fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) {
    ESP_LOGE(TAG, "Failed to create socket: %d", errno);
    goto cleanup;
  }
  xSemaphoreTake(socket_mutex, portMAX_DELAY);
  server_socket = fd;
  xSemaphoreGive(socket_mutex);
  if (server_stop_requested) goto cleanup;

  int opt = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons(RTSP_PORT);
  if (bind(fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    ESP_LOGE(TAG, "Failed to bind: %d", errno);
    goto cleanup;
  }
  if (server_stop_requested) goto cleanup;
  if (listen(fd, 5) < 0) {
    ESP_LOGE(TAG, "Failed to listen: %d", errno);
    goto cleanup;
  }
  // A bounded select plus nonblocking accept means stop never relies on
  // shutdown() unblocking accept() on a listening socket.
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    ESP_LOGE(TAG, "Failed to make listener nonblocking: %d", errno);
    goto cleanup;
  }
  if (server_stop_requested) goto cleanup;
  server_running = true;
  result = ESP_OK;
  atomic_store_explicit(&server_start_result, result, memory_order_release);
  ESP_LOGI(TAG, "RTSP server listening on port %d", RTSP_PORT);

  while (!server_stop_requested) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);
    struct timeval wait = {.tv_sec = 0, .tv_usec = 200000};
    int ready = select(fd + 1, &read_fds, NULL, NULL, &wait);
    if (server_stop_requested) break;
    if (ready == 0) continue;
    if (ready < 0) {
      if (errno == EINTR) continue;
      ESP_LOGE(TAG, "Listener select failed: %d", errno);
      break;
    }
    socklen_t client_addr_len = sizeof(client_addr);
    int new_socket = accept(fd, (struct sockaddr *)&client_addr, &client_addr_len);
    if (new_socket < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK && !server_stop_requested) {
        ESP_LOGE(TAG, "Failed to accept: %d", errno);
      }
      continue;
    }
    // Stop may race a successful accept. The server owns this unpublished fd.
    if (server_stop_requested) {
      close(new_socket);
      break;
    }
    // Accepted sockets must be blocking for rtsp_crypto_read_block(), even on
    // socket implementations which inherit listener flags.
    int client_flags = fcntl(new_socket, F_GETFL, 0);
    if (client_flags < 0 || fcntl(new_socket, F_SETFL, client_flags & ~O_NONBLOCK) < 0) {
      close(new_socket);
      continue;
    }

    const int new_slot = 1 - current_slot;
    if (clients[new_slot].active) {
      signal_client_stop(new_slot, false);
      int timeout = 30;
      while (clients[new_slot].active && !server_stop_requested && timeout-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
      }
    }
    if (server_stop_requested || clients[new_slot].active) {
      close(new_socket);
      if (server_stop_requested) break;
      ESP_LOGE(TAG, "Slot %d task did not exit in time", new_slot);
      continue;
    }
    signal_client_stop(current_slot, true);
    xSemaphoreTake(socket_mutex, portMAX_DELAY);
    clients[new_slot].socket = new_socket;
    xSemaphoreGive(socket_mutex);
    clients[new_slot].should_stop = false;
    clients[new_slot].is_old = false;
    clients[new_slot].audio_released = false;
    // Set active before create: the task may finish before xTaskCreate returns.
    clients[new_slot].active = true;
    BaseType_t task_ret = xTaskCreate(client_task, "rtsp_client", CLIENT_STACK_SIZE,
                                      (void *)(intptr_t)new_slot, 5, NULL);
    if (task_ret != pdPASS) {
      close_owned_socket(&clients[new_slot].socket);
      clients[new_slot].active = false;
      ESP_LOGE(TAG, "Failed to create client task");
    } else {
      current_slot = new_slot;
    }
  }

cleanup:
  server_running = false;
  close_owned_socket(&server_socket);
  for (int i = 0; i < 2; ++i) signal_client_stop(i, false);
  if (atomic_load(&server_start_result) == RTSP_START_PENDING) {
    atomic_store_explicit(&server_start_result, result, memory_order_release);
  }
  atomic_store_explicit(&server_task_active, false, memory_order_release);
  vTaskDelete(NULL);
}

// Caller holds lifecycle_mutex. Timeout leaves all stop flags asserted. A
// subsequent start is refused while a client/server is still alive.
static esp_err_t rtsp_server_stop_locked(void) {
  server_stop_requested = true;
  server_running = false;
  if (socket_mutex) {
    shutdown_owned_socket(&server_socket);
    for (int i = 0; i < 2; ++i) signal_client_stop(i, false);
  }
  int timeout = RTSP_STOP_WAIT_TICKS;
  while ((server_task_active || !rtsp_clients_stopped()) && timeout-- > 0) {
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  if (server_task_active || !rtsp_clients_stopped()) {
    ESP_LOGE(TAG, "RTSP tasks did not finish stopping");
    return ESP_ERR_TIMEOUT;
  }
  return ESP_OK;
}

esp_err_t rtsp_server_start(void) {
  SemaphoreHandle_t lock = lifecycle_get_mutex();
  if (!lock) return ESP_ERR_NO_MEM;
  if (xSemaphoreTake(lock, pdMS_TO_TICKS(5000)) != pdTRUE) return ESP_ERR_TIMEOUT;
  esp_err_t result = ESP_ERR_INVALID_STATE;
  if (server_task_active || !rtsp_clients_stopped()) goto out;
  if (!socket_mutex) socket_mutex = xSemaphoreCreateMutex();
  if (!cleanup_mutex) cleanup_mutex = xSemaphoreCreateMutex();
  if (!conn_mutex) conn_mutex = xSemaphoreCreateMutex();
  if (!socket_mutex || !cleanup_mutex || !conn_mutex) {
    result = ESP_ERR_NO_MEM;
    goto out;
  }
  server_stop_requested = false;
  server_running = false;
  server_start_result = RTSP_START_PENDING;
  server_task_active = true;
  if (xTaskCreate(server_task, "rtsp_server", SERVER_STACK_SIZE, NULL, 5, NULL) != pdPASS) {
    server_task_active = false;
    server_start_result = ESP_FAIL;
    result = ESP_FAIL;
    goto out;
  }
  int timeout = RTSP_START_WAIT_TICKS;
  while (atomic_load_explicit(&server_start_result, memory_order_acquire) == RTSP_START_PENDING &&
         timeout-- > 0) {
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  result = atomic_load_explicit(&server_start_result, memory_order_acquire);
  if (result == RTSP_START_PENDING) result = ESP_ERR_TIMEOUT;
  if (result != ESP_OK || !server_running) {
    if (result == ESP_OK) result = ESP_FAIL;
    // Cancel even a task that has not run yet; it must observe this before
    // binding or publishing readiness. A timeout never permits a blind restart.
    esp_err_t stopped = rtsp_server_stop_locked();
    if (stopped != ESP_OK) result = stopped;
  }
out:
  xSemaphoreGive(lock);
  return result;
}

esp_err_t rtsp_server_stop_checked(void) {
  SemaphoreHandle_t lock = lifecycle_get_mutex();
  if (!lock) return ESP_ERR_NO_MEM;
  if (xSemaphoreTake(lock, pdMS_TO_TICKS(5000)) != pdTRUE) return ESP_ERR_TIMEOUT;
  esp_err_t result = rtsp_server_stop_locked();
  xSemaphoreGive(lock);
  return result;
}

void rtsp_server_stop(void) {
  (void)rtsp_server_stop_checked();
}
