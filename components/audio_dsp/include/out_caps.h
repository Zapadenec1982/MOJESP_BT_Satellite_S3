/**
 * @file out_caps.h
 * @brief The output schemes a firmware can play — `audio.out_caps`, as bits.
 *
 * docs/PLAN_SOUND_UX_2026-10-14.md, S8: «Кросовер» shows a scheme as working only once the
 * firmware says it plays it; until then it is «ще не підтримується». A scheme is said here after
 * it has been built and checked (the four-channel DAC plan, P4_Four_Channel_DAC_DSP.md §3.6, §8),
 * never ahead of it. This board has one stereo pair, so it says stereo and nothing else.
 */

#pragma once

#include <cstdint>

namespace audio_core {

enum OutCaps : int32_t {
    OUT_STEREO = 1,   ///< 2.0: the two speakers, the whole range
    OUT_SUB    = 2,   ///< 2.1: the speakers and a subwoofer on OUT 2
    OUT_BIAMP  = 4,   ///< bi-amping: tweeters on OUT 1, woofers on OUT 2
    OUT_FOUR   = 8,   ///< 4.0: front on OUT 1, rear on OUT 2
};

/// What this firmware plays.
constexpr int32_t OUT_CAPS_NOW = OUT_STEREO;

}  // namespace audio_core
