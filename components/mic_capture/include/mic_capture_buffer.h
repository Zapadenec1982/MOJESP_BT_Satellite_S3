#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace mic_capture {

enum class State { idle, armed, triggered, ready };

struct Span {
    const std::int16_t* data;
    std::size_t size;
};

// Caller owns capacity mono samples for the lifetime of Buffer. No allocation or
// synchronization: serialize access externally; input must not alias storage.
class Buffer {
public:
    // Fixed post count = capacity - pre_limit, even with partial prehistory.
    // Null storage, zero capacity or pre_limit > capacity are invalid.
    Buffer(std::int16_t* storage, std::size_t capacity, std::size_t pre_limit)
        : storage_(storage), capacity_(capacity), pre_limit_(pre_limit) {}

    bool valid() const noexcept {
        return storage_ && capacity_ != 0 && pre_limit_ <= capacity_;
    }

    // Clear logical contents without touching storage; reset disarms, arm restarts.
    void reset() noexcept {
        state_ = State::idle;
        start_ = write_ = size_ = pre_count_ = 0;
    }
    bool arm() noexcept {
        reset();
        if (!valid()) return false;
        state_ = State::armed;
        return true;
    }

    // Consume rolling prehistory while armed; after trigger accept only the fixed
    // post count. Returns consumed samples (including discarded rolling history).
    std::size_t push(const std::int16_t* samples, std::size_t n) noexcept {
        if (!samples || (state_ != State::armed && state_ != State::triggered)) return 0;
        if (state_ == State::armed && pre_limit_ == 0) return n;
        std::size_t consumed = 0;
        while (consumed < n && state_ != State::ready) {
            storage_[write_] = samples[consumed++];
            write_ = next(write_);
            if (state_ == State::armed) {
                if (size_ == pre_limit_) start_ = next(start_);
                else ++size_;
                pre_count_ = size_;
            } else {
                ++size_;
                if (size_ - pre_count_ == capacity_ - pre_limit_) state_ = State::ready;
            }
        }
        return consumed;
    }

    // Trigger lies between samples: the next pushed sample is the first post
    // sample. Reject idle/retrigger; zero post count freezes immediately.
    bool trigger() noexcept {
        if (state_ != State::armed) return false;
        state_ = capacity_ == pre_limit_ ? State::ready : State::triggered;
        return true;
    }

    State state() const noexcept { return state_; }
    std::size_t size() const noexcept { return size_; }
    // While armed, pre_count tracks rolling history; after trigger it is fixed.
    std::size_t pre_count() const noexcept { return pre_count_; }
    // Chronological boundary / index of first post sample (meaningful after trigger).
    std::size_t trigger_index() const noexcept { return pre_count_; }

    // Concatenate these spans to export oldest-first, without copying. Views are
    // invalidated by mutation; ready data stays frozen until reset/arm. PCM values
    // are native int16_t: WAV payload must be serialized little-endian.
    std::array<Span, 2> spans() const noexcept {
        if (size_ == 0) return {{{nullptr, 0}, {nullptr, 0}}};
        const std::size_t tail = capacity_ - start_;
        const std::size_t first = size_ < tail ? size_ : tail;
        return {{{storage_ + start_, first},
                 {size_ == first ? nullptr : storage_, size_ - first}}};
    }

private:
    std::size_t next(std::size_t index) const noexcept {
        return index == capacity_ - 1 ? 0 : index + 1;
    }
    std::int16_t* storage_;
    std::size_t capacity_;
    std::size_t pre_limit_;
    std::size_t start_ = 0, write_ = 0, size_ = 0, pre_count_ = 0;
    State state_ = State::idle;
};

using WavHeader = std::array<std::uint8_t, 44>;

// Canonical RIFF/WAVE PCM mono, 16-bit, 48000 Hz header. Count is samples, not
// bytes. Reject lengths exceeding RIFF's 32-bit size; leave output unchanged.
inline bool make_wav_header(std::uint64_t sample_count, WavHeader& out) noexcept {
    if (sample_count > (std::numeric_limits<std::uint32_t>::max() - 36ULL) / 2) return false;
    WavHeader h = {{'R','I','F','F',0,0,0,0,'W','A','V','E',
                    'f','m','t',' ',16,0,0,0,1,0,1,0,
                    0x80,0xbb,0,0,0,0x77,1,0,2,0,16,0,
                    'd','a','t','a',0,0,0,0}};
    const auto bytes = static_cast<std::uint32_t>(sample_count * 2);
    for (unsigned i = 0; i < 4; ++i) {
        h[4 + i] = static_cast<std::uint8_t>((bytes + 36) >> (8 * i));
        h[40 + i] = static_cast<std::uint8_t>(bytes >> (8 * i));
    }
    out = h;
    return true;
}

} // namespace mic_capture
