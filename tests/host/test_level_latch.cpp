/**
 * @file test_level_latch.cpp
 * @brief HOST tests for the volume's way to the audio task and its scale (components/audio_dsp,
 *        LevelLatch; docs/PLAN_SOUND_UX_2026-10-14.md, S4): the last value always arrives, however
 *        many were written while the reader was away — a full queue used to drop one for good; the
 *        pair is never read half-changed; 100 % on any client is «Найгучніше»; after power-on the
 *        volume is no louder than audio.vol_start; the loudness follows the level that plays.
 */

#include <atomic>
#include <cmath>
#include <memory>
#include <thread>
#include "doctest.h"
#include "dsp_chain.h"
#include "level_latch.h"

using namespace audio_core;

namespace {

/// RMS of the output for a sine at `hz` over the second half of a second.
double sine_rms(DspChain& c, double fs, double hz) {
    float blk[DspChain::BLOCK * 2];
    double acc = 0.0;
    size_t cnt = 0, n = 0;
    while (n < (size_t)fs) {
        for (size_t i = 0; i < DspChain::BLOCK; i++)
            blk[2 * i] = blk[2 * i + 1] = 0.25f * (float)std::sin(2 * M_PI * hz * (double)(n + i) / fs);
        c.process(blk, DspChain::BLOCK);
        for (size_t i = 0; i < DspChain::BLOCK; i++)
            if (n + i >= (size_t)(fs / 2)) { acc += (double)blk[2 * i] * blk[2 * i]; cnt++; }
        n += DspChain::BLOCK;
    }
    return std::sqrt(acc / (double)cnt);
}

std::unique_ptr<DspChain> chain_at(float level, float loudness = 0.0f) {
    auto c = std::make_unique<DspChain>();
    c->set_sample_rate(48000.0f);
    c->set_eq_enabled(false);
    c->set_loudness(loudness);
    c->set_volume(level);
    c->reset();
    return c;
}

}  // namespace

TEST_CASE("latch: the last value arrives however many were written while the reader was away") {
    LevelLatch l;
    int32_t pct = -1, lim = -1;
    CHECK_FALSE(l.take(pct, lim));                     // nothing written yet
    // A hundred changes with the audio task busy: the command queue held sixteen and dropped the
    // rest, the newest among them. Here the newest is what the reader finds.
    for (int i = 1; i <= 100; i++) l.put(i % 101, 40 + i % 61);
    CHECK_FALSE(l.applied());
    REQUIRE(l.take(pct, lim));
    CHECK(pct == 100);
    CHECK(lim == 40 + 100 % 61);
    CHECK(l.applied());
    CHECK_FALSE(l.take(pct, lim));                     // taken once
    l.put(30, 85);
    CHECK_FALSE(l.applied());
    REQUIRE(l.take(pct, lim));
    CHECK(pct == 30);
    CHECK(lim == 85);
    l.put(30, 85);                                     // the same pair again still counts as new
    CHECK(l.take(pct, lim));
    l.put(-5, 400);                                    // held to 0…100
    REQUIRE(l.take(pct, lim));
    CHECK(pct == 0);
    CHECK(lim == 100);
}

TEST_CASE("latch: the pair is never read half-changed") {
    LevelLatch l;
    std::atomic<bool> done{false};
    std::atomic<int> bad{0}, seen{0};
    std::thread reader([&] {
        int32_t pct, lim;
        while (!done.load()) {
            if (l.take(pct, lim)) {
                seen++;
                if (lim != 40 + pct % 61) bad++;       // every pair written keeps this
            }
        }
    });
    for (int i = 0; i < 200000; i++) {
        const int32_t pct = i % 101;
        l.put(pct, 40 + pct % 61);
    }
    done = true;
    reader.join();
    CHECK(bad.load() == 0);
    CHECK(seen.load() > 0);
}

TEST_CASE("scale: «Найгучніше» compresses it - 100 % on any client is the limit") {
    CHECK(LevelLatch::effective(100, 85) == 85.0f);
    CHECK(LevelLatch::effective(50, 85) == 42.5f);
    CHECK(LevelLatch::effective(0, 85) == 0.0f);
    CHECK(LevelLatch::effective(100, 100) == 100.0f);
    // what 100 % plays at an 85 % limit is what 85 % played without one, to the sample
    auto a = chain_at(LevelLatch::effective(100, 85)), b = chain_at(85.0f);
    CHECK(sine_rms(*a, 48000.0, 1000.0) == sine_rms(*b, 48000.0, 1000.0));
    // and a step is limit/100 of the law's 0.6 dB: 0.51 dB at 85 % (over the law's knee)
    auto s70 = chain_at(LevelLatch::effective(70, 85)), s71 = chain_at(LevelLatch::effective(71, 85));
    const double step = 20.0 * std::log10(sine_rms(*s71, 48000.0, 1000.0) / sine_rms(*s70, 48000.0, 1000.0));
    CHECK(std::fabs(step - 0.51) < 0.005);
    // the integer volume is the law as it was, bit for bit
    auto i40 = std::make_unique<DspChain>(), f40 = std::make_unique<DspChain>();
    i40->set_volume_percent(40);
    f40->set_volume(40.0f);
    i40->reset();
    f40->reset();
    CHECK(sine_rms(*i40, 48000.0, 440.0) == sine_rms(*f40, 48000.0, 440.0));
}

TEST_CASE("scale: the loudness follows the level that plays") {
    auto a = chain_at(LevelLatch::effective(100, 70), 1.0f), b = chain_at(70.0f, 1.0f);
    CHECK(a->loudness_db() > 0.0f);
    CHECK(a->loudness_db() == b->loudness_db());
}

TEST_CASE("after power-on: no louder than audio.vol_start, unless it is 0 (as it was)") {
    CHECK(LevelLatch::boot(80, 0) == 80);
    CHECK(LevelLatch::boot(80, 30) == 30);
    CHECK(LevelLatch::boot(20, 30) == 20);
    CHECK(LevelLatch::boot(-5, 30) == 0);
    CHECK(LevelLatch::boot(150, 0) == 100);
    CHECK(LevelLatch::boot(150, 40) == 40);
    CHECK(LevelLatch::boot(0, 30) == 0);               // silence stays silence
}
