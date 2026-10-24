/**
 * @file test_dsp_limiter.cpp
 * @brief HOST tests for the brickwall (components/audio_dsp, DspChain::process): nothing
 *        leaves above the threshold whatever comes in, a steady tone over it is scaled and
 *        not distorted, the look-ahead is (M+1)·D frames, and the gain comes back to exactly 1.
 */

#include <cmath>
#include <algorithm>
#include <cstring>
#include <vector>
#include "doctest.h"
#include "dsp_chain.h"

using namespace audio_core;

namespace {

uint32_t lcg(uint32_t& s) { s = s * 1664525u + 1013904223u; return s; }
float unit(uint32_t& s) { return (float)(int32_t)lcg(s) / 2147483648.0f; }   // −1..1

/// The chunk the brickwall works in at `fs` (dsp_chain.cpp, set_sample_rate).
uint32_t chunk(double fs) { return fs <= 50000 ? 8u : fs <= 100000 ? 16u : 30u; }

/// A chain with nothing in it but the volume at 100 % and the brickwall.
struct Bare {
    DspChain c;
    explicit Bare(double fs) {
        c.set_sample_rate((float)fs);
        c.reset();
        c.set_eq_enabled(false);
        c.set_volume_percent(100);
        float z[DspChain::BLOCK * 2] = {};
        for (int n = 0; n < 200; n++) { memset(z, 0, sizeof(z)); c.process(z, DspChain::BLOCK); }  // ramp up
    }
};

/// Amplitude and phase of a sine at `hz` in x (least squares over whole periods), and the RMS
/// of what is left after it — THD+N, in dB relative to the sine.
double thd_n_db(const std::vector<float>& x, double fs, double hz) {
    const size_t n = x.size();
    double ss = 0, sc = 0, s2 = 0, c2 = 0, cs = 0;
    for (size_t i = 0; i < n; i++) {
        const double w = 2 * M_PI * hz * (double)i / fs, si = std::sin(w), co = std::cos(w);
        ss += x[i] * si; sc += x[i] * co; s2 += si * si; c2 += co * co; cs += si * co;
    }
    const double det = s2 * c2 - cs * cs;
    const double a = (ss * c2 - sc * cs) / det, b = (sc * s2 - ss * cs) / det;
    double res = 0, sig = 0;
    for (size_t i = 0; i < n; i++) {
        const double w = 2 * M_PI * hz * (double)i / fs;
        const double fit = a * std::sin(w) + b * std::cos(w);
        res += (x[i] - fit) * (x[i] - fit);
        sig += fit * fit;
    }
    return 10.0 * std::log10(res / sig + 1e-300);
}

}  // namespace

TEST_CASE("brickwall: nothing leaves above the threshold, whatever comes in") {
    // 20 seconds of everything a limiter fears, at every rate the output runs at, in blocks of
    // awkward lengths so the chunks straddle every kind of join.
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        Bare b(fs);
        uint32_t seed = 99;
        double ph = 0.0;
        float worst = 0.0f;
        size_t done = 0, over = 0;
        const size_t total = (size_t)(20.0 * fs);
        float blk[DspChain::BLOCK * 2];
        while (done < total) {
            const size_t n = 1 + lcg(seed) % DspChain::BLOCK;
            const int scene = (int)((done / (size_t)(0.25 * fs)) % 5);
            for (size_t i = 0; i < n; i++) {
                float l = 0, r = 0;
                switch (scene) {
                case 0:   // bass 12 dB over, with a little left/right difference
                    l = 4.0f * CEILING * (float)std::sin(ph);
                    r = 3.5f * CEILING * (float)std::sin(ph + 0.3);
                    ph += 2 * M_PI * 40.0 / fs;
                    break;
                case 1:   // full-scale noise, three times over
                    l = 3.0f * unit(seed); r = 3.0f * unit(seed);
                    break;
                case 2:   // quiet, then single-sample clicks of 3.5 full scale
                    l = r = 0.05f * unit(seed);
                    if (lcg(seed) % 1000 == 0) l = (lcg(seed) & 1) ? 3.5f : -3.5f;
                    break;
                case 3:   // a kick: 50 Hz bursts that start at full tilt
                    l = r = ((done + i) % (size_t)(0.1 * fs) < (size_t)(0.03 * fs))
                                ? 2.5f * (float)std::sin(2 * M_PI * 50.0 * (double)((done + i) % (size_t)(0.1 * fs)) / fs)
                                : 0.0f;
                    break;
                default:  // a two-tone just over the threshold
                    l = r = 0.7f * (float)std::sin(ph) + 0.4f * (float)std::sin(ph * 37.0);
                    ph += 2 * M_PI * 60.0 / fs;
                    break;
                }
                blk[2 * i] = l;
                blk[2 * i + 1] = r;
            }
            b.c.process(blk, n);
            for (size_t i = 0; i < 2 * n; i++) {
                worst = std::max(worst, std::fabs(blk[i]));
                over += std::fabs(blk[i]) > CEILING * (1.0f + 1e-6f);
            }
            done += n;
        }
        MESSAGE(fs << " Hz: loudest sample out " << 20.0 * std::log10(worst / CEILING) << " dB re the threshold");
        CAPTURE(fs);
        CHECK(over == 0);
        CHECK(b.c.faults() == 0);
    }
}

TEST_CASE("brickwall: a steady tone over the threshold is scaled, not distorted") {
    // The one-pole limiter it replaced measured −41…−44 dB THD+N at 50 Hz, 6–9 dB over: its
    // gain moved inside every cycle. With the hold, the gain on a steady tone stops moving.
    for (double fs : {48000.0, 192000.0})
        for (double hz : {20.0, 50.0, 100.0, 1000.0})
            for (double over_db : {3.0, 9.0}) {
                Bare b(fs);
                const float amp = CEILING * (float)std::pow(10.0, over_db / 20.0);
                double ph = 0.0;
                std::vector<float> tail;
                float blk[DspChain::BLOCK * 2];
                const size_t total = (size_t)(1.5 * fs), keep_from = (size_t)(1.0 * fs);
                for (size_t done = 0; done < total; done += DspChain::BLOCK) {
                    for (size_t i = 0; i < DspChain::BLOCK; i++) {
                        blk[2 * i] = blk[2 * i + 1] = amp * (float)std::sin(ph);
                        ph += 2 * M_PI * hz / fs;
                    }
                    b.c.process(blk, DspChain::BLOCK);
                    if (done >= keep_from) for (size_t i = 0; i < DspChain::BLOCK; i++) tail.push_back(blk[2 * i]);
                }
                const double db = thd_n_db(tail, fs, hz);
                MESSAGE(fs << " Hz, " << hz << " Hz tone " << over_db << " dB over: THD+N " << db << " dB");
                CAPTURE(fs); CAPTURE(hz); CAPTURE(over_db); CAPTURE(db);
                CHECK(db <= -120.0);
                CHECK(*std::max_element(tail.begin(), tail.end()) == doctest::Approx(CEILING).epsilon(2e-3));
            }
}

TEST_CASE("brickwall: the look-ahead is (M+1)·D frames at every rate") {
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        Bare b(fs);
        const uint32_t L = (DspChain::LIM_M + 1) * chunk(fs);
        CHECK(b.c.latency_frames() == L);
        std::vector<float> out;
        float blk[DspChain::BLOCK * 2] = {};
        blk[0] = blk[1] = 0.5f;                                 // one impulse under the threshold
        for (int n = 0; n < 4; n++) {
            b.c.process(blk, DspChain::BLOCK);
            for (size_t i = 0; i < DspChain::BLOCK; i++) out.push_back(blk[2 * i]);
            memset(blk, 0, sizeof(blk));
        }
        CAPTURE(fs);
        for (size_t i = 0; i < out.size(); i++) CHECK(out[i] == (i == L ? 0.5f : 0.0f));
    }
}

TEST_CASE("brickwall: after a loud passage the gain returns to exactly 1, after hold and release") {
    Bare b(48000.0);
    float blk[DspChain::BLOCK * 2];
    double ph = 0.0;
    for (int n = 0; n < 100; n++) {                             // half a second, 9 dB over
        for (size_t i = 0; i < DspChain::BLOCK; i++) { blk[2 * i] = blk[2 * i + 1] = 2.5f * (float)std::sin(ph); ph += 2 * M_PI * 100.0 / 48000.0; }
        b.c.process(blk, DspChain::BLOCK);
    }
    CHECK(b.c.limiter_gain() < 0.45f);
    // Quiet: still held at first, then released.
    int blocks_to_unity = -1;
    for (int n = 0; n < 2000 && blocks_to_unity < 0; n++) {
        for (size_t i = 0; i < DspChain::BLOCK; i++) { blk[2 * i] = blk[2 * i + 1] = 0.1f * (float)std::sin(ph); ph += 2 * M_PI * 100.0 / 48000.0; }
        b.c.process(blk, DspChain::BLOCK);
        if (n == 6) CHECK(b.c.limiter_gain() < 0.45f);          // 30 ms: inside the 40 ms hold
        if (b.c.limiter_gain() == 1.0f) blocks_to_unity = n;
    }
    // From 0.4 at τ = 250 ms until a release step no longer moves the gain in float (about
    // 5e-5 short, where it lands on 1): ln(0.6/5e-5)·τ ≈ 2.35 s, plus the 40 ms hold.
    const double s = blocks_to_unity * (double)DspChain::BLOCK / 48000.0;
    CAPTURE(s);
    CHECK(s > 2.0);
    CHECK(s < 3.0);
    CHECK(b.c.take_limiter_min() == doctest::Approx(CEILING / 2.5f).epsilon(0.01));  // the loud part
    CHECK(b.c.take_limiter_min() == 1.0f);
}

TEST_CASE("brickwall: the window minimum sees a peak that has already let go") {
    Bare b(48000.0);
    float blk[DspChain::BLOCK * 2] = {};
    b.c.take_limiter_min();
    blk[2 * 10] = blk[2 * 10 + 1] = 3.0f;                       // one sample, ~10 dB over
    b.c.process(blk, DspChain::BLOCK);
    for (int n = 0; n < 1000; n++) { memset(blk, 0, sizeof(blk)); b.c.process(blk, DspChain::BLOCK); }
    CHECK(b.c.limiter_gain() == 1.0f);                          // five seconds later: let go
    CHECK(b.c.take_limiter_min() == doctest::Approx(CEILING / 3.0f).epsilon(0.01));
    CHECK(b.c.take_limiter_min() == 1.0f);                      // and the next window starts afresh
}

TEST_CASE("brickwall: blocks of any length never trip the NaN guard") {
    // The panel feeds 240 frames, a multiple of every chunk; tests and other callers need not.
    Bare b(48000.0);
    float blk[DspChain::BLOCK * 2];
    uint32_t seed = 3;
    double ph = 0.0;
    for (int k = 0; k < 4000; k++) {
        const size_t n = 1 + lcg(seed) % 37;
        for (size_t i = 0; i < n; i++) { blk[2 * i] = blk[2 * i + 1] = 1.7f * (float)std::sin(ph); ph += 0.013; }
        b.c.process(blk, n);
    }
    CHECK(b.c.faults() == 0);
}
