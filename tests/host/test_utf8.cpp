/**
 * @file test_utf8.cpp
 * @brief modesp/utf8.h: where a UTF-8 string may be cut.
 *
 * The defect this exists for: a Bluetooth track name in Cyrillic, cut by a byte count to fit
 * a 64-byte state value, reached the web as "…Слаане�" — the first byte of "ц" without the
 * second, which the JSON writer turns into U+FFFD. Every case below is a cut that lands
 * inside or right after a character of 1, 2, 3 or 4 bytes.
 */

#include "doctest.h"

#include "modesp/utf8.h"

#include <cstring>
#include <string>

using modesp::utf8_copy;
using modesp::utf8_fit;

namespace {

size_t fit(const std::string& s, size_t max) { return utf8_fit(s.data(), s.size(), max); }

}  // namespace

TEST_CASE("utf8_fit: ASCII is cut exactly where asked") {
    const std::string s = "abcdef";
    CHECK(fit(s, 0) == 0);
    CHECK(fit(s, 3) == 3);
    CHECK(fit(s, 6) == 6);
    CHECK(fit(s, 64) == 6);       // shorter than the room: all of it
    CHECK(fit("", 64) == 0);
}

TEST_CASE("utf8_fit: a two-byte letter is kept whole or not at all") {
    const std::string s = "Слаанець";   // eight Cyrillic letters, two bytes each
    REQUIRE(s.size() == 16);
    CHECK(fit(s, 16) == 16);
    CHECK(fit(s, 15) == 14);      // would end on the first byte of "ь"
    CHECK(fit(s, 13) == 12);      // ... of "ц": the "…Слаане�" case
    CHECK(fit(s, 12) == 12);
    CHECK(fit(s, 1) == 0);
}

TEST_CASE("utf8_fit: three- and four-byte characters") {
    const std::string euro = "a€";      // 61 E2 82 AC
    REQUIRE(euro.size() == 4);
    CHECK(fit(euro, 2) == 1);
    CHECK(fit(euro, 3) == 1);
    CHECK(fit(euro, 4) == 4);

    const std::string note = "a🎵";     // 61 F0 9F 8E B5
    REQUIRE(note.size() == 5);
    CHECK(fit(note, 2) == 1);
    CHECK(fit(note, 3) == 1);
    CHECK(fit(note, 4) == 1);
    CHECK(fit(note, 5) == 5);
}

TEST_CASE("utf8_fit: text a source already cut mid-letter loses the half") {
    // What the BT module handed the store: the phone's title cut on a byte count before it
    // got there. The room is plenty; the last letter is still only half there.
    const std::string whole = "Слаанець";
    const std::string cut = whole.substr(0, 13);   // "Слаане" + the first byte of "ц"
    CHECK(fit(cut, 64) == 12);
    CHECK(cut.substr(0, fit(cut, 64)) == "Слаане");

    const std::string euro = std::string("a€").substr(0, 3);   // two bytes of three
    CHECK(fit(euro, 64) == 1);
}

TEST_CASE("utf8_fit: bytes that are not UTF-8 are not its business") {
    // A run of continuation bytes has no start to find within four steps: left alone.
    const std::string cont(6, '\x80');
    CHECK(fit(cont, 64) == 6);
    // 0xF8..0xFF do not start a character of any length.
    CHECK(fit(std::string("ab\xFF"), 64) == 3);
    // A stray continuation byte after ASCII is not a cut character either.
    CHECK(fit(std::string("ab\x80"), 64) == 3);
    // The one look-alike: a Latin-1 "é" (0xE9) as the very last byte reads as the lead of
    // a three-byte character with nothing after it, and goes. The JSON writer would have
    // sent it as U+FFFD; nothing on this board writes Latin-1 on purpose.
    CHECK(fit(std::string("Caf\xE9"), 64) == 3);
}

TEST_CASE("utf8_copy: NUL-terminated, whole letters, bounded by the buffer") {
    const std::string s = "Слаанець";
    char out[14];
    std::memset(out, 'x', sizeof out);
    CHECK(utf8_copy(out, sizeof out, s.data(), s.size()) == 12);   // 13 bytes of room
    CHECK(std::string(out) == "Слаане");

    char big[64];
    CHECK(utf8_copy(big, sizeof big, s.data(), s.size()) == 16);
    CHECK(std::string(big) == s);

    char one[1] = {'x'};
    CHECK(utf8_copy(one, sizeof one, s.data(), s.size()) == 0);
    CHECK(one[0] == '\0');

    CHECK(utf8_copy(big, sizeof big, nullptr, 5) == 0);
    CHECK(big[0] == '\0');
    CHECK(utf8_copy(nullptr, 8, s.data(), s.size()) == 0);
}
