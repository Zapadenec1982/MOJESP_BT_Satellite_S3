#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace radio_core {
// read_wire returns bytes, 0 for timeout, -1 for FIN, or another negative error.
// A timeout preserves every framing state, including partial chunk-size lines.
class HttpBody {
  public:
    static constexpr int END = -100;
    static constexpr int ERROR = -101;
    void reset(bool sized, uint64_t length, bool chunks) {
        *this = HttpBody{};
        sized_ = sized;
        left_ = length;
        chunks_ = chunks;
    }
    template <class Read> int read(char *out, size_t cap, Read wire) {
        if (failed_)
            return ERROR;
        if (done_)
            return END;
        if (!cap)
            return 0;
        if (!chunks_) {
            if (sized_ && !left_) {
                done_ = true;
                return END;
            }
            size_t n = sized_ && left_ < cap ? static_cast<size_t>(left_) : cap;
            int r = wire(out, n);
            if (r > 0) {
                if (sized_)
                    left_ -= r;
                return r;
            }
            if (r == -1 && !sized_) {
                done_ = true;
                return END;
            }
            if (r < 0) {
                failed_ = true;
                return ERROR;
            }
            return 0;
        }
        for (;;) {
            if (phase_ == 1 && left_) {
                size_t n = left_ < cap ? static_cast<size_t>(left_) : cap;
                int r = wire(out, n);
                if (r > 0) {
                    left_ -= r;
                    return r;
                }
                if (!r)
                    return 0;
                failed_ = true;
                return ERROR;
            }
            char c = 0;
            int r = wire(&c, 1);
            if (!r)
                return 0;
            if (r < 0) {
                failed_ = true;
                return ERROR;
            }
            if (phase_ == 1) {
                if (c != '\r') {
                    failed_ = true;
                    return ERROR;
                }
                phase_ = 2;
                continue;
            }
            if (phase_ == 2) {
                if (c != '\n') {
                    failed_ = true;
                    return ERROR;
                }
                phase_ = 0;
                continue;
            }
            if (used_ == sizeof(line_) - 1 || (phase_ == 3 && ++trailers_ > 4096)) {
                failed_ = true;
                return ERROR;
            }
            line_[used_++] = c;
            if (c != '\n')
                continue;
            if (used_ < 2 || line_[used_ - 2] != '\r') {
                failed_ = true;
                return ERROR;
            }
            if (phase_ == 3) {
                if (used_ == 2) {
                    done_ = true;
                    return END;
                }
                used_ = 0;
                continue;
            }
            uint64_t size = 0;
            size_t i = 0;
            for (; i < used_ - 2 && line_[i] != ';'; ++i) {
                char x = line_[i];
                unsigned d = x >= '0' && x <= '9'   ? x - '0'
                             : x >= 'a' && x <= 'f' ? x - 'a' + 10
                             : x >= 'A' && x <= 'F' ? x - 'A' + 10
                                                    : 99;
                if (d > 15 || size > (UINT64_MAX - d) / 16) {
                    failed_ = true;
                    return ERROR;
                }
                size = size * 16 + d;
            }
            if (!i) {
                failed_ = true;
                return ERROR;
            }
            used_ = 0;
            left_ = size;
            phase_ = size ? 1 : 3;
        }
    }

  private:
    bool sized_ = false, chunks_ = false, done_ = false, failed_ = false;
    uint64_t left_ = 0;
    unsigned phase_ = 0, trailers_ = 0;
    size_t used_ = 0;
    char line_[128]{};
};
} // namespace radio_core
