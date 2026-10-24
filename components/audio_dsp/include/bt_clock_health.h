#pragma once
#include <cstdint>

namespace audio_core {
// A decision belongs to one announced format/session. Priming does not clear it.
// Queue loss is evidence about delivery, not a measurement of the peer's crystal.
class BtClockHealth {
public:
    void reset() { *this = BtClockHealth{}; }
    bool underrun() { return ++underruns_ >= 2; }
    bool observe(uint32_t fill, uint32_t hz, int32_t applied_ppm, uint32_t elapsed_ms) {
        if (!hz || applied_ppm > -95) {
            saturated_ms_ = 0;
            return false;
        }
        if (!saturated_ms_) start_fill_ = fill;
        saturated_ms_ += elapsed_ms;
        if (saturated_ms_ < 8000) return false;
        // At least 20 ms below the 120 ms target, and still losing >=8 ms per
        // observation span at maximum correction. The drift must exceed the
        // 240-frame read sawtooth (5.44 ms at 44.1 kHz), not just its phase.
        const bool draining = fill < hz / 10 &&
                              start_fill_ > fill + hz / 125;
        saturated_ms_ = 0;
        return draining;
    }
private:
    uint32_t underruns_ = 0;
    uint32_t saturated_ms_ = 0;
    uint32_t start_fill_ = 0;
};
} // namespace audio_core
