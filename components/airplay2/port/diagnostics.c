/* Opt-in AirPlay negotiation trace. No bodies, keys or authentication values. */
#include "esp_log.h"
#include "rtsp_message.h"
#include <string.h>
#include <strings.h>

static const char *TAG = "airplay_trace";
static void printable(char *out, size_t cap, const uint8_t *in, size_t len) {
  size_t n = len < cap - 1 ? len : cap - 1;
  for (size_t i = 0; i < n; ++i)
    out[i] = in[i] >= 32 && in[i] <= 126 ? (char)in[i] : '?';
  out[n] = 0;
}

void airplay_trace_request(int socket, const uint8_t *raw, size_t len) {
  if (!raw || !len || esp_log_level_get(TAG) < ESP_LOG_INFO) return;
  const uint8_t *end = memchr(raw, '\n', len);
  if (!end) {
    ESP_LOGI(TAG, "RX fd=%d malformed request (%zu bytes, no request line)", socket, len);
    return;
  }
  char line[192];
  size_t n = (size_t)(end - raw);
  if (n && raw[n - 1] == '\r') --n;
  printable(line, sizeof(line), raw, n);
  ESP_LOGI(TAG, "RX fd=%d bytes=%zu %s", socket, len, line);
  size_t offset = (size_t)(end - raw) + 1;
  while (offset < len) {
    const uint8_t *start = raw + offset;
    end = memchr(start, '\n', len - offset);
    if (!end) break;
    n = (size_t)(end - start);
    offset = (size_t)(end - raw) + 1;
    if (n && start[n - 1] == '\r') --n;
    if (!n) break;
    static const char *const allowed[] = {
      "CSeq:", "User-Agent:", "Content-Type:", "Content-Length:"
    };
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); ++i) {
      size_t prefix = strlen(allowed[i]);
      if (n >= prefix && strncasecmp((const char *)start, allowed[i], prefix) == 0) {
        printable(line, sizeof(line), start, n);
        ESP_LOGI(TAG, "RX fd=%d %s", socket, line);
        break;
      }
    }
    if (n >= 16 && strncasecmp((const char *)start, "Apple-Challenge:", 16) == 0)
      ESP_LOGI(TAG, "RX fd=%d Apple-Challenge present", socket);
  }
}

int __real_rtsp_send_response(int, rtsp_conn_t *, int, const char *, int,
                              const char *, const char *, size_t);
int __real_rtsp_send_ok(int, rtsp_conn_t *, int);
int __real_rtsp_send_http_response(int, rtsp_conn_t *, int, const char *,
                                   const char *, const char *, size_t);

int __wrap_rtsp_send_response(int socket, rtsp_conn_t *conn, int status,
                              const char *text, int cseq, const char *headers,
                              const char *body, size_t len) {
  int result = __real_rtsp_send_response(socket, conn, status, text, cseq, headers, body, len);
  ESP_LOGI(TAG, "TX fd=%d RTSP status=%d cseq=%d body=%zu encrypted=%d apple_response=%d result=%d",
           socket, status, cseq, len, conn && conn->encrypted_mode,
           headers && strstr(headers, "Apple-Response:") != NULL, result);
  return result;
}

/* send_ok calls send_response inside its own object, bypassing linker wrapping. */
int __wrap_rtsp_send_ok(int socket, rtsp_conn_t *conn, int cseq) {
  int result = __real_rtsp_send_ok(socket, conn, cseq);
  ESP_LOGI(TAG, "TX fd=%d RTSP status=200 cseq=%d body=0 encrypted=%d result=%d",
           socket, cseq, conn && conn->encrypted_mode, result);
  return result;
}

int __wrap_rtsp_send_http_response(int socket, rtsp_conn_t *conn, int status,
                                   const char *text, const char *type,
                                   const char *body, size_t len) {
  int result = __real_rtsp_send_http_response(socket, conn, status, text, type, body, len);
  ESP_LOGI(TAG, "TX fd=%d HTTP status=%d body=%zu encrypted=%d result=%d",
           socket, status, len, conn && conn->encrypted_mode, result);
  return result;
}
