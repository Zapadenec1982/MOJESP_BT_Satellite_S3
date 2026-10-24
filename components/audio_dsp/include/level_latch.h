/**
 * @file level_latch.h
 * @brief The volume and «Максимум», from the main loop to the audio task: the last value
 *        written, never a queue (docs/PLAN_SOUND_UX_2026-10-14.md, S4).
 *
 * The volume used to travel as a command, and the command queue drops what does not fit: a
 * volume dropped stayed lost until the next change, while the main loop believed it sent
 * (modules/audio, send()). Here the main loop writes the newest pair and the audio task takes it
 * at its next block — however many were written in between, the last one arrives.
 *
 * One 32-bit word holds both numbers and a count, so the reader never sees one half changed and
 * knows when something moved; the writer can see when its last value was taken. One writer (the
 * main loop), one reader (the audio task).
 *
 * The rules of the scale live here too, pure, for the host tests: «Максимум» compresses the
 * scale (effective) rather than cutting it, and after power-on the volume is no louder than
 * audio.vol_start (boot).
 */

#pragma once

#include <atomic>
#include <cstdint>

namespace audio_core {

class LevelLatch {
public:
    /// Main loop: the volume and the limit, each 0…100 % of the scale (clamped).
    void put(int32_t pct, int32_t limit) {
        const uint32_t seq = (word_.load(std::memory_order_relaxed) >> 16) + 1;
        word_.store((seq & 0xFFFFu) << 16 | (uint32_t)clamp(limit) << 8 | (uint32_t)clamp(pct),
                    std::memory_order_release);
    }

    /// Audio task: the last put, if it is not the one taken last; false otherwise.
    bool take(int32_t& pct, int32_t& limit) {
        const uint32_t w = word_.load(std::memory_order_acquire);
        if (w == last_) return false;
        last_ = w;
        pct = (int32_t)(w & 0xFFu);
        limit = (int32_t)(w >> 8 & 0xFFu);
        taken_.store(w, std::memory_order_release);
        return true;
    }

    /// The last put has been taken: what is heard is what was asked for.
    bool applied() const {
        return taken_.load(std::memory_order_acquire) == word_.load(std::memory_order_acquire);
    }

    /// The level the volume law plays, % of its scale: «Максимум» compresses the scale, so
    /// 100 % on any client is the limit and every step is limit/100 of one.
    static float effective(int32_t pct, int32_t limit) {
        return (float)clamp(pct) * (float)clamp(limit) / 100.0f;
    }

    /// The volume after power-on: the saved one, no louder than `start` unless `start` is 0
    /// ("as it was"). Both in % of the scale; the limit acts through effective().
    static int32_t boot(int32_t saved, int32_t start) {
        const int32_t restored = clamp(saved);
        return clamp(start) == 0 ? restored : (restored < clamp(start) ? restored : clamp(start));
    }

    static int32_t clamp(int32_t v) { return v < 0 ? 0 : v > 100 ? 100 : v; }

private:
    std::atomic<uint32_t> word_{0};      ///< count (16) | limit (8) | volume (8)
    std::atomic<uint32_t> taken_{0};     ///< the word the reader took last
    uint32_t              last_ = 0;     ///< the reader's own copy of it
};

} // namespace audio_core
