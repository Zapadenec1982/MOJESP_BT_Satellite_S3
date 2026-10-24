/**
 * @file airplay_volume.h
 * @brief How the sender's volume moves the room's. Pure, so the host tests reach it.
 *
 * The customer asked that the room keep its level when a phone starts (13.10.2026), and then that
 * the phone still reach the whole range with its slider agreeing with the panel. In AirPlay 2 mode
 * the panel cannot move the phone's slider (that would take MRP, which upstream does not
 * implement). What it can do is announce its level: GET /info carries initialVolume
 * (airplay2_report_volume), and a sender that honours it starts its slider there and sends no
 * level until somebody moves it. Then the phone's level is the room's level, and this class is
 * the identity.
 *
 * For a sender that restates its own level anyway, and for the room moved by another hand (the
 * knob, the web) mid-session, the phone's level maps onto the room's through a reference point:
 * piecewise linear from (0, 0) through (phone_ref, room_ref) to (100, 100). So nothing jumps when
 * the reference is set, and both ends of the phone's travel are the room's ends. A phone already
 * at its top when the reference is set cannot go higher, and neither can the room through it.
 *  - a session starts with the reference at the room's level for both: the announced level;
 *  - every level in the HOLD_MS after a sender connects or starts playing only sets the
 *    reference, as does any level while AirPlay is not the source (none is kept for later);
 *  - the room moved by another hand becomes the reference with the phone's last level.
 * Main task only.
 */
#pragma once

#include <cstdint>

namespace airplay_volume {

class Follow {
public:
    static constexpr uint32_t HOLD_MS = 3000;

    /// A sender connected; it was told `room` as initialVolume.
    void session_start(int32_t room) {
        phone_ref_ = room_ref_ = phone_last_ = room;
        hold_ms_ = HOLD_MS;
    }
    /// The sender started playing: it may restate its level, which is not a press either.
    void play_start() { hold_ms_ = HOLD_MS; }
    void tick(uint32_t dt_ms) { hold_ms_ = dt_ms >= hold_ms_ ? 0 : hold_ms_ - dt_ms; }

    /// The sender reported `phone` (0..100). `room` is the room's level now, `mine` whether
    /// AirPlay is the source. Returns the room's new level, or -1 to leave it where it is.
    int32_t on_level(int32_t phone, int32_t room, bool mine) {
        phone_last_ = phone;
        if (hold_ms_ > 0 || !mine) {
            phone_ref_ = phone;
            room_ref_  = room;
            return -1;
        }
        return map(phone);
    }

    /// The room moved by another hand: the phone's next step counts from here.
    void room_moved(int32_t room) {
        room_ref_  = room;
        phone_ref_ = phone_last_;
    }

private:
    int32_t map(int32_t p) const {
        if (p <= phone_ref_) {
            if (phone_ref_ <= 0) return room_ref_;
            return (room_ref_ * p + phone_ref_ / 2) / phone_ref_;
        }
        if (phone_ref_ >= 100) return room_ref_;
        const int32_t span = 100 - phone_ref_;
        return room_ref_ + ((100 - room_ref_) * (p - phone_ref_) + span / 2) / span;
    }

    uint32_t hold_ms_    = 0;
    int32_t  phone_ref_  = 0;
    int32_t  room_ref_   = 0;
    int32_t  phone_last_ = 0;
};

} // namespace airplay_volume
