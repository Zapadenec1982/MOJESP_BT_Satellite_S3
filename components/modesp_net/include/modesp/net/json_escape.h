#pragma once

#include <cstddef>
#include <cstdint>

namespace modesp::net {

// A byte can expand to six JSON bytes (\u00XX). Invalid UTF-8 is replaced by
// U+FFFD, so even a bounded or damaged upstream string remains valid JSON.
constexpr size_t json_escaped_capacity(size_t source_bytes) { return source_bytes * 6 + 1; }

inline bool json_escape(char* out, size_t cap, const char* src, size_t* written = nullptr) {
    static constexpr char hex[] = "0123456789abcdef";
    if (!out || !cap || !src) return false;
    size_t w = 0;
    auto emit = [&](const char* p, size_t n) {
        if (n >= cap - w) return false;
        for (size_t i = 0; i < n; ++i) out[w++] = p[i];
        return true;
    };
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(src); *p;) {
        const unsigned char c = *p;
        if (c == '"' || c == '\\') {
            const char pair[2] = {'\\', static_cast<char>(c)};
            if (!emit(pair, 2)) return false;
            ++p;
        } else if (c < 0x20) {
            const char escaped[6] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]};
            if (!emit(escaped, 6)) return false;
            ++p;
        } else if (c < 0x80) {
            if (!emit(reinterpret_cast<const char*>(p), 1)) return false;
            ++p;
        } else {
            size_t n = c >= 0xc2 && c <= 0xdf ? 2 :
                       c >= 0xe0 && c <= 0xef ? 3 :
                       c >= 0xf0 && c <= 0xf4 ? 4 : 0;
            bool valid = n != 0;
            for (size_t i = 1; valid && i < n; ++i)
                valid = p[i] && (p[i] & 0xc0) == 0x80;
            if (valid && n == 3) valid = !(c == 0xe0 && p[1] < 0xa0) &&
                                         !(c == 0xed && p[1] >= 0xa0);
            if (valid && n == 4) valid = !(c == 0xf0 && p[1] < 0x90) &&
                                         !(c == 0xf4 && p[1] >= 0x90);
            if (valid) {
                if (!emit(reinterpret_cast<const char*>(p), n)) return false;
                p += n;
            } else {
                if (!emit("\\uFFFD", 6)) return false;
                ++p;
            }
        }
    }
    out[w] = '\0';
    if (written) *written = w;
    return true;
}

// The other way: a JSON string's escapes turned back into UTF-8, in place. jsmn hands out a
// string's bytes as they stand in the body, escapes and all, so a value with " or \ reached the
// handlers with its backslashes still in it. The result is never longer than the input.
// False for a malformed escape, a lone surrogate, or \u0000 (it would cut the value short).
inline bool json_unescape(char* s, size_t* written = nullptr) {
    if (!s) return false;
    auto hex4 = [](const char* p, uint32_t& v) {
        v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = p[i];
            const uint32_t d = c >= '0' && c <= '9' ? uint32_t(c - '0') :
                               c >= 'a' && c <= 'f' ? uint32_t(c - 'a' + 10) :
                               c >= 'A' && c <= 'F' ? uint32_t(c - 'A' + 10) : 16u;
            if (d > 15) return false;
            v = (v << 4) | d;
        }
        return true;
    };
    char* w = s;
    for (const char* p = s; *p;) {
        if (*p != '\\') { *w++ = *p++; continue; }
        const char e = p[1];
        p += 2;
        switch (e) {
        case '"':  *w++ = '"';  break;
        case '\\': *w++ = '\\'; break;
        case '/':  *w++ = '/';  break;
        case 'b':  *w++ = '\b'; break;
        case 'f':  *w++ = '\f'; break;
        case 'n':  *w++ = '\n'; break;
        case 'r':  *w++ = '\r'; break;
        case 't':  *w++ = '\t'; break;
        case 'u': {
            uint32_t cp;
            if (!hex4(p, cp)) return false;
            p += 4;
            if (cp >= 0xd800 && cp <= 0xdbff) {          // a pair: the low half must follow
                uint32_t lo;
                if (p[0] != '\\' || p[1] != 'u' || !hex4(p + 2, lo) || lo < 0xdc00 || lo > 0xdfff)
                    return false;
                p += 6;
                cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
            } else if ((cp >= 0xdc00 && cp <= 0xdfff) || cp == 0) {
                return false;
            }
            if (cp < 0x80) {
                *w++ = char(cp);
            } else if (cp < 0x800) {
                *w++ = char(0xc0 | (cp >> 6));
                *w++ = char(0x80 | (cp & 0x3f));
            } else if (cp < 0x10000) {
                *w++ = char(0xe0 | (cp >> 12));
                *w++ = char(0x80 | ((cp >> 6) & 0x3f));
                *w++ = char(0x80 | (cp & 0x3f));
            } else {
                *w++ = char(0xf0 | (cp >> 18));
                *w++ = char(0x80 | ((cp >> 12) & 0x3f));
                *w++ = char(0x80 | ((cp >> 6) & 0x3f));
                *w++ = char(0x80 | (cp & 0x3f));
            }
            break;
        }
        default:
            return false;                                // includes a backslash at the very end
        }
    }
    *w = '\0';
    if (written) *written = size_t(w - s);
    return true;
}

} // namespace modesp::net
