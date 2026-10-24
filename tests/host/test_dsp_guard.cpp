/**
 * @file test_dsp_guard.cpp
 * @brief HOST tests for what keeps the chain safe and clean (components/audio_dsp): the
 *        NaN/Inf guard, subnormal flushing after a pause, the two-pole volume ramp, and a
 *        fresh start at a new sample rate.
 */

#include <cmath>
#include <cstring>
#include <limits>
#include <vector>
#include "doctest.h"
#include "dsp_chain.h"

using namespace audio_core;

namespace {

EqBand band(BandType t, float f, float g, float q) {
    EqBand b; b.type = t; b.freq_hz = f; b.gain_db = g; b.q = q; b.enabled = true;
    return b;
}

std::vector<EqBand> customer_eq() {
    return {
        band(BandType::LOW_SHELF, 40, 2.6f, 0.71f),  band(BandType::PEAK, 62, 1.8f, 1.0f),
        band(BandType::PEAK, 125, 1.7f, 1.0f),       band(BandType::PEAK, 250, 1.5f, 1.0f),
        band(BandType::PEAK, 500, 0.2f, 1.0f),       band(BandType::PEAK, 1000, -1.7f, 1.0f),
        band(BandType::PEAK, 2000, -2.1f, 1.0f),     band(BandType::PEAK, 4000, -2.1f, 1.0f),
        band(BandType::PEAK, 8000, -1.4f, 1.0f),     band(BandType::HIGH_SHELF, 12000, 4.2f, 0.71f),
    };
}

/// 8th-order Butterworth high-pass in double: what a change puts above a bass note.
struct Hp8 {
    struct Sec { double b0, b1, b2, a1, a2, x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
    Sec s[4];
    Hp8(double fc, double fs) {
        const double qs[4] = {0.50979558, 0.60134489, 0.89997622, 2.56291545};
        for (int i = 0; i < 4; i++) {
            const double w0 = 2 * M_PI * fc / fs, cw = std::cos(w0), al = std::sin(w0) / (2 * qs[i]);
            const double a0 = 1 + al;
            s[i] = {(1 + cw) / 2 / a0, -(1 + cw) / a0, (1 + cw) / 2 / a0, -2 * cw / a0, (1 - al) / a0};
        }
    }
    double tick(double x) {
        for (Sec& q : s) {
            const double y = q.b0 * x + q.b1 * q.x1 + q.b2 * q.x2 - q.a1 * q.y1 - q.a2 * q.y2;
            q.x2 = q.x1; q.x1 = x; q.y2 = q.y1; q.y1 = y;
            x = y;
        }
        return x;
    }
};

struct Tone {
    double fs, hz, amp, phase = 0.0;
    void fill(float* st, size_t n) {
        for (size_t i = 0; i < n; i++) {
            const float v = (float)(amp * std::sin(phase));
            st[2 * i] = st[2 * i + 1] = v;
            phase += 2 * M_PI * hz / fs;
            if (phase > 2 * M_PI) phase -= 2 * M_PI;
        }
    }
};

double dbfs(double v) { return 20.0 * std::log10(v + 1e-30); }

}  // namespace

TEST_CASE("guard: a NaN or an infinity becomes one silent block, a reset and a count") {
    for (float poison : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        DspChain c;
        c.reset();
        const auto eq = customer_eq();
        c.set_bands(eq.data(), (int)eq.size());
        c.set_mono_bass(120.0f);
        c.set_volume_percent(100);
        Tone t{48000.0, 220.0, 0.2};
        float blk[DspChain::BLOCK * 2];
        for (int n = 0; n < 40; n++) { t.fill(blk, DspChain::BLOCK); c.process(blk, DspChain::BLOCK); }
        REQUIRE(c.faults() == 0);

        t.fill(blk, DspChain::BLOCK);
        blk[2 * 100] = poison;
        c.process(blk, DspChain::BLOCK);
        CHECK(c.faults() == 1);
        for (float v : blk) CHECK(v == 0.0f);

        // The chain carries on from rest: finite, and back at its level once the ramp is up.
        float pk = 0.0f;
        for (int n = 0; n < 60; n++) {
            t.fill(blk, DspChain::BLOCK);
            c.process(blk, DspChain::BLOCK);
            for (float v : blk) REQUIRE(std::isfinite(v));
            if (n >= 40) for (float v : blk) pk = std::max(pk, std::fabs(v));
        }
        CHECK(c.faults() == 1);
        CHECK(pk > 0.15f);
    }
}

TEST_CASE("guard: after a pause the filters hold exact zeros, never subnormals") {
    DspChain c;
    c.set_sample_rate(96000.0f);
    c.reset();
    const auto eq = customer_eq();
    c.set_bands(eq.data(), (int)eq.size());
    c.set_mono_bass(120.0f);
    c.set_width(1.2f);
    c.set_volume_percent(100);
    Tone t{96000.0, 50.0, 0.5};
    float blk[DspChain::BLOCK * 2];
    for (int n = 0; n < 400; n++) {
        t.fill(blk, DspChain::BLOCK);
        for (size_t i = 0; i < DspChain::BLOCK; i++) blk[2 * i + 1] *= -0.3f;   // some side signal
        c.process(blk, DspChain::BLOCK);
    }
    size_t subnormal = 0, nonzero_last_second = 0;
    const int blocks = (int)(5.0 * 96000.0 / DspChain::BLOCK);
    for (int n = 0; n < blocks; n++) {
        memset(blk, 0, sizeof(blk));
        c.process(blk, DspChain::BLOCK);
        for (float v : blk) {
            if (std::fpclassify(v) == FP_SUBNORMAL) subnormal++;
            if (n >= blocks - 400 && v != 0.0f) nonzero_last_second++;
        }
    }
    CHECK(subnormal == 0);
    CHECK(nonzero_last_second == 0);
    CHECK(c.faults() == 0);
}

TEST_CASE("volume ramp: two poles of VOL_TAU_S, a time at every rate") {
    // The step response of two equal one-poles is 1 − e^(−t/τ)(1 + t/τ): 63.2 % at 2.146 τ.
    auto t63_samples = [](float fs) {
        DspChain c;
        c.set_sample_rate(fs);
        c.reset();
        c.set_volume_percent(100);
        float blk[DspChain::BLOCK * 2];
        for (int n = 0, done = 0; n < 200; n++, done += (int)DspChain::BLOCK) {
            for (size_t i = 0; i < DspChain::BLOCK; i++) blk[2 * i] = blk[2 * i + 1] = 0.5f;
            c.process(blk, DspChain::BLOCK);
            for (size_t i = 0; i < DspChain::BLOCK; i++)    // the limiter's look-ahead comes first
                if (blk[2 * i] >= 0.5f * 0.632f) return done + (int)i + 1 - (int)c.latency_frames();
        }
        return -1;
    };
    for (float fs : {48000.0f, 96000.0f, 192000.0f}) {
        CAPTURE(fs);
        CHECK(t63_samples(fs) == doctest::Approx(2.146 * DspChain::VOL_TAU_S * fs).epsilon(0.02));
    }
}

TEST_CASE("volume ramp: 100 % arrives at exactly unity, and the path is then bit-exact") {
    // Bit-exact against the input as it went in, the limiter's look-ahead earlier.
    DspChain c;
    c.reset();
    c.set_eq_enabled(false);
    c.set_volume_percent(100);
    const uint32_t L = c.latency_frames();
    std::vector<float> in, out;
    float blk[DspChain::BLOCK * 2];
    Tone t{48000.0, 997.0, 0.3};
    for (int n = 0; n < 80; n++) {
        t.fill(blk, DspChain::BLOCK);
        in.insert(in.end(), blk, blk + 2 * DspChain::BLOCK);
        c.process(blk, DspChain::BLOCK);
        out.insert(out.end(), blk, blk + 2 * DspChain::BLOCK);
    }
    const size_t from = 60 * DspChain::BLOCK;
    size_t differ = 0;
    for (size_t i = from; i < out.size() / 2; i++)
        differ += (out[2 * i] != in[2 * (i - L)]) + (out[2 * i + 1] != in[2 * (i - L) + 1]);
    CHECK(differ == 0);
}

TEST_CASE("volume ramp: unmute and volume steps on a bass note do not click") {
    // A 50 Hz note at −6 dBFS; what reaches above 500 Hz when the volume moves.
    for (double fs : {48000.0, 192000.0}) {
        DspChain c;
        c.set_sample_rate((float)fs);
        c.reset();
        c.set_eq_enabled(false);
        c.set_volume_percent(100);
        Hp8 hp(500.0, fs);
        Tone t{fs, 50.0, 0.5};
        float blk[DspChain::BLOCK * 2];
        auto run = [&](double seconds) {
            double worst = 0.0;
            for (size_t done = 0; done < (size_t)(seconds * fs); done += DspChain::BLOCK) {
                t.fill(blk, DspChain::BLOCK);
                c.process(blk, DspChain::BLOCK);
                for (size_t i = 0; i < DspChain::BLOCK; i++) worst = std::max(worst, std::fabs(hp.tick(blk[2 * i])));
            }
            return worst;
        };
        run(0.6);
        const double floor = run(0.2);
        c.set_mute(true);            run(0.2);
        c.set_mute(false);           const double unmute = run(0.2);
        c.set_volume_percent(99);    const double step1 = run(0.2);
        c.set_volume_percent(89);    const double step10 = run(0.2);
        MESSAGE(fs << " Hz: floor " << dbfs(floor) << ", unmute " << dbfs(unmute) << ", 0.6 dB step "
                   << dbfs(step1) << ", 6 dB step " << dbfs(step10) << " dBFS");
        CAPTURE(fs);
        CHECK(dbfs(unmute) <= -80.0);
        CHECK(dbfs(step1) <= -100.0);
        CHECK(dbfs(step10) <= -86.0);
    }
}

TEST_CASE("rate change: the EQ is redesigned for the new rate and starts from rest") {
    DspChain c;
    const EqBand b = band(BandType::PEAK, 1000, -6.0f, 1.0f);
    c.set_bands(&b, 1);
    c.set_volume_percent(100);
    for (float fs : {44100.0f, 192000.0f, 48000.0f}) {
        c.set_sample_rate(fs);
        c.reset();
        float blk[DspChain::BLOCK * 2] = {};
        c.process(blk, DspChain::BLOCK);
        for (float v : blk) CHECK(v == 0.0f);
        Tone t{fs, 1000.0, 0.2};
        float pk = 0.0f;
        for (int n = 0; n < 400; n++) {
            t.fill(blk, DspChain::BLOCK);
            c.process(blk, DspChain::BLOCK);
            if (n >= 300) for (float v : blk) pk = std::max(pk, std::fabs(v));
        }
        CAPTURE(fs);
        CHECK(dbfs(pk / 0.2) == doctest::Approx(-6.0).epsilon(0.01));
    }
}
