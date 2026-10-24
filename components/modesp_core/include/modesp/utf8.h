/**
 * @file utf8.h
 * @brief Where a UTF-8 string may be cut.
 *
 * Every fixed buffer on this board is counted in bytes, and a byte count lands in the middle
 * of a character as soon as the text is not ASCII: a Cyrillic letter is two bytes, a euro
 * sign three, an emoji four. Cut there, the string ends in a lead byte with nothing after it.
 * A JSON writer has to replace that with U+FFFD — the "�" at the end of a track name — and a
 * label renderer draws whatever it makes of it.
 *
 * Header-only and free of IDF, so the host tests and any module can use the same rule.
 */
#pragma once

#include <cstddef>

namespace modesp {

/// The length of the longest prefix of `s[0, len)` that is at most `max` bytes and does not
/// end inside a character.
///
/// A string that already ends in part of a character — a source upstream cut it on a byte
/// count before it got here — loses that part the same way, even when it is shorter than
/// `max`. Nothing else changes: this is not a validator, and a byte that is not UTF-8 at all
/// is left where it is. Only the last character is looked at, so the cost is at most four
/// bytes whatever the length.
inline size_t utf8_fit(const char* s, size_t len, size_t max) {
    const size_t n = len < max ? len : max;
    // Walk back over the continuation bytes (10xxxxxx) to the byte that starts the last
    // character, then see whether all of that character is inside the prefix. A character
    // has at most three continuation bytes, so four steps settle it either way.
    size_t lead = n;
    for (int step = 0; step < 4 && lead > 0; ++step) {
        const unsigned char c = static_cast<unsigned char>(s[--lead]);
        if ((c & 0xC0) == 0x80) continue;
        const size_t want = c < 0x80          ? 1
                          : (c & 0xE0) == 0xC0 ? 2
                          : (c & 0xF0) == 0xE0 ? 3
                          : (c & 0xF8) == 0xF0 ? 4
                          : 0;                  // 0xF8..0xFF start nothing: not ours to judge
        return (want != 0 && lead + want > n) ? lead : n;
    }
    return n;   // empty, or four continuation bytes in a row: nothing to decide
}

/// Copy `src` into `dst` (`cap` bytes, always NUL-terminated when `cap` > 0), cut by
/// utf8_fit(). Returns the number of bytes copied, not counting the NUL.
inline size_t utf8_copy(char* dst, size_t cap, const char* src, size_t len) {
    if (!dst || cap == 0) return 0;
    const size_t n = src ? utf8_fit(src, len, cap - 1) : 0;
    for (size_t i = 0; i < n; ++i) dst[i] = src[i];
    dst[n] = '\0';
    return n;
}

}  // namespace modesp
