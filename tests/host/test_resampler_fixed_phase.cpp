#include "doctest.h"
#include "resampler.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace {
using Context = std::unique_ptr<Resample, decltype(&resampleFree)>;

Context make_context(int source, int destination, int flags = BLACKMAN_HARRIS) {
    Context r(resampleFixedRatioInit(2, 48, 320, source, destination, 0, flags), resampleFree);
    REQUIRE(r != nullptr);
    return r;
}

std::vector<float> signal(int frames) {
    std::vector<float> result(frames * 2);
    for (int i = 0; i < frames; ++i) {
        result[2*i] = 0.3f * std::sin(0.071f * i) + (i % 727 == 0 ? 0.2f : 0.0f);
        result[2*i+1] = 0.2f * std::cos(0.113f * i) - (i % 991 == 0 ? 0.3f : 0.0f);
    }
    return result;
}

// Drive each API independently: their floating/integer boundary decisions may
// partition calls differently, but all consumed input and final output must agree.
std::vector<float> convert(Resample* r, const std::vector<float>& input,
                           bool interleaved, bool fragmented) {
    const int frames = static_cast<int>(input.size() / 2);
    std::vector<float> left(frames), right(frames), result;
    for (int i = 0; i < frames; ++i) {
        left[i] = input[2*i]; right[i] = input[2*i+1];
    }
    const int chunks[] = {1, 17, 719, 3, 1024, 161, 47};
    const int capacities[] = {1, 2, 7, 3, 31};
    int consumed = 0, call = 0;
    bool flushing = false;
    while (true) {
        REQUIRE(call < frames * 6 + 1000); // a stalled conversion must fail, not hang
        const int count = std::min(frames - consumed, fragmented ? chunks[call % 7] : 1024);
        const int capacity = fragmented ? capacities[call % 5] : 2048;
        std::vector<float> out(capacity * 2 + 2, 12345.0f);
        std::vector<float> out_l(capacity + 1, 12345.0f), out_r(capacity + 1, 12345.0f);
        const bool at_end = consumed == frames;
        const int supplied = at_end ? (flushing ? 0 : -1) : count;
        ResampleResult step;
        if (interleaved) {
            step = resampleProcessInterleaved(r, at_end ? nullptr : input.data() + consumed*2,
                                              supplied, out.data(), capacity, 0.123);
        } else {
            const float* in[] = {left.data() + consumed, right.data() + consumed};
            float* planar[] = {out_l.data(), out_r.data()};
            step = resampleProcess(r, at_end ? nullptr : in, supplied, planar, capacity, 0.123);
        }
        REQUIRE(step.input_used <= static_cast<unsigned>(count));
        REQUIRE(step.output_generated <= static_cast<unsigned>(capacity));
        CHECK(out[capacity*2] == 12345.0f);
        CHECK(out[capacity*2+1] == 12345.0f);
        CHECK(out_l[capacity] == 12345.0f);
        CHECK(out_r[capacity] == 12345.0f);
        for (unsigned i = 0; i < step.output_generated; ++i) {
            result.push_back(interleaved ? out[2*i] : out_l[i]);
            result.push_back(interleaved ? out[2*i+1] : out_r[i]);
        }
        consumed += static_cast<int>(step.input_used);
        ++call;
        if (at_end) {
            flushing = true;
            if (!step.output_generated) break;
        } else {
            REQUIRE(step.input_used + step.output_generated > 0);
        }
    }
    CHECK(consumed == frames);
    float out[2];
    const auto ignored = resampleProcessInterleaved(r, input.data(), 1, out, 1, 1.0);
    CHECK(ignored.input_used == 0);
    CHECK(ignored.output_generated == 0);
    return result;
}

void same_audio(const std::vector<float>& actual, const std::vector<float>& expected) {
    REQUIRE(actual.size() == expected.size());
    float max_error = 0;
    for (std::size_t i = 0; i < actual.size(); ++i) {
        REQUIRE(std::isfinite(actual[i]));
        max_error = std::max(max_error, std::fabs(actual[i] - expected[i]));
    }
    CHECK(max_error < 2e-6f);
}

// Observe fallback phase arithmetic without dereferencing a deliberately negative
// or unusually large sample position. Production FIR coefficients stay untouched.
artsample_t phase_probe(Resample*, artsample_t*, int base, float fraction) {
    return static_cast<float>(base) + fraction;
}
}

TEST_CASE("fixed phase: planar reference, tiny outputs, compaction, reset and flush") {
    const int rates[][2] = {{44100,48000}, {48000,44100}, {96000,48000}};
    const auto input = signal(10007);
    for (const auto& rate : rates) {
        CAPTURE(rate[0]); CAPTURE(rate[1]);
        auto planar = make_context(rate[0], rate[1]);
        auto regular = make_context(rate[0], rate[1]);
        auto fragmented = make_context(rate[0], rate[1]);
        const auto reference = convert(planar.get(), input, false, true);
        const auto actual = convert(fragmented.get(), input, true, true);
        same_audio(actual, reference);
        same_audio(convert(regular.get(), input, true, false), reference);
        // The library emits its initial half-filter delay and flushes a matching
        // half-filter tail; preserve that existing output-length contract.
        const auto expected_frames = static_cast<std::size_t>(
            std::ceil(static_cast<double>(input.size()/2 + 24) * rate[1] / rate[0]));
        CHECK(actual.size()/2 == expected_frames);
        resampleReset(fragmented.get());
        same_audio(convert(fragmented.get(), input, true, true), actual);
    }
}

TEST_CASE("fixed phase: long run retains exact rational phase across calls") {
    auto planar = make_context(44100, 48000);
    auto interleaved = make_context(44100, 48000);
    const auto input = signal(100003);
    same_audio(convert(interleaved.get(), input, true, false),
               convert(planar.get(), input, false, false));
}

TEST_CASE("fixed phase: guards preserve fallback position and sample arithmetic") {
    for (int scenario = 0; scenario < 7; ++scenario) {
        CAPTURE(scenario);
        auto planar = make_context(44100, 48000);
        auto interleaved = make_context(44100, 48000);
        for (auto* r : {planar.get(), interleaved.get()}) {
            r->flags &= ~RESAMPLER_SNAP_OFFSET;
            r->subsample = phase_probe;
            r->inputIndex = 64;
            switch (scenario) {
            case 0: r->outputOffset = -0.25; break; // negative fractional position
            case 1: r->outputOffset = 24.003; break; // not on the bank's phase grid
            case 2: r->fixedRatio = 1.1; break; // denominator / ratio is nonintegral
            case 3: r->outputOffset = 20000000.003; break; // scaled position > int32
            case 4: r->fixedRatio = 1e-10; break; // step > int32
            case 5: r->flags |= SUBSAMPLE_INTERPOLATE; break;
            case 6: r->flags &= ~RESAMPLE_FIXED_RATIO; break;
            }
        }
        float out[4] = {}, l[2] = {}, rr[2] = {};
        float* outputs[] = {l, rr};
        const auto a = resampleProcessInterleaved(interleaved.get(), nullptr, 0, out, 2, 1.031);
        const auto b = resampleProcess(planar.get(), nullptr, 0, outputs, 2, 1.031);
        CHECK(a.input_used == b.input_used);
        REQUIRE(a.output_generated == b.output_generated);
        for (unsigned i = 0; i < a.output_generated; ++i) {
            CHECK(out[2*i] == l[i]); CHECK(out[2*i+1] == rr[i]);
        }
        CHECK(interleaved->outputOffset == planar->outputOffset);
        CHECK(interleaved->inputIndex == planar->inputIndex);
    }
}

TEST_CASE("fixed phase: zero capacity leaves aligned state and input untouched") {
    auto r = make_context(44100, 48000);
    const double position = r->outputOffset;
    const int index = r->inputIndex;
    float in[2] = {1,-1}, out[2] = {9,9};
    const auto result = resampleProcessInterleaved(r.get(), in, 1, out, 0, 1.0);
    CHECK(result.input_used == 0);
    CHECK(result.output_generated == 0);
    CHECK(r->outputOffset == position);
    CHECK(r->inputIndex == index);
    CHECK(out[0] == 9); CHECK(out[1] == 9);
}
