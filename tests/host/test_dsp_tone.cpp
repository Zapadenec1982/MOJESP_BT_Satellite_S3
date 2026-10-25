/**
 * @file test_dsp_tone.cpp
 * @brief HOST tests for Бас, Високі and the balance (components/audio_dsp, DspChain::set_tone,
 *        set_balance_db): at 0 they are wires, bit-exact;
 *        the shelves give their curve whether the EQ is on or off, on top of the EQ's own ten
 *        bands; they count in B; the balance only ever turns the other channel down.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include "doctest.h"
#include "dsp_chain.h"

using namespace audio_core;

namespace {

uint32_t lcg(uint32_t& s) { s = s * 1664525u + 1013904223u; return s; }

double db(double x) { return 20.0 * std::log10(x); }

std::unique_ptr<DspChain> chain(double fs, bool eq_on) {
    auto c = std::make_unique<DspChain>();
    c->set_sample_rate((float)fs);
    c->set_eq_enabled(eq_on);
    c->set_volume_percent(70);                 // −18 dB: under any cap the headroom sets here
    c->set_loudness(0.0f);
    c->reset();
    return c;
}

/// RMS of each channel for a sine at `hz` (both channels), over the last half of a second.
void sine_rms(DspChain& c, double fs, double hz, double& l, double& r) {
    float blk[DspChain::BLOCK * 2];
    double al = 0.0, ar = 0.0;
    size_t cnt = 0, n = 0;
    while (n < (size_t)fs) {
        for (size_t i = 0; i < DspChain::BLOCK; i++)
            blk[2 * i] = blk[2 * i + 1] = 0.25f * (float)std::sin(2 * M_PI * hz * (double)(n + i) / fs);
        c.process(blk, DspChain::BLOCK);
        for (size_t i = 0; i < DspChain::BLOCK; i++) {
            if (n + i < (size_t)(fs / 2)) continue;
            al += (double)blk[2 * i] * blk[2 * i];
            ar += (double)blk[2 * i + 1] * blk[2 * i + 1];
            cnt++;
        }
        n += DspChain::BLOCK;
    }
    l = std::sqrt(al / (double)cnt);
    r = std::sqrt(ar / (double)cnt);
}

double sine_rms(DspChain& c, double fs, double hz) {
    double l, r;
    sine_rms(c, fs, hz, l, r);
    return l;
}

/// Бас and Високі as EqBands, for the expected curve.
std::vector<EqBand> shelves(float bass, float treble) {
    std::vector<EqBand> v(2);
    v[0].type = BandType::LOW_SHELF;
    v[0].freq_hz = DspChain::TONE_LO_HZ;
    v[1].type = BandType::HIGH_SHELF;
    v[1].freq_hz = DspChain::TONE_HI_HZ;
    v[0].gain_db = bass;
    v[1].gain_db = treble;
    for (auto& b : v) { b.q = DspChain::TONE_Q; b.enabled = b.gain_db != 0.0f; }
    return v;
}

/// What the chain's own designs of `eq` give at `hz` (matched at `fs`, as it runs them).
double designed_db(const std::vector<EqBand>& eq, double fs, double hz) {
    double s = 0.0;
    for (const auto& b : eq) { SvfDesign x; if (b.enabled && design_svf_params(b, fs, x)) s += svf_magnitude_db(x, fs, hz); }
    return s;
}

/// The true maximum of the summed curve on [20 Hz, min(20 kHz, 0.49 fs)], by brute force.
double dense_max_db(const std::vector<EqBand>& eq, double fs) {
    std::vector<SvfDesign> d;
    for (const auto& b : eq) { SvfDesign x; if (b.enabled && design_svf_params(b, fs, x)) d.push_back(x); }
    const double lo = 20.0, hi = std::min(20000.0, 0.49 * fs);
    double best = -1e9;
    for (int j = 0; j <= 20000; j++) {
        const double f = lo * std::pow(hi / lo, j / 20000.0);
        double s = 0.0;
        for (const auto& x : d) s += svf_magnitude_db(x, fs, f);
        best = std::max(best, s);
    }
    return best;
}

/// Ten peaks of somebody's own: what the shelves must leave alone.
std::vector<EqBand> own_bands() {
    std::vector<EqBand> v(MAX_BANDS);
    for (int i = 0; i < MAX_BANDS; i++) {
        v[i].type = BandType::PEAK;
        v[i].freq_hz = 40.0f * std::pow(2.0f, (float)i);
        v[i].gain_db = (i % 2 ? -2.0f : 2.5f);
        v[i].q = 1.4f;
        v[i].enabled = true;
    }
    return v;
}

}  // namespace

TEST_CASE("tone and balance at 0: wires, bit-exact") {
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        CAPTURE(fs);
        const auto eq = own_bands();
        auto a = chain(fs, true), b = chain(fs, true);
        a->set_bands(eq.data(), (int)eq.size());
        b->set_bands(eq.data(), (int)eq.size());
        b->set_tone(0.0f, 0.0f);
        b->set_balance_db(0.0f);
        b->set_tone(NAN, NAN);                   // not a number: no shelf
        b->set_balance_db(NAN);
        uint32_t seed = 7;
        float x[DspChain::BLOCK * 2], y[DspChain::BLOCK * 2];
        bool same = true;
        for (int k = 0; k < 200; k++) {
            for (float& v : x) v = (float)((int32_t)lcg(seed)) / 4.3e9f;
            memcpy(y, x, sizeof(x));
            a->process(x, DspChain::BLOCK);
            b->process(y, DspChain::BLOCK);
            same &= memcmp(x, y, sizeof(x)) == 0;
        }
        CHECK(same);
        CHECK(a->headroom_db() == b->headroom_db());
    }
}

TEST_CASE("Бас and Високі: their curve at the ends and nothing in the middle, EQ on or off") {
    for (bool eq_on : {true, false}) {
        for (double fs : {48000.0, 96000.0}) {
            CAPTURE(eq_on);
            CAPTURE(fs);
            const float bass = 6.0f, treble = -4.0f;
            const auto want = shelves(bass, treble);
            for (double hz : {30.0, 100.0, 1000.0, 10000.0, 16000.0}) {
                CAPTURE(hz);
                auto ref = chain(fs, eq_on), toned = chain(fs, eq_on);
                toned->set_tone(bass, treble);
                const double got = db(sine_rms(*toned, fs, hz) / sine_rms(*ref, fs, hz));
                CHECK(std::fabs(got - designed_db(want, fs, hz)) < 0.05);
            }
        }
    }
    // between the corners they leave the voice nearly alone; at the ends, nearly all of it
    const auto want = shelves(6.0f, 6.0f);
    CHECK(std::fabs(designed_db(want, 48000.0, 1000.0)) < 0.4);
    CHECK(designed_db(shelves(6.0f, 0.0f), 48000.0, 25.0) > 5.5);
    CHECK(designed_db(shelves(0.0f, 6.0f), 48000.0, 18000.0) > 5.0);
}

TEST_CASE("Бас and Високі count in B, EQ on or off, and sit on top of the EQ's own ten bands") {
    for (double fs : {48000.0, 96000.0}) {
        CAPTURE(fs);
        auto c = chain(fs, false);
        c->set_tone(6.0f, 0.0f);
        CHECK(std::fabs(c->headroom_db() - dense_max_db(shelves(6.0f, 0.0f), fs)) < 0.05);
        c->set_tone(-6.0f, -6.0f);                    // a cut costs no headroom (float noise aside)
        CHECK(c->headroom_db() < 0.01f);

        auto e = chain(fs, true);
        const auto eq = own_bands();
        e->set_bands(eq.data(), (int)eq.size());
        e->set_tone(4.0f, 3.0f);
        auto all = eq;
        for (const auto& s : shelves(4.0f, 3.0f)) all.push_back(s);
        CHECK(std::fabs(e->headroom_db() - dense_max_db(all, fs)) < 0.05);
        e->set_eq_enabled(false);                     // the EQ off, the shelves still play
        CHECK(std::fabs(e->headroom_db() - dense_max_db(shelves(4.0f, 3.0f), fs)) < 0.05);
    }
}

TEST_CASE("where the speakers stand: the room's cut on Бас's shelf, Бас keeping its ±6 over it") {
    for (double fs : {48000.0, 96000.0}) {
        CAPTURE(fs);
        // by a wall, in a corner: the low shelf the cut asks for, alone or with Бас on it
        for (int place : {1, 2}) {
            for (float bass : {0.0f, -6.0f, 4.0f}) {
                CAPTURE(place);
                CAPTURE(bass);
                const float place_db = DspChain::PLACE_DB[place];
                const auto want = shelves(bass + place_db, 0.0f);
                for (double hz : {30.0, 100.0, 1000.0}) {
                    CAPTURE(hz);
                    auto ref = chain(fs, false), room = chain(fs, false);
                    room->set_tone(bass, 0.0f, place_db);
                    const double got = db(sine_rms(*room, fs, hz) / sine_rms(*ref, fs, hz));
                    CHECK(std::fabs(got - designed_db(want, fs, hz)) < 0.05);
                }
            }
        }
        // a cut costs no headroom; Бас +6 in a corner is a wire, bit-exact
        auto c = chain(fs, false);
        c->set_tone(-6.0f, 0.0f, DspChain::PLACE_DB[2]);
        CHECK(c->headroom_db() < 0.01f);
        auto a = chain(fs, false), b = chain(fs, false);
        b->set_tone(6.0f, 0.0f, DspChain::PLACE_DB[2]);
        uint32_t seed = 11;
        float x[DspChain::BLOCK * 2], y[DspChain::BLOCK * 2];
        bool same = true;
        for (int k = 0; k < 50; k++) {
            for (float& v : x) v = (float)((int32_t)lcg(seed)) / 4.3e9f;
            memcpy(y, x, sizeof(x));
            a->process(x, DspChain::BLOCK);
            b->process(y, DspChain::BLOCK);
            same &= memcmp(x, y, sizeof(x)) == 0;
        }
        CHECK(same);
    }
    // anything above 0 or not a number is free-standing; below the corner is the corner
    const double fs = 48000.0;
    auto free_ = chain(fs, false), up = chain(fs, false), nan_ = chain(fs, false), deep = chain(fs, false);
    up->set_tone(0.0f, 0.0f, 3.0f);
    nan_->set_tone(0.0f, 0.0f, NAN);
    deep->set_tone(0.0f, 0.0f, -20.0f);
    CHECK(std::fabs(db(sine_rms(*up, fs, 30.0) / sine_rms(*free_, fs, 30.0))) < 1e-6);
    CHECK(std::fabs(db(sine_rms(*nan_, fs, 30.0) / sine_rms(*free_, fs, 30.0))) < 1e-6);
    CHECK(std::fabs(db(sine_rms(*deep, fs, 30.0) / sine_rms(*free_, fs, 30.0)) -
                    designed_db(shelves(DspChain::PLACE_DB[2], 0.0f), fs, 30.0)) < 0.05);
}

TEST_CASE("balance: only the other channel goes down, and the louder one is left bit-exact") {
    const double fs = 48000.0;
    for (float bal : {6.0f, -3.5f}) {
        CAPTURE(bal);
        auto ref = chain(fs, false), c = chain(fs, false);
        c->set_balance_db(bal);
        double rl, rr, l, r;
        sine_rms(*ref, fs, 440.0, rl, rr);
        sine_rms(*c, fs, 440.0, l, r);
        if (bal > 0) {
            CHECK(std::fabs(db(l / rl) + bal) < 0.01);
            CHECK(r == rr);
        } else {
            CHECK(std::fabs(db(r / rr) - bal) < 0.01);
            CHECK(l == rl);
        }
    }
    // past its reach it stops at BAL_MAX_DB, and it costs no headroom
    auto c = chain(fs, false), ref = chain(fs, false);
    c->set_balance_db(40.0f);
    CHECK(c->headroom_db() == 0.0f);
    double rl, rr, l, r;
    sine_rms(*ref, fs, 440.0, rl, rr);
    sine_rms(*c, fs, 440.0, l, r);
    CHECK(std::fabs(db(l / rl) + DspChain::BAL_MAX_DB) < 0.01);
}

TEST_CASE("balance glides: a step is spread over the volume's time, not one sample") {
    const double fs = 48000.0;
    auto c = chain(fs, false);
    float blk[DspChain::BLOCK * 2];
    for (int k = 0; k < 20; k++) {                    // a settled DC on both channels
        for (float& v : blk) v = 0.25f;
        c->process(blk, DspChain::BLOCK);
    }
    const float before = blk[0];
    c->set_balance_db(6.0f);
    for (float& v : blk) v = 0.25f;
    c->process(blk, DspChain::BLOCK);
    // the first sample hardly moved; the left settles well under where it was within the block
    CHECK(blk[0] > 0.99f * before);
    CHECK(blk[2 * (DspChain::BLOCK - 1)] < before);
    float step = 0.0f;
    for (size_t i = 1; i < DspChain::BLOCK; i++) step = std::max(step, std::fabs(blk[2 * i] - blk[2 * (i - 1)]));
    CHECK(step < 0.01f * before);
}
