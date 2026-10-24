/**
 * @file test_dsp_fade.cpp
 * @brief HOST tests for EQ changes while playing (components/audio_dsp): every change
 *        crossfades over DspChain::FADE_S with the outgoing design on its own state, an edit
 *        that arrives mid-fade waits its turn, and none of it is audible as a click.
 *
 * The click is measured the way an ear would notice it on a bass note: everything the change
 * puts above the note. The output goes through an 8th-order Butterworth high-pass at 500 Hz
 * (in double), which leaves a 62 Hz tone at −157 dBFS, and the peak of what comes through
 * around the edit is the click.
 */

#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include "doctest.h"
#include "dsp_chain.h"

using namespace audio_core;

namespace {

/// 8th-order Butterworth high-pass, four biquad sections in double (RBJ, Q from the poles).
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

EqBand peak(float f, float g) {
    EqBand b; b.type = BandType::PEAK; b.freq_hz = f; b.gain_db = g; b.q = 1.0f; b.enabled = true;
    return b;
}

/// A chain at `fs` playing a tone through one band, with a high-pass watching for clicks.
struct Rig {
    double fs;
    DspChain chain;
    Hp8 hp;
    double hz, amp, phase = 0.0;
    float blk[DspChain::BLOCK * 2];
    /// At 80 % (−12 dB) with the input 12 dB up: `amp` is the level after the volume, and a
    /// boost stays under the volume ceiling it asks for — the band is all that changes.
    static constexpr double VOL_COMP = 3.98107170553497;   // 10^(12/20)
    Rig(double fs_, double hz_, double amp_, const EqBand& band)
        : fs(fs_), hp(500.0, fs_), hz(hz_), amp(amp_) {
        chain.set_sample_rate((float)fs);
        chain.reset();
        chain.set_bands(&band, 1);
        chain.set_eq_enabled(true);
        chain.set_volume_percent(80);
        // Settle the tone, the band, the volume ramp and the watcher's own start-up.
        run(0.6);
    }
    /// Play `seconds`; return the largest |high-passed output| seen, and the last block's peak.
    double run(double seconds, float* last_peak = nullptr) {
        double worst = 0.0;
        const size_t n = DspChain::BLOCK;
        for (size_t done = 0; done < (size_t)(seconds * fs); done += n) {
            for (size_t i = 0; i < n; i++) {
                const float v = (float)(amp * VOL_COMP * std::sin(phase));
                blk[2 * i] = blk[2 * i + 1] = v;
                phase += 2 * M_PI * hz / fs;
                if (phase > 2 * M_PI) phase -= 2 * M_PI;
            }
            chain.process(blk, n);
            float pk = 0.0f;
            for (size_t i = 0; i < n; i++) {
                worst = std::max(worst, std::fabs(hp.tick(blk[2 * i])));
                pk = std::max(pk, std::fabs(blk[2 * i]));
            }
            if (last_peak) *last_peak = pk;
        }
        return worst;
    }
};

double dbfs(double v) { return 20.0 * std::log10(v + 1e-30); }

}  // namespace

TEST_CASE("EQ fade: no change to a bass note's EQ is audible as a click") {
    for (double fs : {48000.0, 192000.0}) {
        Rig r(fs, 62.0, 0.25, peak(62, 1.8f));
        const double floor = r.run(0.2);
        MESSAGE(fs << " Hz: watcher floor " << dbfs(floor) << " dBFS");

        struct Step { const char* what; double worst; };
        std::vector<Step> steps;
        EqBand b = peak(62, 6.0f);
        r.chain.set_bands(&b, 1);                         steps.push_back({"+1.8 -> +6 dB", r.run(0.1)});
        b.gain_db = -3.0f; r.chain.set_bands(&b, 1);      steps.push_back({"+6 -> -3 dB", r.run(0.1)});
        b.enabled = false; r.chain.set_bands(&b, 1);      steps.push_back({"band off", r.run(0.1)});
        b.enabled = true; r.chain.set_bands(&b, 1);       steps.push_back({"band on", r.run(0.1)});
        r.chain.set_eq_enabled(false);                    steps.push_back({"EQ off", r.run(0.1)});
        r.chain.set_eq_enabled(true);                     steps.push_back({"EQ on", r.run(0.1)});
        // Three edits inside one fade: the second waits, the third replaces it.
        b.gain_db = 4.0f; r.chain.set_bands(&b, 1);       r.run(0.005);
        b.gain_db = 1.0f; r.chain.set_bands(&b, 1);       r.run(0.005);
        b.gain_db = -2.0f; r.chain.set_bands(&b, 1);      steps.push_back({"three edits in one fade", r.run(0.1)});
        r.chain.set_subsonic(40.0f);                      steps.push_back({"subsonic on", r.run(0.1)});
        r.chain.set_subsonic(60.0f);                      steps.push_back({"subsonic 40 -> 60 Hz", r.run(0.1)});
        r.chain.set_subsonic(0.0f);                       steps.push_back({"subsonic off", r.run(0.1)});
        for (const Step& s : steps) {
            MESSAGE(fs << " Hz, " << std::string(s.what) << ": click " << dbfs(s.worst) << " dBFS");
            CAPTURE(fs); const std::string what = s.what; CAPTURE(what);
            CHECK(dbfs(s.worst) <= -80.0);
        }
    }
}

TEST_CASE("EQ fade: the last of several edits is what plays, within two fades") {
    for (double fs : {44100.0, 96000.0}) {
        Rig r(fs, 1000.0, 0.2, peak(1000, 0.0f));
        EqBand b = peak(1000, 6.0f);
        r.chain.set_bands(&b, 1);
        r.run(0.004);
        b.gain_db = 3.0f; r.chain.set_bands(&b, 1);
        r.run(0.004);
        b.gain_db = -3.0f; r.chain.set_bands(&b, 1);      // replaces the +3 that was waiting
        float pk = 0.0f;
        r.run(2 * DspChain::FADE_S + 0.005, &pk);        // the fade in flight, then this one
        CAPTURE(fs);
        CHECK(dbfs(pk / 0.2) == doctest::Approx(-3.0).epsilon(0.01));
    }
}

TEST_CASE("EQ fade: a change takes FADE_S at every rate, not a number of samples") {
    // A 1 kHz tone, a band from 0 to +6 dB at 1 kHz: the level crosses the halfway point of the
    // change (in linear amplitude) at the middle of the fade.
    auto halfway_s = [](double fs) {
        Rig r(fs, 1000.0, 0.2, peak(1000, 0.0f));
        EqBand b = peak(1000, 6.0f);
        r.chain.set_bands(&b, 1);
        const double lo = 0.2, hi = 0.2 * std::pow(10.0, 6.0 / 20.0), mid = (lo + hi) / 2;
        double t = 0.0;
        float pk = 0.0f;
        for (int k = 0; k < 400; k++) {
            r.run(DspChain::BLOCK / fs, &pk);
            t += DspChain::BLOCK / fs;
            if (pk >= mid) return t;
        }
        return -1.0;
    };
    for (double fs : {48000.0, 192000.0}) {
        const double t = halfway_s(fs);
        CAPTURE(fs);
        // Halfway through FADE_S, give or take the block the peak is read over (5 ms at 48k).
        CHECK(t >= DspChain::FADE_S * 0.5 - 0.002);
        CHECK(t <= DspChain::FADE_S * 0.5 + DspChain::BLOCK / fs + 0.002);
    }
}

TEST_CASE("EQ fade: a reset lands any change at once and starts the filters from rest") {
    Rig r(48000.0, 1000.0, 0.2, peak(1000, 0.0f));
    EqBand b = peak(1000, 6.0f);
    r.chain.set_bands(&b, 1);
    r.run(0.002);
    b.gain_db = -6.0f; r.chain.set_bands(&b, 1);          // waits behind the fade
    r.chain.reset();                                       // a source switch: envelope shut
    float silence[DspChain::BLOCK * 2] = {};
    r.chain.process(silence, DspChain::BLOCK);
    for (float v : silence) CHECK(v == 0.0f);              // no state survives to ring
    float pk = 0.0f;
    r.run(0.1, &pk);
    CHECK(dbfs(pk / 0.2) == doctest::Approx(-6.0).epsilon(0.01));
}
