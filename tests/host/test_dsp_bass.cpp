/**
 * @file test_dsp_bass.cpp
 * @brief HOST tests for the bass-first limiter (components/audio_dsp, DspChain::set_bass_xo):
 *        the LR4 split is a flat all-pass while there is nothing to do, loud bass is turned
 *        down without moving the mids, the power cap holds, and "off" is not there at all.
 */

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include "doctest.h"
#include "dsp_chain.h"

using namespace audio_core;

namespace {

struct Rig {
    double fs;
    DspChain c;
    std::vector<float> line;
    Rig(double fs_, float xo) : fs(fs_), line(DspChain::BASS_LINE_FRAMES * 2) {
        c.set_sample_rate((float)fs);
        c.set_bass_line(line.data(), DspChain::BASS_LINE_FRAMES);
        c.reset();
        c.set_eq_enabled(false);
        c.set_volume_percent(100);
        c.set_bass_xo(xo);
    }
    /// Play `seconds` of `gen(t)` → (l, r); keep the output from `keep_from` seconds on.
    template <class G>
    std::vector<float> play(double seconds, double keep_from, G gen, float* worst = nullptr) {
        std::vector<float> out;
        float blk[DspChain::BLOCK * 2];
        size_t n = 0;
        while (n < (size_t)(seconds * fs)) {
            for (size_t i = 0; i < DspChain::BLOCK; i++) {
                const double t = (double)(n + i) / fs;
                float l, r;
                gen(t, l, r);
                blk[2 * i] = l;
                blk[2 * i + 1] = r;
            }
            c.process(blk, DspChain::BLOCK);
            for (size_t i = 0; i < DspChain::BLOCK; i++) {
                if (worst) *worst = std::max(*worst, std::max(std::fabs(blk[2 * i]), std::fabs(blk[2 * i + 1])));
                if ((double)(n + i) / fs >= keep_from) out.push_back(blk[2 * i]);
            }
            n += DspChain::BLOCK;
        }
        return out;
    }
};

/// Amplitude of the sine at `hz` in x (least squares).
double amp_at(const float* x, size_t n, double fs, double hz) {
    double ss = 0, sc = 0, s2 = 0, c2 = 0, cs = 0;
    for (size_t i = 0; i < n; i++) {
        const double w = 2 * M_PI * hz * (double)i / fs, si = std::sin(w), co = std::cos(w);
        ss += x[i] * si; sc += x[i] * co; s2 += si * si; c2 += co * co; cs += si * co;
    }
    const double det = s2 * c2 - cs * cs;
    const double a = (ss * c2 - sc * cs) / det, b = (sc * s2 - ss * cs) / det;
    return std::sqrt(a * a + b * b);
}

double rms(const std::vector<float>& x) {
    double acc = 0;
    for (float v : x) acc += (double)v * v;
    return std::sqrt(acc / (double)x.size());
}

double db(double x) { return 20.0 * std::log10(x); }

}  // namespace

TEST_CASE("bass limiter: with nothing to limit, the LR4 split is a flat all-pass") {
    for (double fs : {48000.0, 192000.0}) {
        Rig r(fs, 120.0f);
        for (double hz : {30.0, 60.0, 120.0, 240.0, 1000.0, 8000.0}) {
            const auto out = r.play(0.6, 0.3, [&](double t, float& l, float& rr) {
                l = rr = 0.25f * (float)std::sin(2 * M_PI * hz * t);
            });
            CAPTURE(fs); CAPTURE(hz);
            CHECK(db(rms(out) / (0.25 / std::sqrt(2.0))) == doctest::Approx(0.0).scale(1.0).epsilon(0.01));
        }
        CHECK(r.c.take_bass_min() == 1.0f);
    }
}

TEST_CASE("bass limiter: a bass note over the ceiling is turned down, the mids are not") {
    // 50 Hz at +6 dB over the ceiling under a 1 kHz marker at −20 dB. A full-band limiter has to
    // take the marker down with the bass, about 6 dB, and moves it with every beat; this one
    // takes the bass down and leaves the marker where it was.
    auto run = [](double fs, float xo, double& marker_db, double& swing_db, float& worst) {
        Rig r(fs, xo);
        worst = 0.0f;
        const auto out = r.play(2.0, 1.0, [](double t, float& l, float& rr) {
            l = rr = 2.0f * (float)std::sin(2 * M_PI * 50.0 * t) + 0.1f * (float)std::sin(2 * M_PI * 1000.0 * t);
        }, &worst);
        marker_db = db(amp_at(out.data(), out.size(), fs, 1000.0) / 0.1);
        // The marker's level in 20 ms windows: how much it is pumped.
        const size_t w = (size_t)(0.02 * fs);
        double lo = 1e9, hi = -1e9;
        for (size_t s = 0; s + w <= out.size(); s += w) {
            const double a = db(amp_at(out.data() + s, w, fs, 1000.0));
            lo = std::min(lo, a);
            hi = std::max(hi, a);
        }
        swing_db = hi - lo;
    };
    for (double fs : {48000.0, 192000.0}) {
        double m, sw, m0, sw0;
        float worst, worst0;
        run(fs, 120.0f, m, sw, worst);
        run(fs, 0.0f, m0, sw0, worst0);
        MESSAGE(fs << " Hz: marker " << m << " dB, swing " << sw << " dB with the bass limiter; "
                   << m0 << " dB, swing " << sw0 << " dB with the brickwall alone");
        CAPTURE(fs);
        CHECK(worst <= CEILING * (1.0f + 1e-6f));
        CHECK(m >= -0.2);
        CHECK(sw <= 0.7);
        CHECK(m0 < -3.0);                                    // what it saves the mids from
    }
}

TEST_CASE("bass limiter: the power cap holds sustained bass under it") {
    Rig r(48000.0, 120.0f);
    r.c.set_rms_cap_db(-6.0f);                                // a quarter of a full-scale sine's power
    std::vector<float> outl, outr;
    float blk[DspChain::BLOCK * 2];
    double acc = 0;
    size_t cnt = 0;
    for (size_t n = 0; n < (size_t)(3.0 * 48000.0); n += DspChain::BLOCK) {
        for (size_t i = 0; i < DspChain::BLOCK; i++) {
            const double t = (double)(n + i) / 48000.0;
            blk[2 * i] = blk[2 * i + 1] = 0.9f * (float)std::sin(2 * M_PI * 60.0 * t);
        }
        r.c.process(blk, DspChain::BLOCK);
        if (n >= (size_t)(1.0 * 48000.0))
            for (size_t i = 0; i < DspChain::BLOCK; i++) { acc += (double)blk[2 * i] * blk[2 * i] + (double)blk[2 * i + 1] * blk[2 * i + 1]; cnt++; }
    }
    const double power_db = 10.0 * std::log10(acc / (double)cnt);    // re a full-scale sine on both channels
    MESSAGE("60 Hz at −0.9 dB under a −6 dB power cap: " << power_db << " dB");
    CHECK(power_db <= -6.0 + 0.2);
    CHECK(power_db >= -6.0 - 1.0);                           // and not much more than asked
}

TEST_CASE("bass limiter: off is not there at all, and its look-ahead is part of the latency") {
    Rig off(48000.0, 0.0f);
    CHECK(off.c.latency_frames() == (DspChain::LIM_M + 1) * 8u);
    const uint32_t L = off.c.latency_frames();
    std::vector<float> in, out;
    float blk[DspChain::BLOCK * 2];
    for (int k = 0; k < 60; k++) {
        for (size_t i = 0; i < DspChain::BLOCK; i++) blk[2 * i] = blk[2 * i + 1] = 0.3f * std::sin(0.01f * (float)(k * 240 + (int)i));
        in.insert(in.end(), blk, blk + 2 * DspChain::BLOCK);
        off.c.process(blk, DspChain::BLOCK);
        out.insert(out.end(), blk, blk + 2 * DspChain::BLOCK);
    }
    size_t differ = 0;
    for (size_t i = 40 * DspChain::BLOCK; i < out.size() / 2; i++) differ += out[2 * i] != in[2 * (i - L)];
    CHECK(differ == 0);
    Rig on(48000.0, 120.0f);
    const uint32_t mb = (uint32_t)std::lround(DspChain::BASS_LOOKAHEAD_S * 48000.0 / 8.0);
    CHECK(on.c.latency_frames() == (DspChain::LIM_M + 1) * 8u + (mb + 1) * 8u);
}

TEST_CASE("bass limiter: moving the crossover goes through a mute, not a click") {
    Rig r(48000.0, 120.0f);
    // A 62 Hz note at −12 dB; what reaches above 500 Hz while the split moves.
    struct Hp { double b0, b1, b2, a1, a2, x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
    std::vector<Hp> hp;
    for (double q : {0.50979558, 0.60134489, 0.89997622, 2.56291545}) {
        const double w0 = 2 * M_PI * 500.0 / 48000.0, cw = std::cos(w0), al = std::sin(w0) / (2 * q), a0 = 1 + al;
        hp.push_back({(1 + cw) / 2 / a0, -(1 + cw) / a0, (1 + cw) / 2 / a0, -2 * cw / a0, (1 - al) / a0});
    }
    double worst = 0.0;
    bool changed = false;
    size_t n = 0;
    float blk[DspChain::BLOCK * 2];
    for (int k = 0; k < 400; k++) {
        if (k == 200) { r.c.set_bass_xo(80.0f); changed = true; }
        for (size_t i = 0; i < DspChain::BLOCK; i++) blk[2 * i] = blk[2 * i + 1] = 0.25f * (float)std::sin(2 * M_PI * 62.0 * (double)(n + i) / 48000.0);
        r.c.process(blk, DspChain::BLOCK);
        for (size_t i = 0; i < DspChain::BLOCK; i++) {
            double x = blk[2 * i];
            for (Hp& h : hp) { const double y = h.b0 * x + h.b1 * h.x1 + h.b2 * h.x2 - h.a1 * h.y1 - h.a2 * h.y2; h.x2 = h.x1; h.x1 = x; h.y2 = h.y1; h.y1 = y; x = y; }
            if (changed) worst = std::max(worst, std::fabs(x));
        }
        n += DspChain::BLOCK;
    }
    MESSAGE("crossover 120 -> 80 Hz under a 62 Hz note: " << db(worst + 1e-30) << " dBFS above 500 Hz");
    CHECK(db(worst + 1e-30) <= -80.0);
    CHECK(r.c.latency_frames() > (DspChain::LIM_M + 1) * 8u);   // still on, at the new corner
}

TEST_CASE("bass limiter: a treble burst over a quiet bass does not pump the bass") {
    // 55 Hz at 0.30, never over the ceiling, and every half second 10 ms of 1 kHz at 0.90: the
    // peak is the treble's. The bass used to drop to its −12 dB floor on every burst.
    for (double fs : {48000.0, 192000.0}) {
        Rig r(fs, 120.0f);
        const auto out = r.play(3.0, 1.0, [](double t, float& l, float& rr) {
            const double ph = std::fmod(t, 0.5);
            const float burst = ph < 0.01 ? 0.9f * (float)std::sin(2 * M_PI * 1000.0 * t) : 0.0f;
            l = rr = 0.30f * (float)std::sin(2 * M_PI * 55.0 * t) + burst;
        });
        // The bass level in 20 ms windows: the deepest dip.
        const size_t w = (size_t)(0.02 * fs);
        double lo = 1e9;
        for (size_t s0 = 0; s0 + w <= out.size(); s0 += w)
            lo = std::min(lo, db(amp_at(out.data() + s0, w, fs, 55.0) / 0.30));
        MESSAGE(fs << " Hz: deepest bass dip under the bursts " << lo << " dB");
        CAPTURE(fs);
        CHECK(lo >= -3.0);                                   // the brickwall's own dip, no more
    }
}
