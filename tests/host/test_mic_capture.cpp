#include "doctest.h"
#include "mic_capture_buffer.h"

#include <algorithm>
#include <limits>
#include <vector>

using mic_capture::Buffer;
using mic_capture::State;

namespace {
std::vector<std::int16_t> exported(const Buffer& b) {
    std::vector<std::int16_t> result;
    for (const auto span : b.spans()) {
        if (span.size) result.insert(result.end(), span.data, span.data + span.size);
    }
    return result;
}
std::uint32_t le32(const mic_capture::WavHeader& h, std::size_t offset) {
    return std::uint32_t(h[offset]) | (std::uint32_t(h[offset + 1]) << 8)
        | (std::uint32_t(h[offset + 2]) << 16) | (std::uint32_t(h[offset + 3]) << 24);
}
}

TEST_CASE("mic capture: rolling wrap exports borrowed chronological spans") {
    std::int16_t storage[7] = {};
    Buffer b(storage, 7, 3);
    REQUIRE(b.arm());
    const std::int16_t input[] = {0,1,2,3,4,5,6,7};
    CHECK(b.push(input, 8) == 8);
    CHECK(exported(b) == std::vector<std::int16_t>{5,6,7});
    CHECK(b.pre_count() == 3);
    REQUIRE(b.trigger());
    CHECK(b.trigger_index() == 3);
    const std::int16_t post[] = {8,9,10,11,12,13};
    CHECK(b.push(post, 6) == 4);
    CHECK(b.state() == State::ready);
    CHECK(b.size() == 7);
    const auto spans = b.spans();
    CHECK(spans[0].data == storage + 5);
    CHECK(spans[0].size == 2);
    CHECK(spans[1].data == storage);
    CHECK(spans[1].size == 5);
    const auto frozen = exported(b);
    CHECK(frozen == std::vector<std::int16_t>{5,6,7,8,9,10,11});
    CHECK_FALSE(b.trigger());
    CHECK(b.push(post, 6) == 0);
    CHECK(exported(b) == frozen);
}

TEST_CASE("mic capture: partial history keeps fixed post count and rejects retrigger") {
    std::int16_t storage[8] = {};
    Buffer b(storage, 8, 5);
    const std::int16_t input[] = {-32768,32767,12,13,14,15};
    CHECK(b.state() == State::idle);
    CHECK_FALSE(b.trigger());
    CHECK(b.push(input, 6) == 0);
    REQUIRE(b.arm());
    CHECK(b.push(input, 2) == 2);
    REQUIRE(b.trigger());
    CHECK(b.pre_count() == 2);
    CHECK(b.trigger_index() == 2);
    CHECK(b.push(input + 2, 2) == 2);
    CHECK(b.state() == State::triggered);
    CHECK_FALSE(b.trigger());
    CHECK(b.push(input + 4, 2) == 1);
    CHECK(b.state() == State::ready);
    CHECK(b.size() == 5);
    CHECK(exported(b) == std::vector<std::int16_t>{-32768,32767,12,13,14});
    REQUIRE(b.arm());
    CHECK(b.size() == 0);
    CHECK(b.pre_count() == 0);
    CHECK(b.trigger_index() == 0);
    CHECK(exported(b).empty());
    REQUIRE(b.trigger());
    CHECK(b.push(input + 2, 4) == 3);
    CHECK(exported(b) == std::vector<std::int16_t>{12,13,14});
    b.reset();
    CHECK(b.state() == State::idle);
    CHECK(b.size() == 0);
    CHECK(b.pre_count() == 0);
    CHECK(b.push(input, 1) == 0);
}

TEST_CASE("mic capture: all small capacities, history lengths and chunk boundaries") {
    for (std::size_t capacity = 1; capacity <= 9; ++capacity) {
        for (std::size_t pre = 0; pre <= capacity; ++pre) {
            for (std::size_t history = 0; history <= 3 * capacity; ++history) {
                for (std::size_t chunk = 1; chunk <= capacity + 1; ++chunk) {
                    CAPTURE(capacity);
                    CAPTURE(pre);
                    CAPTURE(history);
                    CAPTURE(chunk);
                    std::vector<std::int16_t> storage(capacity + 2, -999);
                    Buffer b(storage.data() + 1, capacity, pre);
                    REQUIRE(b.arm());
                    std::vector<std::int16_t> input(history + capacity + 2);
                    for (std::size_t i = 0; i < input.size(); ++i) input[i] = static_cast<std::int16_t>(i);
                    for (std::size_t i = 0; i < history;) {
                        const auto count = std::min(chunk, history - i);
                        REQUIRE(b.push(input.data() + i, count) == count);
                        i += count;
                    }
                    const auto kept = std::min(pre, history);
                    const std::vector<std::int16_t> before(input.begin() + history - kept, input.begin() + history);
                    CHECK(exported(b) == before);
                    REQUIRE(b.trigger());
                    CHECK(b.trigger_index() == kept);
                    const auto post = capacity - pre;
                    std::size_t accepted = 0;
                    for (std::size_t i = history; i < input.size();) {
                        const auto count = std::min(chunk, input.size() - i);
                        accepted += b.push(input.data() + i, count);
                        i += count;
                    }
                    CHECK(accepted == post);
                    CHECK(b.state() == State::ready);
                    CHECK(b.pre_count() == kept);
                    CHECK(b.size() == kept + post);
                    CHECK(exported(b) == std::vector<std::int16_t>(input.begin() + history - kept, input.begin() + history + post));
                    CHECK(storage.front() == -999);
                    CHECK(storage.back() == -999);
                }
            }
        }
    }
}

TEST_CASE("mic capture: invalid configuration and empty pushes are safe") {
    std::int16_t storage[2] = {1,2};
    Buffer null(nullptr, 2, 1), empty(storage, 0, 0), excessive(storage, 2, 3);
    for (auto* b : {&null, &empty, &excessive}) {
        CHECK_FALSE(b->valid());
        CHECK_FALSE(b->arm());
        CHECK_FALSE(b->trigger());
        CHECK(b->push(storage, 2) == 0);
        CHECK(exported(*b).empty());
    }
    Buffer b(storage, 2, 1);
    REQUIRE(b.arm());
    CHECK(b.push(nullptr, 10) == 0);
    CHECK(b.push(storage, 0) == 0);
    CHECK(b.size() == 0);
    REQUIRE(b.trigger());
    CHECK(b.push(nullptr, 10) == 0);
    CHECK(b.state() == State::triggered);
}

TEST_CASE("mic capture: WAV header encodes PCM mono16 48k and checked RIFF lengths") {
    mic_capture::WavHeader h{};
    REQUIRE(mic_capture::make_wav_header(48000, h));
    const mic_capture::WavHeader expected = {{
        'R','I','F','F',0x24,0x77,1,0,'W','A','V','E',
        'f','m','t',' ',16,0,0,0,1,0,1,0,0x80,0xbb,0,0,
        0,0x77,1,0,2,0,16,0,'d','a','t','a',0,0x77,1,0}};
    CHECK(h == expected);
    for (const std::uint64_t count : {0ULL, 1ULL, 257ULL, 48000ULL, 2147483629ULL}) {
        REQUIRE(mic_capture::make_wav_header(count, h));
        CHECK(le32(h, 4) == 36 + count * 2);
        CHECK(le32(h, 40) == count * 2);
    }
    const auto previous = h;
    CHECK_FALSE(mic_capture::make_wav_header(2147483630ULL, h));
    CHECK(h == previous);
    CHECK_FALSE(mic_capture::make_wav_header(std::numeric_limits<std::uint64_t>::max(), h));
    CHECK(h == previous);
}
