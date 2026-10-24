#pragma once
#include <cstddef>
#include <cstdint>
#include <cmath>

namespace audio_core {
// Observational stereo statistics. Exact zeros are counted separately per
// channel, across block boundaries; leading silence is not an interior hole.
struct StartupProbe {
    struct Channel {
        float peak = 0, step = 0, previous = 0;
        uint32_t leading = 0, run = 0, longest = 0, holes = 0;
        bool seen = false;
    } ch[2];
    uint32_t frames = 0;
    void observe(const float* pcm, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            for (size_t c = 0; c < 2; ++c) {
                auto& s = ch[c];
                const float v = pcm[2 * i + c];
                const float peak = std::fabs(v), step = std::fabs(v - s.previous);
                if (peak > s.peak) s.peak = peak;
                if (step > s.step) s.step = step;
                s.previous = v;
                if (v == 0.0f) {
                    if (!s.seen) ++s.leading;
                    else {
                        ++s.run;
                        if (s.run > s.longest) s.longest = s.run;
                    }
                } else {
                    if (s.run >= 16) ++s.holes;
                    s.run = 0;
                    s.seen = true;
                }
            }
        }
        frames += static_cast<uint32_t>(count);
    }
};
} // namespace audio_core
