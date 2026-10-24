/**
 * @file test_resampler.cpp
 * @brief The fixed-ratio resampler, held still while its arithmetic is replaced.
 *
 * The resampler was measured on the bench costing about 45% of one core at a 48 kHz output,
 * which is roughly seven times what the filtering itself should need. Disassembling the
 * project's own object showed why: the sample path is float, but the PHASE path is double,
 * and this chip is built `-mabi=ilp32f`, so every double operation is a soft-float library
 * call. Twenty of them per stereo output frame, one of which is a divide.
 *
 * Replacing that arithmetic is worth doing and is exactly the kind of change that quietly
 * ruins the audio. So this test pins the OUTPUT, not the implementation: feed a sine, check
 * what comes back is that sine, at the right frequency, the right level and with the error
 * down where a 48-tap filter should put it. Any rewrite that keeps these numbers is safe.
 *
 * Nothing here knows how the resampler works inside, and that is the point.
 */

#include "doctest.h"

extern "C" {
#include "resampler.h"
}

#include "pcm_ring.h"   // fallback_rate: which rate a refused one converts to

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

constexpr int    TAPS    = 48;      ///< what the player builds with
constexpr int    FILTERS = 320;     ///< ditto: >= the reduced denominator for 44.1 -> 48
constexpr double PI      = 3.14159265358979323846;

/// One second of a sine, interleaved stereo, right channel inverted so a swapped or
/// collapsed channel cannot pass unnoticed.
std::vector<float> sine(double rate_hz, double freq_hz, double seconds, double amp = 0.5) {
    const size_t frames = (size_t)(rate_hz * seconds);
    std::vector<float> out(frames * 2);
    for (size_t i = 0; i < frames; i++) {
        const float s = (float)(amp * std::sin(2.0 * PI * freq_hz * (double)i / rate_hz));
        out[2 * i]     = s;
        out[2 * i + 1] = -s;
    }
    return out;
}

/// Peak magnitude, ignoring the filter's settling time at both ends.
float peak(const std::vector<float>& x, size_t skip, int stride, int offset) {
    float p = 0.0f;
    for (size_t i = skip; i + skip < x.size() / (size_t)stride; i++) {
        const float v = std::fabs(x[i * (size_t)stride + (size_t)offset]);
        if (v > p) p = v;
    }
    return p;
}

/// How far the output is from the sine it should be, as a fraction of the signal.
///
/// Measured against an ideal sine whose phase is recovered from the data itself, because a
/// resampler delays by half its tap length and the delay is not the thing under test.
double residual(const std::vector<float>& x, double rate_hz, double freq_hz,
                double amp, size_t skip, int stride, int offset) {
    // Fit phase by correlating against sin and cos at the known frequency.
    double si = 0.0, co = 0.0;
    const size_t n = x.size() / (size_t)stride;
    for (size_t i = skip; i + skip < n; i++) {
        const double t = 2.0 * PI * freq_hz * (double)i / rate_hz;
        const double v = x[i * (size_t)stride + (size_t)offset];
        si += v * std::sin(t);
        co += v * std::cos(t);
    }
    const double phase = std::atan2(co, si);

    double err = 0.0, sig = 0.0;
    for (size_t i = skip; i + skip < n; i++) {
        const double t = 2.0 * PI * freq_hz * (double)i / rate_hz + phase;
        const double want = amp * std::sin(t);
        const double got  = x[i * (size_t)stride + (size_t)offset];
        err += (got - want) * (got - want);
        sig += want * want;
    }
    return sig > 0.0 ? std::sqrt(err / sig) : 1.0;
}

/// Run a whole buffer through in CHUNK-sized pieces, the way the player does.
std::vector<float> convert(Resample* r, const std::vector<float>& in, double ratio,
                           size_t chunk = 1024) {
    std::vector<float> out;
    const size_t in_frames = in.size() / 2;
    // Room for the ratio plus slack; the resampler is told the real limit per call.
    std::vector<float> scratch(chunk * 2 * 8);
    size_t done = 0;
    while (done < in_frames) {
        size_t n = in_frames - done;
        if (n > chunk) n = chunk;
        const ResampleResult res = resampleProcessInterleaved(
            r, in.data() + done * 2, (int)n, scratch.data(),
            (int)(scratch.size() / 2), ratio);
        out.insert(out.end(), scratch.begin(),
                   scratch.begin() + (long)(res.output_generated * 2));
        // input_used can be short when the output buffer fills; honour it.
        done += res.input_used ? (size_t)res.input_used : n;
    }
    return out;
}

}  // namespace

TEST_CASE("resampler: 22.05 kHz up to 44.1 kHz is an exact doubling") {
    // The case the player actually meets: a rate below what the DAC can lock, converted to
    // the nearest rate in its own family. gcd(22050, 44100) = 22050, so the reduced
    // denominator is 2 and the library needs exactly two filters.
    Resample* r = resampleFixedRatioInit(2, TAPS, FILTERS, 22050.0, 44100.0, 0,
                                         BLACKMAN_HARRIS);
    REQUIRE(r != nullptr);

    const auto in  = sine(22050.0, 1000.0, 0.5);
    const auto out = convert(r, in, 2.0);

    // Two output frames per input frame, within a tap of settling.
    const size_t in_frames  = in.size() / 2;
    const size_t out_frames = out.size() / 2;
    CHECK(out_frames > 2 * in_frames - 4 * (size_t)TAPS);
    CHECK(out_frames <= 2 * in_frames + 4 * (size_t)TAPS);

    const size_t skip = (size_t)TAPS * 2;
    CHECK(peak(out, skip, 2, 0) == doctest::Approx(0.5).epsilon(0.02));
    CHECK(peak(out, skip, 2, 1) == doctest::Approx(0.5).epsilon(0.02));

    // A 1 kHz tone is nowhere near the 22.05 kHz Nyquist, so the filter should leave it
    // alone. 1% residual is generous for 48 taps and catches any arithmetic that has gone
    // wrong without failing on the last bit.
    CHECK(residual(out, 44100.0, 1000.0, 0.5, skip, 2, 0) < 0.01);

    resampleFree(r);
}

TEST_CASE("resampler: 44.1 kHz to 48 kHz keeps the tone") {
    // The awkward ratio: gcd is 300, so the reduced denominator is 160 and the phase lands
    // on a different filter almost every sample. This is where sloppy phase arithmetic shows.
    Resample* r = resampleFixedRatioInit(2, TAPS, FILTERS, 44100.0, 48000.0, 0,
                                         BLACKMAN_HARRIS);
    REQUIRE(r != nullptr);

    const auto in  = sine(44100.0, 1000.0, 0.25);
    const auto out = convert(r, in, 48000.0 / 44100.0);

    const size_t skip = (size_t)TAPS * 2;
    CHECK(peak(out, skip, 2, 0) == doctest::Approx(0.5).epsilon(0.02));
    CHECK(residual(out, 48000.0, 1000.0, 0.5, skip, 2, 0) < 0.01);

    resampleFree(r);
}

TEST_CASE("resampler: 96 kHz down to 48 kHz halves without aliasing the tone") {
    // Downsampling, where the lowpass matters. A 1 kHz tone must survive; the test is that
    // it survives at the right level rather than that nothing aliases, which needs a sweep.
    Resample* r = resampleFixedRatioInit(2, TAPS, FILTERS, 96000.0, 48000.0, 0,
                                         BLACKMAN_HARRIS | INCLUDE_LOWPASS);
    REQUIRE(r != nullptr);

    const auto in  = sine(96000.0, 1000.0, 0.25);
    const auto out = convert(r, in, 0.5);

    CHECK(out.size() / 2 > in.size() / 2 / 2 - 2 * (size_t)TAPS);

    const size_t skip = (size_t)TAPS * 2;
    CHECK(peak(out, skip, 2, 0) == doctest::Approx(0.5).epsilon(0.03));
    CHECK(residual(out, 48000.0, 1000.0, 0.5, skip, 2, 0) < 0.02);

    resampleFree(r);
}

TEST_CASE("resampler: a ratio of two takes the integer phase and gives the double path's samples, bit for bit") {
    // resampleProcessInterleaved() runs an exact fixed ratio on a Q32 phase (no soft doubles
    // per frame on the P4); resampleProcess() keeps the double phase. Both must produce the
    // same output, sample for sample, across uneven calls — and 44.1 -> 48, which stays on
    // the double path in both, still agrees too.
    const double pairs[][2] = {{192000.0, 96000.0}, {176400.0, 88200.0}, {176400.0, 44100.0},
                               {22050.0, 44100.0}, {44100.0, 48000.0}};
    for (const auto& pr : pairs) {
        CAPTURE(pr[0]);
        CAPTURE(pr[1]);
        // The flags RateConverter::ensure() (audio_pipe.cpp) builds with.
        const int flags = BLACKMAN_HARRIS | INCLUDE_LOWPASS | SUBSAMPLE_INTERPOLATE;
        Resample* a = resampleFixedRatioInit(2, TAPS, FILTERS, pr[0], pr[1], 0, flags);
        Resample* b = resampleFixedRatioInit(2, TAPS, FILTERS, pr[0], pr[1], 0, flags);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        const double ratio = pr[1] / pr[0];
        auto in = sine(pr[0], 997.0, 0.2, 0.4);
        const auto hi = sine(pr[0], pr[0] * 0.31, 0.2, 0.2);
        for (size_t i = 0; i < in.size(); i++) in[i] += hi[i];
        const size_t frames = in.size() / 2;
        std::vector<float> l(frames), r(frames);
        for (size_t i = 0; i < frames; i++) { l[i] = in[2 * i]; r[i] = in[2 * i + 1]; }

        std::vector<float> ia, pa;                           // interleaved, planar
        std::vector<float> outi(4096 * 2), outl(4096), outr(4096);
        size_t done_i = 0, done_p = 0, k = 0;
        const size_t chunks[] = {1024, 7, 333, 1, 2048, 240};
        while (done_i < frames || done_p < frames) {
            const size_t want = chunks[k++ % 6];
            if (done_i < frames) {
                const size_t n = std::min(want, frames - done_i);
                const ResampleResult ri = resampleProcessInterleaved(a, in.data() + done_i * 2, (int)n,
                                                                     outi.data(), 4096, ratio);
                ia.insert(ia.end(), outi.begin(), outi.begin() + (long)ri.output_generated * 2);
                done_i += ri.input_used ? ri.input_used : n;
            }
            if (done_p < frames) {
                const size_t n = std::min(want, frames - done_p);
                const float* ins[2] = {l.data() + done_p, r.data() + done_p};
                float* outs[2] = {outl.data(), outr.data()};
                const ResampleResult rp = resampleProcess(b, ins, (int)n, outs, 4096, ratio);
                for (unsigned j = 0; j < rp.output_generated; j++) {
                    pa.push_back(outl[j]);
                    pa.push_back(outr[j]);
                }
                done_p += rp.input_used ? rp.input_used : n;
            }
        }
        CHECK(ia.size() == pa.size());
        const size_t n = std::min(ia.size(), pa.size());
        CHECK(n > frames * ratio * 2 * 0.9);
        CHECK(memcmp(ia.data(), pa.data(), n * sizeof(float)) == 0);
        resampleFree(a);
        resampleFree(b);
    }
}

TEST_CASE("resampler: 192 kHz down to 96 kHz, the ceiling's halving, keeps the band and drops the rest") {
    // What a 192 kHz track now goes through (AudioModule::PLAY_MAX_HZ): the audible band
    // passes at its level, and what lies above the new Nyquist does not fold back into it —
    // 70 kHz would land on 26 kHz.
    const double rates[][2] = {{192000.0, 96000.0}, {176400.0, 88200.0}};
    for (const auto& rr : rates) {
        CAPTURE(rr[0]);
        // The flags RateConverter::ensure() (audio_pipe.cpp) builds with.
        const int flags = BLACKMAN_HARRIS | INCLUDE_LOWPASS | SUBSAMPLE_INTERPOLATE;
        Resample* r = resampleFixedRatioInit(2, TAPS, FILTERS, rr[0], rr[1], 0, flags);
        REQUIRE(r != nullptr);
        auto in = sine(rr[0], 1000.0, 0.25);
        const auto hi = sine(rr[0], 70000.0, 0.25, 0.25);
        for (size_t i = 0; i < in.size(); i++) in[i] += hi[i];
        const auto out = convert(r, in, 0.5);
        const size_t skip = (size_t)TAPS * 2;
        CHECK(residual(out, rr[1], 1000.0, 0.5, skip, 2, 0) < 0.02);
        CHECK(residual(out, rr[1], 1000.0, 0.5, skip, 2, 1) < 0.02);
        resampleFree(r);

        // 20 kHz, the top of the band, at its level.
        r = resampleFixedRatioInit(2, TAPS, FILTERS, rr[0], rr[1], 0, flags);
        REQUIRE(r != nullptr);
        const auto top = convert(r, sine(rr[0], 20000.0, 0.25), 0.5);
        CHECK(peak(top, skip, 2, 0) == doctest::Approx(0.5).epsilon(0.03));
        resampleFree(r);
    }
}

TEST_CASE("resampler: silence in, silence out") {
    // Cheap, and it catches an uninitialised history or a stray offset, both of which make a
    // click at the start of every track rather than a wrong number anywhere.
    Resample* r = resampleFixedRatioInit(2, TAPS, FILTERS, 22050.0, 44100.0, 0,
                                         BLACKMAN_HARRIS);
    REQUIRE(r != nullptr);

    const std::vector<float> in(22050 * 2, 0.0f);
    const auto out = convert(r, in, 2.0);

    REQUIRE(out.size() > 0);
    float worst = 0.0f;
    for (float v : out) worst = std::fmax(worst, std::fabs(v));
    CHECK(worst < 1e-6f);

    resampleFree(r);
}

TEST_CASE("resampler: a reset clears the history, not the filter bank") {
    // The player resets between tracks at the same rate rather than rebuilding, because
    // building 320 filters costs hundreds of milliseconds. Prove a reset really does forget
    // the previous track: full-scale input, then reset, then silence must come out silent.
    Resample* r = resampleFixedRatioInit(2, TAPS, FILTERS, 22050.0, 44100.0, 0,
                                         BLACKMAN_HARRIS);
    REQUIRE(r != nullptr);

    const auto loud = sine(22050.0, 1000.0, 0.05, 0.9);
    convert(r, loud, 2.0);

    resampleReset(r);

    const std::vector<float> quiet(4096 * 2, 0.0f);
    const auto out = convert(r, quiet, 2.0);

    REQUIRE(out.size() > 0);
    float worst = 0.0f;
    for (float v : out) worst = std::fmax(worst, std::fabs(v));
    CHECK(worst < 1e-6f);

    resampleFree(r);
}


TEST_CASE("fallback_rate: a refused rate stays in its own family") {
    using audio_core::fallback_rate;

    // What this board can clock: 44.1 kHz is the DAC's floor, 192 the audio PLL's ceiling.
    constexpr uint32_t LO = 44100, HI = 192000;

    SUBCASE("below the floor, the 44.1 family lands on 44.1") {
        // The case that started this. 22050 to 44100 is exactly two, so the bank collapses
        // from 320 filters to two and the ratio has no fractional phase at all.
        CHECK(fallback_rate(22050, LO, HI) == 44100);
        CHECK(fallback_rate(11025, LO, HI) == 44100);
    }

    SUBCASE("below the floor, the 48 family lands on 48") {
        CHECK(fallback_rate(8000,  LO, HI) == 48000);
        CHECK(fallback_rate(16000, LO, HI) == 48000);
        CHECK(fallback_rate(24000, LO, HI) == 48000);
        CHECK(fallback_rate(32000, LO, HI) == 48000);
    }

    SUBCASE("above the ceiling, halve within the family") {
        CHECK(fallback_rate(384000, LO, HI) == 192000);
        CHECK(fallback_rate(352800, LO, HI) == 176400);
        CHECK(fallback_rate(768000, LO, HI) == 192000);
    }

    SUBCASE("the playback ceiling: 192 and 176.4 kHz halve to 96 and 88.2") {
        // AudioModule::retune() passes min(port maximum, PLAY_MAX_HZ = 96 kHz) as the top: a
        // hi-res track is halved by its producer — a ratio of two, the bank of two filters —
        // and nothing at or under the ceiling moves.
        CHECK(fallback_rate(192000, LO, 96000) == 96000);
        CHECK(fallback_rate(176400, LO, 96000) == 88200);
        CHECK(fallback_rate(384000, LO, 96000) == 96000);
        CHECK(fallback_rate(96000,  LO, 96000) == 48000);   // only ever asked when refused
    }

    SUBCASE("nothing in range is answered honestly, not guessed") {
        // A window with no member of either family in it.
        CHECK(fallback_rate(22050, 50000, 60000) == 0);
        CHECK(fallback_rate(0, LO, HI) == 0);
        CHECK(fallback_rate(22050, HI, LO) == 0);      // inverted window
    }

    SUBCASE("the family test is divisibility, not a list") {
        // 11025 divides every member of the 44.1 family and no member of the 48 family,
        // which is why the rule needs no table to maintain.
        CHECK(fallback_rate(88200,  LO, 48000) == 44100);
        CHECK(fallback_rate(176400, LO, 96000) == 88200);
        CHECK(fallback_rate(96000,  LO, 48000) == 48000);
    }
}
