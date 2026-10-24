#pragma once

#include "pcm_ring.h"

namespace player_core {

constexpr uint32_t COMMAND_PENDING = uint32_t(1) << 31;

inline bool command_cancelled(const std::atomic<uint32_t>& requested, uint32_t active) {
    const uint32_t state = requested.load(std::memory_order_acquire);
    return !(state & COMMAND_PENDING) && state != active;
}

/// Publish cancellation only for an accepted command. The pending bit and generation
/// share one atomic word, so a producer cannot observe a failed enqueue as cancellation.
template <class Send>
bool queue_pcm_command(std::atomic<uint32_t>& requested, bool interrupts, Send send) {
    const uint32_t previous = requested.load(std::memory_order_acquire);
    const uint32_t next = interrupts ? ((previous + 1) & ~COMMAND_PENDING) : previous;
    requested.store(next | COMMAND_PENDING, std::memory_order_release);
    const bool accepted = send(next);
    requested.store(accepted ? next : previous, std::memory_order_release);
    return accepted;
}

/// A producer may wait for capacity, but never for an owner/session that has gone away.
/// Cancellation is checked even after partial progress, so STOP is not stuck behind PCM.
template <class Cancel, class Wait>
bool put_pcm(audio_core::PcmRing& ring, audio_core::PcmRing::Owner owner, uint32_t generation,
             const float* stereo, size_t frames, Cancel cancel, Wait wait) {
    if (cancel() || ring.owner() != owner || ring.generation() != generation) return false;
    size_t written = 0;
    while (written < frames) {
        if (cancel() || ring.owner() != owner || ring.generation() != generation) return false;
        const size_t n = ring.write(owner, generation, stereo + written * 2, frames - written);
        written += n;
        if (!n) wait();
    }
    return true;
}

} // namespace player_core
