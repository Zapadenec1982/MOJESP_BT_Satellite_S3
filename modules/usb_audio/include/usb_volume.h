/**
 * @file usb_volume.h
 * @brief How the computer's volume slider moves the room's level — the rules AirPlay keeps
 *        (modules/airplay/include/airplay_volume.h), for USB.
 *
 * A copy rather than a shared header: modules do not depend on one another
 * (docs/CONCEPT.md, "SharedState contract: sources"), and these thirty lines are the whole of
 * it. The rules are the same so the panel answers a laptop the way it answers a phone:
 *  - the room keeps its level when the computer starts — a slider left at 100 % is not a request;
 *  - for HOLD_MS after that, or while USB is not the source, a level only sets the reference;
 *  - after it, the slider maps piecewise linearly through (reference → the room's level), so the
 *    top of the slider is the top of the panel and the bottom is silence;
 *  - a level moved by the knob or the web becomes the new reference.
 *
 * The computer reports decibels (−80…0, the feature unit's range in uac_usb.c); position()
 * spreads them evenly over 0…100, which is how the OS draws its slider against this range.
 */

#pragma once

#include <cstdint>

namespace usb_volume {

/// The feature unit's range, 1/256 dB (uac_usb.c: VOL_MIN, VOL_MAX).
constexpr int32_t MIN_256 = -80 * 256;
constexpr int32_t MAX_256 = 0;

/// The slider's position, 0..100, from the host's level in 1/256 dB.
inline int32_t position(int32_t db256) {
    if (db256 <= MIN_256) return 0;
    if (db256 >= MAX_256) return 100;
    return ((db256 - MIN_256) * 100 + (MAX_256 - MIN_256) / 2) / (MAX_256 - MIN_256);
}

class Follow {
public:
    static constexpr uint32_t HOLD_MS = 3000;

    /// The computer started a stream; the room is at `room`.
    void session_start(int32_t room, int32_t host) {
        host_ref_ = host_last_ = host;
        room_ref_ = room;
        hold_ms_  = HOLD_MS;
    }
    void tick(uint32_t dt_ms) { hold_ms_ = dt_ms >= hold_ms_ ? 0 : hold_ms_ - dt_ms; }

    /// The computer's slider is at `host` (0..100). `room` is the room's level now, `mine`
    /// whether USB is the source. Returns the room's new level, or -1 to leave it.
    int32_t on_level(int32_t host, int32_t room, bool mine) {
        host_last_ = host;
        if (hold_ms_ > 0 || !mine) {
            host_ref_ = host;
            room_ref_ = room;
            return -1;
        }
        return map(host);
    }

    /// The room moved by another hand: the slider's next step counts from here.
    void room_moved(int32_t room) {
        room_ref_ = room;
        host_ref_ = host_last_;
    }

private:
    int32_t map(int32_t p) const {
        if (p <= host_ref_) {
            if (host_ref_ <= 0) return room_ref_;
            return (room_ref_ * p + host_ref_ / 2) / host_ref_;
        }
        if (host_ref_ >= 100) return room_ref_;
        const int32_t span = 100 - host_ref_;
        return room_ref_ + ((100 - room_ref_) * (p - host_ref_) + span / 2) / span;
    }

    uint32_t hold_ms_   = 0;
    int32_t  host_ref_  = 0;
    int32_t  room_ref_  = 0;
    int32_t  host_last_ = 0;
};

}  // namespace usb_volume
