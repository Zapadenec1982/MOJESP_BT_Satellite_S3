/**
 * @file test_dsp_loudness.cpp
 * @brief HOST tests for loudness compensation and the port lift (components/audio_dsp,
 *        DspChain::set_loudness, set_loud_ref, set_port): the shelves follow ISO 226 within their
 *        fit, are a wire at the top of the volume scale and when off, never lift anything past
 *        what 100 % plays, and glide without ticking; «Звичайна гучність» at 100 is the old rule
 *        bit for bit, adds nothing at the reference and the old rule's lift for the distance under
 *        it; the port lift puts its peak on the port and the subsonic under it, crossfades in, and
 *        counts in the headroom unless the bass limiter can take it away.
 */

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "doctest.h"
#include "dsp_chain.h"

using namespace audio_core;

namespace {

double db(double x) { return 20.0 * std::log10(x); }

// ISO 226:2003, the table of the standard, for f ≤ 12.5 kHz.
const double ISO_F[]  = {20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800,
                         1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500};
const double ISO_AF[] = {0.532, 0.506, 0.480, 0.455, 0.432, 0.409, 0.387, 0.367, 0.349, 0.330, 0.315,
                         0.301, 0.288, 0.276, 0.267, 0.259, 0.253, 0.250, 0.246, 0.244, 0.243, 0.243,
                         0.243, 0.242, 0.242, 0.245, 0.254, 0.271, 0.301};
const double ISO_LU[] = {-31.6, -27.2, -23.0, -19.1, -15.9, -13.0, -10.3, -8.1, -6.2, -4.5, -3.1, -2.0,
                         -1.1, -0.4, 0.0, 0.3, 0.5, 0.0, -2.7, -4.1, -1.0, 1.7, 2.5, 1.2, -2.1, -7.1,
                         -11.2, -10.7, -3.1};
const double ISO_TF[] = {78.5, 68.7, 59.5, 51.1, 44.0, 37.5, 31.5, 26.5, 22.1, 17.9, 14.4, 11.4, 8.6, 6.2,
                         4.4, 3.0, 2.2, 2.4, 3.5, 1.7, -1.3, -4.2, -6.0, -5.4, -1.5, 6.0, 12.6, 13.9, 12.3};

/// SPL at table point i for a loudness of `phon`.
double iso_spl(int i, double phon) {
    const double af = 4.47e-3 * (std::pow(10.0, 0.025 * phon) - 1.15) +
                      std::pow(0.4 * std::pow(10.0, (ISO_TF[i] + ISO_LU[i]) / 10.0 - 9.0), ISO_AF[i]);
    return 10.0 / ISO_AF[i] * std::log10(af) - ISO_LU[i] + 94.0;
}

/// The boost at table point i, re 1 kHz, that keeps the balance heard at 85 phon `a` dB quieter.
double iso_boost(int i, double a) {
    const int k = 17;   // 1 kHz
    return (iso_spl(i, 85.0 - a) - iso_spl(k, 85.0 - a)) - (iso_spl(i, 85.0) - iso_spl(k, 85.0));
}

std::unique_ptr<DspChain> chain(double fs, int vol, float loud) {
    auto c = std::make_unique<DspChain>();
    c->set_sample_rate((float)fs);
    c->set_eq_enabled(false);
    c->set_volume_percent(vol);
    c->set_loudness(loud);
    c->reset();                                // the shelves land on their targets: no glide
    return c;
}

/// RMS of the output for a sine at `hz` (both channels), between `from` and `to` seconds.
double sine_rms(DspChain& c, double fs, double hz, float amp, double from = 0.5, double to = 1.0) {
    float blk[DspChain::BLOCK * 2];
    double acc = 0.0;
    size_t cnt = 0, n = 0;
    while (n < (size_t)(to * fs)) {
        for (size_t i = 0; i < DspChain::BLOCK; i++)
            blk[2 * i] = blk[2 * i + 1] = amp * (float)std::sin(2 * M_PI * hz * (double)(n + i) / fs);
        c.process(blk, DspChain::BLOCK);
        for (size_t i = 0; i < DspChain::BLOCK; i++)
            if ((double)(n + i) / fs >= from) { acc += (double)blk[2 * i] * blk[2 * i]; cnt++; }
        n += DspChain::BLOCK;
    }
    return std::sqrt(acc / (double)cnt);
}

/// Peak of the settled output for a sine at `hz`.
float sine_peak(DspChain& c, double fs, double hz, float amp) {
    float blk[DspChain::BLOCK * 2], pk = 0.0f;
    size_t n = 0;
    while (n < (size_t)(0.6 * fs)) {
        for (size_t i = 0; i < DspChain::BLOCK; i++)
            blk[2 * i] = blk[2 * i + 1] = amp * (float)std::sin(2 * M_PI * hz * (double)(n + i) / fs);
        c.process(blk, DspChain::BLOCK);
        if (n >= (size_t)(0.3 * fs)) for (float v : blk) pk = std::max(pk, std::fabs(v));
        n += DspChain::BLOCK;
    }
    return pk;
}

/// A 4th-order Butterworth high-pass in double, to see what a change throws above the music.
struct Hp4 {
    struct S { double b0, b1, b2, a1, a2, x1 = 0, x2 = 0, y1 = 0, y2 = 0; } s[2];
    Hp4(double fc, double fs) {
        const double Q[2] = {0.54119610, 1.30656296};
        for (int j = 0; j < 2; j++) {
            const double w0 = 2 * M_PI * fc / fs, cw = std::cos(w0), al = std::sin(w0) / (2 * Q[j]), a0 = 1 + al;
            s[j] = {(1 + cw) / 2 / a0, -(1 + cw) / a0, (1 + cw) / 2 / a0, -2 * cw / a0, (1 - al) / a0};
        }
    }
    double operator()(double x) {
        for (S& h : s) {
            const double y = h.b0 * x + h.b1 * h.x1 + h.b2 * h.x2 - h.a1 * h.y1 - h.a2 * h.y2;
            h.x2 = h.x1; h.x1 = x; h.y2 = h.y1; h.y1 = y; x = y;
        }
        return x;
    }
};

uint32_t lcg(uint32_t& s) { s = s * 1664525u + 1013904223u; return s; }

}  // namespace

TEST_CASE("loudness: the shelves follow the ISO 226 contours within their fit") {
    const double hz[] = {40, 63, 100, 200, 500, 2000, 4000, 8000, 12500};
    for (double fs : {48000.0, 192000.0}) {
        for (int vol : {50, 70}) {                       // 30 and 18 dB under the top
            const double a = 60.0 * (1.0 - vol / 100.0);
            auto on = chain(fs, vol, 1.0f), off = chain(fs, vol, 0.0f);
            const double ref = db(sine_rms(*on, fs, 1000, 0.1f) / sine_rms(*off, fs, 1000, 0.1f));
            double worst = 0.0;
            for (double f : hz) {
                const int i = (int)(std::find(std::begin(ISO_F), std::end(ISO_F), f) - std::begin(ISO_F));
                const double got = db(sine_rms(*on, fs, f, 0.1f) / sine_rms(*off, fs, f, 0.1f)) - ref;
                const double want = iso_boost(i, a);
                CAPTURE(fs); CAPTURE(vol); CAPTURE(f); CAPTURE(got); CAPTURE(want);
                CHECK(std::fabs(got - want) <= 1.5);
                worst = std::max(worst, std::fabs(got - want));
            }
            MESSAGE(fs << " Hz, " << a << " dB under the top: at worst " << worst << " dB off the contours");
        }
    }
}

TEST_CASE("loudness: a wire at the top of the scale, and when it is off") {
    auto top_on = chain(48000.0, 100, 1.0f), top_off = chain(48000.0, 100, 0.0f);
    auto mid_off = chain(48000.0, 50, 0.0f);
    auto mid_none = std::make_unique<DspChain>();
    mid_none->set_eq_enabled(false);
    mid_none->set_volume_percent(50);
    mid_none->reset();
    uint32_t seed = 7;
    float a[DspChain::BLOCK * 2], b[DspChain::BLOCK * 2], c[DspChain::BLOCK * 2], d[DspChain::BLOCK * 2];
    size_t differ_top = 0, differ_mid = 0;
    for (int k = 0; k < 100; k++) {
        for (size_t i = 0; i < DspChain::BLOCK * 2; i++)
            a[i] = b[i] = c[i] = d[i] = 0.5f * ((float)(lcg(seed) >> 8) / 8388608.0f - 1.0f);
        top_on->process(a, DspChain::BLOCK);
        top_off->process(b, DspChain::BLOCK);
        mid_off->process(c, DspChain::BLOCK);
        mid_none->process(d, DspChain::BLOCK);
        differ_top += std::memcmp(a, b, sizeof(a)) != 0;
        differ_mid += std::memcmp(c, d, sizeof(c)) != 0;
    }
    CHECK(top_on->loudness_db() == 0.0f);
    CHECK(differ_top == 0);
    CHECK(differ_mid == 0);
}

TEST_CASE("loudness: nothing past what 100 % plays") {
    // A full-scale sine at the two ends of the spectrum, at every volume: the shelves lift less
    // than the volume took away, so the brickwall never has anything to do. And with an EQ boost
    // the top of the scale moves down, and the reference with it.
    for (int with_eq = 0; with_eq < 2; with_eq++) {
        float worst = 0.0f, gmin = 1.0f;
        for (int vol = 1; vol <= 100; vol += (vol < 10 ? 1 : 9)) {
            for (double f : {50.0, 1000.0, 12500.0}) {
                auto c = chain(48000.0, vol, 1.0f);
                if (with_eq) {
                    EqBand p; p.type = BandType::PEAK; p.freq_hz = 1000; p.gain_db = 6; p.q = 1; p.enabled = true;
                    c->set_bands(&p, 1);
                    c->set_eq_enabled(true);
                    c->reset();
                }
                worst = std::max(worst, sine_peak(*c, 48000.0, f, 1.0f));
                gmin = std::min(gmin, c->take_limiter_min());
            }
        }
        MESSAGE(std::string(with_eq ? "with +6 dB at 1 kHz" : "flat") << ": loudest output " << db(worst)
                << " dB re the ceiling, brickwall at least " << gmin);
        CHECK(worst <= CEILING * 1.0001f);
        CHECK(gmin >= 0.9999f);
    }
}

TEST_CASE("loudness: turning the knob glides, without a tick") {
    // A bass note while the volume walks 20 % → 80 % → 20 %, a step a block, which retunes the
    // shelves every block. What lands above 2 kHz is what the changes splattered.
    auto run = [](float loud) {
        auto c = chain(48000.0, 20, loud);
        Hp4 hp(2000.0, 48000.0);
        float blk[DspChain::BLOCK * 2];
        double worst = 0.0;
        size_t n = 0;
        for (int k = 0; k < 400; k++) {
            if (k >= 100 && k < 160) c->set_volume_percent(20 + (k - 99));
            if (k >= 200 && k < 260) c->set_volume_percent(80 - (k - 199));
            for (size_t i = 0; i < DspChain::BLOCK; i++)
                blk[2 * i] = blk[2 * i + 1] = 0.5f * (float)std::sin(2 * M_PI * 60.0 * (double)(n + i) / 48000.0);
            c->process(blk, DspChain::BLOCK);
            for (size_t i = 0; i < DspChain::BLOCK; i++) {
                const double y = hp(blk[2 * i]);
                if (k >= 50) worst = std::max(worst, std::fabs(y));
            }
            n += DspChain::BLOCK;
        }
        return worst;
    };
    const double on = run(1.0f), off = run(0.0f);
    MESSAGE("volume sweeps under a 60 Hz note, above 2 kHz: " << db(on + 1e-30) << " dBFS with loudness, "
            << db(off + 1e-30) << " dBFS without");
    CHECK(db(on + 1e-30) <= -90.0);
}

TEST_CASE("loudness: glides to its target in LOUD_TAU_S and lands on it exactly") {
    auto c = chain(48000.0, 100, 1.0f);
    float blk[DspChain::BLOCK * 2] = {};
    c->set_volume_percent(30);                            // 45.4 dB under the top
    const float want = DspChain::LOUD_LO_K * -DspChain::volume_db(30.0f);
    for (int k = 0; k < 10; k++) c->process(blk, DspChain::BLOCK);   // 50 ms
    const float part = c->loudness_db() / want;
    MESSAGE("after 50 ms: " << 100.0f * part << " % of the way");
    CHECK(part >= 0.55f);
    CHECK(part <= 0.72f);
    for (int k = 0; k < 200; k++) c->process(blk, DspChain::BLOCK);
    CHECK(c->loudness_db() == want);
    // And back to the top: 0 dB exactly, and from then on a wire.
    c->set_volume_percent(100);
    auto ref = chain(48000.0, 100, 0.0f);
    uint32_t seed = 3;
    size_t differ = 0;
    float a[DspChain::BLOCK * 2], b[DspChain::BLOCK * 2];
    for (int k = 0; k < 300; k++) {
        for (size_t i = 0; i < DspChain::BLOCK * 2; i++) a[i] = b[i] = 0.25f * ((float)(lcg(seed) >> 8) / 8388608.0f - 1.0f);
        c->process(a, DspChain::BLOCK);
        ref->process(b, DspChain::BLOCK);
        if (k >= 250) differ += std::memcmp(a, b, sizeof(a)) != 0;
    }
    CHECK(c->loudness_db() == 0.0f);
    CHECK(differ == 0);
}

TEST_CASE("port lift: the peak lands on the port and the subsonic under it") {
    const double fs = 48000.0;
    auto on = chain(fs, 100, 0.0f), off = chain(fs, 100, 0.0f);
    on->set_subsonic(25.0f);                              // waits while the port is set
    on->set_port(50.0f, 4.0f);
    on->reset();
    // What it should be: the analogue prototypes of a BW4 at 40 Hz and the peak.
    EqBand h1, h2, pk;
    h1.type = h2.type = BandType::HIGH_PASS; h1.freq_hz = h2.freq_hz = 40.0f; h1.q = 0.5411961f; h2.q = 1.3065630f;
    pk.type = BandType::PEAK; pk.freq_hz = 50.0f; pk.gain_db = 4.0f; pk.q = DspChain::PORT_Q;
    auto want = [&](double f) { return 10.0 * std::log10(analog_mag2(h1, f) * analog_mag2(h2, f) * analog_mag2(pk, f)); };
    const double ref = db(sine_rms(*on, fs, 1000, 0.1f) / sine_rms(*off, fs, 1000, 0.1f)) - want(1000);
    double worst = 0.0;
    for (double f : {25.0, 30.0, 40.0, 45.0, 50.0, 60.0, 80.0, 120.0, 300.0}) {
        const double got = db(sine_rms(*on, fs, f, 0.1f, 1.0, 2.0) / sine_rms(*off, fs, f, 0.1f, 1.0, 2.0)) - ref;
        CAPTURE(f); CAPTURE(got); CAPTURE(want(f));
        CHECK(got == doctest::Approx(want(f)).scale(1.0).epsilon(0.15));
        worst = std::max(worst, std::fabs(got - want(f)));
    }
    MESSAGE("port 50 Hz +4 dB: at worst " << worst << " dB off the prototype; +" << want(50.0)
            << " dB at 50 Hz, " << want(30.0) << " dB at 30 Hz");
    // Off again: the subsonic goes back to where set_subsonic() put it.
    on->set_port(0.0f, 0.0f);
    on->reset();
    const double at40 = db(sine_rms(*on, fs, 40, 0.1f, 1.0, 2.0) / sine_rms(*off, fs, 40, 0.1f, 1.0, 2.0));
    EqBand l1 = h1, l2 = h2;
    l1.freq_hz = l2.freq_hz = 25.0f;
    CHECK(at40 == doctest::Approx(10.0 * std::log10(analog_mag2(l1, 40.0) * analog_mag2(l2, 40.0))).scale(1.0).epsilon(0.1));
}

TEST_CASE("port lift: switching it on crossfades, not clicks") {
    auto c = chain(48000.0, 100, 0.0f);
    Hp4 hp(500.0, 48000.0);
    float blk[DspChain::BLOCK * 2];
    double worst = 0.0;
    size_t n = 0;
    for (int k = 0; k < 400; k++) {
        if (k == 200) c->set_port(50.0f, 4.0f);
        for (size_t i = 0; i < DspChain::BLOCK; i++)
            blk[2 * i] = blk[2 * i + 1] = 0.25f * (float)std::sin(2 * M_PI * 62.0 * (double)(n + i) / 48000.0);
        c->process(blk, DspChain::BLOCK);
        for (size_t i = 0; i < DspChain::BLOCK; i++) {
            const double y = hp(blk[2 * i]);
            if (k >= 200) worst = std::max(worst, std::fabs(y));
        }
        n += DspChain::BLOCK;
    }
    MESSAGE("port lift switched on under a 62 Hz note: " << db(worst + 1e-30) << " dBFS above 500 Hz");
    CHECK(db(worst + 1e-30) <= -80.0);
}

TEST_CASE("port lift: its boost counts in the headroom, unless the bass limiter takes it") {
    auto c = chain(48000.0, 100, 0.0f);
    c->set_port(50.0f, 4.0f);
    EqBand h1, h2, pk;
    h1.type = h2.type = BandType::HIGH_PASS; h1.freq_hz = h2.freq_hz = 40.0f; h1.q = 0.5411961f; h2.q = 1.3065630f;
    pk.type = BandType::PEAK; pk.freq_hz = 50.0f; pk.gain_db = 4.0f; pk.q = DspChain::PORT_Q;
    double best = 0.0;
    for (int j = 0; j <= 4000; j++) {
        const double f = 20.0 * std::pow(10.0, 3.0 * j / 4000.0);
        best = std::max(best, 10.0 * std::log10(analog_mag2(h1, f) * analog_mag2(h2, f) * analog_mag2(pk, f)));
    }
    MESSAGE("port 50 Hz +4 dB: B = " << c->headroom_db() << " dB, the curve's top " << best << " dB");
    CHECK(c->headroom_db() == doctest::Approx(best).scale(1.0).epsilon(0.05));
    std::vector<float> line(DspChain::BASS_LINE_FRAMES * 2);
    c->set_bass_line(line.data(), DspChain::BASS_LINE_FRAMES);
    c->set_bass_xo(120.0f);
    MESSAGE("with the bass limiter at 120 Hz: B = " << c->headroom_db() << " dB");
    CHECK(c->headroom_db() <= 0.5f);
}

TEST_CASE("usual volume: a reference of 100 is the old rule, bit for bit") {
    // One chain left at its default, one told 40 and then 100 again: the same shelves, the same
    // samples, at every volume.
    for (int vol : {5, 20, 30, 55, 80, 100}) {
        CAPTURE(vol);
        auto a = chain(48000.0, vol, 0.7f), b = chain(48000.0, vol, 0.7f);
        b->set_loud_ref(40.0f);
        b->set_loud_ref(100.0f);
        b->reset();
        CHECK(a->loudness_db() == b->loudness_db());
        float x[DspChain::BLOCK * 2], y[DspChain::BLOCK * 2];
        size_t n = 0;
        bool same = true;
        for (int k = 0; k < 40; k++) {
            for (size_t i = 0; i < DspChain::BLOCK; i++)
                x[2 * i] = x[2 * i + 1] = y[2 * i] = y[2 * i + 1] =
                    0.25f * (float)std::sin(2 * M_PI * 70.0 * (double)(n + i) / 48000.0);
            a->process(x, DspChain::BLOCK);
            b->process(y, DspChain::BLOCK);
            same = same && std::memcmp(x, y, sizeof(x)) == 0;
            n += DspChain::BLOCK;
        }
        CHECK(same);
    }
}

TEST_CASE("usual volume: nothing at the reference or over it, the old lift for the distance under it") {
    auto at = chain(48000.0, 40, 0.7f);
    at->set_loud_ref(40.0f);
    at->reset();
    CHECK(at->loudness_db() == 0.0f);
    auto over = chain(48000.0, 70, 0.7f);
    over->set_loud_ref(40.0f);
    over->reset();
    CHECK(over->loudness_db() == 0.0f);
    // 12 dB under a reference of 90 % is what the old rule adds 12 dB under the top (EQ off: B 0)
    auto under = chain(48000.0, 70, 0.7f);
    under->set_loud_ref(90.0f);
    under->reset();
    auto old = chain(48000.0, 80, 0.7f);
    MESSAGE("12 dB under: " << under->loudness_db() << " dB from 90 %, " << old->loudness_db() << " dB from the top");
    CHECK(old->loudness_db() > 0.0f);
    CHECK(std::fabs(under->loudness_db() - old->loudness_db()) < 1e-3f);
    // and the usual volume moved with the knob: retargeted at once, gliding as the knob does
    auto c = chain(48000.0, 20, 0.7f);
    const float before = c->loudness_db();
    c->set_loud_ref(20.0f);
    c->reset();
    CHECK(before > 0.0f);
    CHECK(c->loudness_db() == 0.0f);
}

TEST_CASE("usual volume: held to 20…100, and NaN is the old rule") {
    DspChain c;
    CHECK(c.loud_ref() == 100.0f);
    c.set_loud_ref(5.0f);
    CHECK(c.loud_ref() == 20.0f);
    c.set_loud_ref(150.0f);
    CHECK(c.loud_ref() == 100.0f);
    c.set_loud_ref(40.0f);
    c.set_loud_ref(std::nanf(""));
    CHECK(c.loud_ref() == 100.0f);
}
