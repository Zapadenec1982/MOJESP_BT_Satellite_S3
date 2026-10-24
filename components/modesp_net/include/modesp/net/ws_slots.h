/**
 * @file ws_slots.h
 * @brief Which WebSocket client keeps a slot and which gives it up - pure, for the host suite
 *
 * WsService serves a fixed number of WebSocket clients. One that vanished without closing TCP
 * (a phone that slept, a laptop lid shut) answers no PING, and it used to keep its slot until
 * lwIP gave up retransmitting: long enough for every slot to belong to someone nobody was
 * looking at. So each slot remembers when its client was last heard from - the handshake, or
 * any frame it sent, PONG included - and a client silent past the threshold is closed by the
 * server. A new client that finds every slot taken gets the slot of one already due to close;
 * otherwise it is refused, out loud.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace modesp::net {

/// One WebSocket client, as WsService tracks it.
struct WsSlot {
    int      fd       = -1;     ///< the session's socket; -1 for a free slot
    uint32_t heard_ms = 0;      ///< last heard from: the handshake or any frame received (ms, wraps)
    bool     lost     = false;  ///< missed the snapshot or a delta: only a reconnect resyncs it
};

/// How long the client has been silent. Unsigned subtraction: the 49-day wrap of a 32-bit
/// millisecond clock does not matter.
inline uint32_t ws_silent_ms(const WsSlot& s, uint32_t now_ms) {
    return now_ms - s.heard_ms;
}

/// The server should close this client: it is out of step, or silent for longer than the threshold.
inline bool ws_should_close(const WsSlot& s, uint32_t now_ms, uint32_t stale_after_ms) {
    return s.fd >= 0 && (s.lost || ws_silent_ms(s, now_ms) > stale_after_ms);
}

enum class WsAdmit : uint8_t {
    Tracked,  ///< the fd already holds `slot`: a socket number reused, so a new client in it
    Free,     ///< `slot` was free
    Evict,    ///< every slot was taken; `slot` holds a client ws_should_close() already condemns
    Reject,   ///< every slot holds a client heard within the threshold
};

struct WsAdmission {
    WsAdmit action;
    int     slot;  ///< -1 with Reject
};

/// Where a client that has just completed its handshake goes. A free slot first; with none,
/// the slot of a client due to be closed anyway - one out of step before one merely silent,
/// and among those the longest silent.
inline WsAdmission ws_admit(const WsSlot* slots, size_t n, int fd,
                            uint32_t now_ms, uint32_t stale_after_ms) {
    if (fd < 0) return {WsAdmit::Reject, -1};
    for (size_t i = 0; i < n; i++)
        if (slots[i].fd == fd) return {WsAdmit::Tracked, static_cast<int>(i)};
    for (size_t i = 0; i < n; i++)
        if (slots[i].fd < 0) return {WsAdmit::Free, static_cast<int>(i)};

    int victim = -1;
    for (size_t i = 0; i < n; i++) {
        const WsSlot& s = slots[i];
        if (!ws_should_close(s, now_ms, stale_after_ms)) continue;
        if (victim < 0) { victim = static_cast<int>(i); continue; }
        const WsSlot& v = slots[victim];
        const bool worse = s.lost != v.lost ? s.lost
                                            : ws_silent_ms(s, now_ms) > ws_silent_ms(v, now_ms);
        if (worse) victim = static_cast<int>(i);
    }
    if (victim < 0) return {WsAdmit::Reject, -1};
    return {WsAdmit::Evict, victim};
}

} // namespace modesp::net
