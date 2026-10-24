/**
 * @file ws_service.h
 * @brief WebSocket service for real-time state delta broadcast
 *
 * Registers a /ws endpoint on the HTTP server.
 * Periodically broadcasts only CHANGED state keys (delta) to all clients.
 * New clients receive full state immediately on connect.
 *
 * Max 3 simultaneous WS clients. Every client is PINGed each HEARTBEAT_INTERVAL_MS, and one not
 * heard from for STALE_AFTER_MS is closed by the server. A client arriving to three live ones is
 * closed with CLOSE_BUSY (modesp/net/ws_slots.h has the rules).
 */

#pragma once

#include "modesp/base_module.h"
#include "modesp/net/ws_slots.h"
#include "esp_http_server.h"
#include "freertos/semphr.h"

namespace modesp {

class SharedState;

class WsService : public BaseModule {
public:
    WsService() : BaseModule("ws", ModulePriority::LOW) {}

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;

    void set_state(SharedState* state) { state_ = state; }
    void set_http_server(httpd_handle_t server);  // Реєструє WS handler якщо ще не зареєстрований

    /// Close code for a client refused because every slot holds a live one: 1013 "Try Again
    /// Later" in the IANA WebSocket close-code registry. The web shows "busy" on it.
    static constexpr uint16_t CLOSE_BUSY = 1013;

private:
    httpd_handle_t server_ = nullptr;
    SharedState* state_ = nullptr;
    bool ws_registered_ = false;

    // Connected clients (3 active browsers), each with when it was last heard from
    static constexpr size_t MAX_WS_CLIENTS = 3;
    net::WsSlot slots_[MAX_WS_CLIENTS];

    // BUG-021: mutex для slots_ (доступ з main loop + httpd потоків)
    StaticSemaphore_t clients_mutex_buf_;
    SemaphoreHandle_t clients_mutex_ = nullptr;

    // Delta broadcast timing (delta payloads ~200B → можна частіше)
    uint32_t broadcast_timer_ = 0;
    static constexpr uint32_t BROADCAST_INTERVAL_MS = 1500;

    // Heartbeat: PING every client, close the ones gone silent. A browser answers a PING from
    // its network stack, hidden tab or not, so three unanswered intervals is a client that is
    // gone - asleep, out of range, lid shut - and not one that is merely idle.
    uint32_t heartbeat_timer_ = 0;
    static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 5000;
    static constexpr uint32_t STALE_AFTER_MS = 3 * HEARTBEAT_INTERVAL_MS;

    void register_ws_handler();
    void broadcast_state();          // Delta — тільки змінені ключі
    void send_full_state_to(int fd); // Повний state для нового клієнта
    void send_to_all(const char* data, size_t len);
    bool remove_client(int fd);  // returns true if fd was tracked
    bool has_client(int fd) const;
    void heard(int fd);          // any frame from fd: it is alive
    void mark_lost(int fd);      // fd missed a message: the next heartbeat closes it

    // Returns true if fd is still a valid WebSocket session
    bool is_ws_client(int fd) const;

    static esp_err_t ws_handler(httpd_req_t* req);

    // A client has just completed the WebSocket handshake: ws_post_handshake_cb from IDF
    // v5.5.5 / v6.0.1, ws_handler with HTTP_GET before that.
    static esp_err_t on_handshake(httpd_req_t* req);

    // Runs in the httpd thread, queued by on_update(): closes the clients due to go, PINGs
    // the rest.
    static void heartbeat_cb(void* arg);

    // Session context of an admitted client. httpd frees it, through on_session_close(), when
    // the session closes for any reason (clean CLOSE, TCP RST, timeout, server shutdown).
    struct SessionCtx {
        WsService* self;
        int        fd;
    };
    // Signature matches httpd_free_ctx_fn_t: void(*)(void*)
    static void on_session_close(void* ctx);

    // ── Thread-safe send via httpd_queue_work ───────────────
    // httpd_ws_send_frame_async() sends directly from the calling
    // thread which races with httpd's recv(). We queue work to the
    // httpd thread instead.
    struct AsyncSendCtx {
        httpd_handle_t  server;
        WsService*      self;     // back-pointer for immediate fd removal on failure
        int             fd;
        httpd_ws_type_t type;
        size_t          len;
        char            data[];   // flexible array member
    };
    static void async_send_cb(void* arg);
};

} // namespace modesp
