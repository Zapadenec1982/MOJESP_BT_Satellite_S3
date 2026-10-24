/**
 * @file test_audio_pipe.cpp
 * @brief pcm_to_f32, the one decoder-to-float conversion both producers use.
 *
 * The card player carried a verbatim copy of this function until the copy was deleted; these
 * cases pin the contract that copy relied on — 24-bit sign extension above all — so the
 * shared implementation cannot drift from what the card was decoding.
 */

#include "doctest.h"
#include "audio_pipe.h"
#include <cstdint>

using namespace audio_core;

TEST_CASE("pcm_to_f32: 24-bit little-endian samples keep their sign and their scale") {
    // +8388607, -8388608, +1, -1 as 24-bit LE: two stereo frames
    const uint8_t src[] = {0xFF, 0xFF, 0x7F,  0x00, 0x00, 0x80,
                           0x01, 0x00, 0x00,  0xFF, 0xFF, 0xFF};
    float dst[2 * 2];
    CHECK(pcm_to_f32(src, sizeof(src), 24, 2, dst, 4) == 2);
    CHECK(dst[0] == doctest::Approx(8388607.0f / 8388608.0f));
    CHECK(dst[1] == -1.0f);
    CHECK(dst[2] == doctest::Approx(1.0f / 8388608.0f));
    CHECK(dst[3] == doctest::Approx(-1.0f / 8388608.0f));
}

TEST_CASE("pcm_to_f32: mono is duplicated, 16/8/32 bit scale to full range, odd widths are refused") {
    SUBCASE("mono to both sides, at its own level") {
        const uint8_t src[] = {0x00, 0x40,  0x00, 0xC0};   // +16384, -16384 as 16-bit LE
        float dst[2 * 2];
        CHECK(pcm_to_f32(src, sizeof(src), 16, 1, dst, 2) == 2);
        CHECK(dst[0] == 0.5f);
        CHECK(dst[1] == 0.5f);
        CHECK(dst[2] == -0.5f);
        CHECK(dst[3] == -0.5f);
    }
    SUBCASE("16-bit full scale") {
        const uint8_t src[] = {0xFF, 0x7F,  0x00, 0x80};
        float dst[2];
        CHECK(pcm_to_f32(src, sizeof(src), 16, 2, dst, 1) == 1);
        CHECK(dst[0] == doctest::Approx(32767.0f / 32768.0f));
        CHECK(dst[1] == -1.0f);
    }
    SUBCASE("8-bit is unsigned") {
        const uint8_t src[] = {0x80, 0x00,  0xFF, 0x80};
        float dst[4];
        CHECK(pcm_to_f32(src, sizeof(src), 8, 2, dst, 2) == 2);
        CHECK(dst[0] == 0.0f);
        CHECK(dst[1] == -1.0f);
        CHECK(dst[2] == doctest::Approx(127.0f / 128.0f));
        CHECK(dst[3] == 0.0f);
    }
    SUBCASE("32-bit") {
        const uint8_t src[] = {0x00, 0x00, 0x00, 0x40,  0x00, 0x00, 0x00, 0x80};
        float dst[2];
        CHECK(pcm_to_f32(src, sizeof(src), 32, 2, dst, 1) == 1);
        CHECK(dst[0] == 0.5f);
        CHECK(dst[1] == -1.0f);
    }
    SUBCASE("bounded by whole frames and by max_frames; unknown widths convert nothing") {
        const uint8_t src[7] = {};
        float dst[8];
        CHECK(pcm_to_f32(src, 7, 16, 2, dst, 8) == 1);    // seven bytes hold one stereo frame
        CHECK(pcm_to_f32(src, 7, 16, 1, dst, 2) == 2);    // three mono frames, two allowed
        CHECK(pcm_to_f32(src, 7, 12, 2, dst, 8) == 0);
        CHECK(pcm_to_f32(src, 7, 0, 2, dst, 8) == 0);
    }
}
