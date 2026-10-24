#include "doctest.h"
#include "modesp/net/json_escape.h"

#include <cstring>
#include <string>

TEST_CASE("state JSON escape keeps the whole long UTF-8 value after quotes") {
    std::string title(128, '"');
    title += "AAAAA\xd0\xaf";
    char escaped[modesp::net::json_escaped_capacity(256)];
    size_t used = 0;
    REQUIRE(modesp::net::json_escape(escaped, sizeof(escaped), title.c_str(), &used));
    CHECK(used == 263);
    CHECK(std::string(escaped).substr(used - 7) == "AAAAA\xd0\xaf");
}

TEST_CASE("state JSON escape preserves emoji and encodes every control byte") {
    const char value[] = "\xf0\x9f\x8e\xb5\x01\n\t\r\\\"";
    char escaped[modesp::net::json_escaped_capacity(sizeof(value) - 1)];
    REQUIRE(modesp::net::json_escape(escaped, sizeof(escaped), value));
    CHECK(std::string(escaped) == "\xf0\x9f\x8e\xb5\\u0001\\u000a\\u0009\\u000d\\\\\\\"");
}

TEST_CASE("state JSON escape replaces an incomplete UTF-8 sequence") {
    const char value[] = {'A', char(0xd0), 0};
    char escaped[modesp::net::json_escaped_capacity(sizeof(value) - 1)];
    REQUIRE(modesp::net::json_escape(escaped, sizeof(escaped), value));
    CHECK(std::strcmp(escaped, "A\\uFFFD") == 0);
}

TEST_CASE("JSON unescape gives back what escape wrote, quotes and backslashes included") {
    const char value[] = "My \"home\" net\\5G \xd0\xaf \xf0\x9f\x8e\xb5\x01";
    char escaped[modesp::net::json_escaped_capacity(sizeof(value) - 1)];
    REQUIRE(modesp::net::json_escape(escaped, sizeof(escaped), value));
    size_t n = 0;
    REQUIRE(modesp::net::json_unescape(escaped, &n));
    CHECK(std::string(escaped) == value);
    CHECK(n == sizeof(value) - 1);
}

TEST_CASE("JSON unescape reads every escape, and \\u pairs as one character") {
    char s[] = "a\\/b\\n\\t\\r\\b\\f\\u0041\\u00e9\\u042f\\ud83c\\udfb5";
    REQUIRE(modesp::net::json_unescape(s));
    CHECK(std::string(s) == "a/b\n\t\r\b\fA\xc3\xa9\xd0\xaf\xf0\x9f\x8e\xb5");
}

TEST_CASE("JSON unescape refuses what is not an escape, and leaves plain text alone") {
    char plain[] = "plain text";
    REQUIRE(modesp::net::json_unescape(plain));
    CHECK(std::string(plain) == "plain text");
    const char* bad[] = {"end\\", "\\x41", "\\u12", "\\u12zz", "\\ud83c", "\\udfb5", "\\ud83c\\u0041", "nul\\u0000"};
    for (const char* b : bad) {
        INFO(b);
        std::string copy(b);
        CHECK_FALSE(modesp::net::json_unescape(&copy[0]));
    }
}
