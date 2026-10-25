/**
 * @file test_dlna_browser_json.cpp
 * @brief GET /api/dlna/browser's body (components/dlna_client/include/browser_json.h) — what the
 *        web's DLNA page is shown of the browser the panel drives.
 */

#include "doctest.h"
#include "../../components/dlna_client/include/browser_json.h"

#define JSMN_STATIC
#include "jsmn.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using dlna_client::View;
using dlna_client::browser_json;

namespace {

View folder_view() {
    View v;
    v.revision = 4097;
    v.busy = false;
    v.servers = false;
    v.can_back = true;
    v.has_next = true;
    v.has_prev = false;
    v.count = 2;
    snprintf(v.title, sizeof(v.title), "Музика");
    snprintf(v.status, sizeof(v.status), "На сторінці: 2");
    snprintf(v.rows[0].title, sizeof(v.rows[0].title), "Альбоми");
    v.rows[0].folder = true;
    snprintf(v.rows[1].title, sizeof(v.rows[1].title), "Say \"Hi\"\\");
    snprintf(v.rows[1].subtitle, sizeof(v.rows[1].subtitle), "Tab\there");
    v.rows[1].playable = true;
    return v;
}

std::string body_of(const View& v) {
    std::vector<char> out(dlna_client::BROWSER_JSON_CAP);
    const size_t n = browser_json(v, out.data(), out.size());
    REQUIRE(n > 0);
    REQUIRE(n == strlen(out.data()));
    return std::string(out.data(), n);
}

/// Parses `json` and returns how many tokens it has; 0 when it is not JSON.
int tokens_in(const std::string& json) {
    jsmn_parser p;
    jsmn_init(&p);
    static jsmntok_t tok[512];
    const int r = jsmn_parse(&p, json.c_str(), json.size(), tok, 512);
    return r > 0 && tok[0].type == JSMN_OBJECT ? r : 0;
}

}  // namespace

TEST_CASE("browser body: every field the web reads, can_back among them") {
    const std::string j = body_of(folder_view());
    CHECK(tokens_in(j) > 0);
    CHECK(j.rfind("{\"revision\":4097,\"busy\":false,\"servers\":false,\"can_back\":true,"
                  "\"count\":2,\"next\":true,\"previous\":false,\"title\":\"Музика\",", 0) == 0);
    CHECK(j.find("\"status\":\"На сторінці: 2\"") != std::string::npos);
    CHECK(j.find("{\"title\":\"Альбоми\",\"artist\":\"\",\"folder\":true,\"playable\":false}")
          != std::string::npos);
}

TEST_CASE("browser body: quotes, backslashes and control bytes are escaped") {
    const std::string j = body_of(folder_view());
    CHECK(j.find("\"title\":\"Say \\\"Hi\\\"\\\\\"") != std::string::npos);
    CHECK(j.find("\"artist\":\"Tab\\u0009here\"") != std::string::npos);
    CHECK(tokens_in(j) > 0);
}

TEST_CASE("browser body: the servers' view cannot go up") {
    View v;
    v.revision = 7;
    v.servers = true;
    v.can_back = false;
    v.count = 1;
    snprintf(v.title, sizeof(v.title), "Медіасервери");
    snprintf(v.rows[0].title, sizeof(v.rows[0].title), "NAS");
    v.rows[0].folder = true;
    const std::string j = body_of(v);
    CHECK(j.find("\"servers\":true,\"can_back\":false,\"count\":1") != std::string::npos);
    CHECK(tokens_in(j) > 0);
}

TEST_CASE("browser body: a count past the page is clamped, not read past the rows") {
    View v;
    v.count = 200;
    const std::string j = body_of(v);
    CHECK(j.find("\"count\":12") != std::string::npos);
    size_t rows = 0;
    for (size_t at = j.find("{\"title\""); at != std::string::npos; at = j.find("{\"title\"", at + 1)) ++rows;
    CHECK(rows == (size_t)dlna_client::PAGE_SIZE);
}

TEST_CASE("browser body: the worst snapshot still fits the buffer the handler allocates") {
    View v;
    v.count = dlna_client::PAGE_SIZE;
    v.revision = 65535;
    // Every byte a control byte: each one becomes six. No real title is like this; it is the
    // bound the buffer was sized against.
    memset(v.title, 1, sizeof(v.title) - 1);
    memset(v.status, 1, sizeof(v.status) - 1);
    for (auto& r : v.rows) {
        memset(r.title, 1, sizeof(r.title) - 1);
        memset(r.subtitle, 1, sizeof(r.subtitle) - 1);
    }
    const std::string j = body_of(v);
    CHECK(j.size() < dlna_client::BROWSER_JSON_CAP);
    CHECK(tokens_in(j) > 0);
}

TEST_CASE("browser body: a buffer too small gives nothing rather than a cut document") {
    char small[64];
    memset(small, 'x', sizeof(small));
    CHECK(browser_json(folder_view(), small, sizeof(small)) == 0);
    CHECK(small[0] == '\0');
    CHECK(browser_json(folder_view(), nullptr, 100) == 0);
}

TEST_CASE("browser body: an unterminated string field is read only to its end") {
    View v;
    v.count = 1;
    memset(v.rows[0].title, 'a', sizeof(v.rows[0].title));   // no NUL inside the field
    const std::string j = body_of(v);
    CHECK(j.find(std::string(sizeof(v.rows[0].title), 'a') + "\"") != std::string::npos);
    CHECK(tokens_in(j) > 0);
}
