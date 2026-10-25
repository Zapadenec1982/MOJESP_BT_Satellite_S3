/**
 * @file test_web_media.cpp
 * @brief GET /api/library and GET /api/cover bodies (components/web_media/include/web_media.h), and
 *        the cover rule the panel and the web share (components/cover/include/cover.h).
 */

#include "doctest.h"
#include "../../components/web_media/include/web_media.h"
#include "../../components/cover/include/cover.h"

#define JSMN_STATIC
#include "jsmn.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace web_media;
using player_core::CoverSource;
using player_core::CoverView;

namespace {

bool is_json(const std::string& j) {
    jsmn_parser p;
    jsmn_init(&p);
    static jsmntok_t tok[1024];
    const int r = jsmn_parse(&p, j.c_str(), j.size(), tok, 1024);
    return r > 0 && tok[0].type == JSMN_OBJECT;
}

/// A card of `n` entries named like an album's tracks.
struct Card {
    std::vector<std::string> rel;
    explicit Card(int n) {
        for (int i = 0; i < n; ++i) {
            char b[64];
            snprintf(b, sizeof(b), "Гурт/Альбом/%02d - Пісня.flac", i + 1);
            rel.push_back(b);
        }
    }
    bool operator()(int i, char* out, size_t cap) const {
        if (i < 0 || i >= (int)rel.size()) return false;
        snprintf(out, cap, "%s", rel[i].c_str());
        return true;
    }
};

std::string page_of(const Card& c, Page p, int* rows, size_t cap = LIB_JSON_CAP, bool busy = false,
                    const char* root = "/sdcard") {
    std::vector<char> out(cap);
    const size_t n = library_page(out.data(), cap, 42, busy, (int)c.rel.size(), 3, root, p, c, rows);
    REQUIRE(n > 0);
    REQUIRE(n == strlen(out.data()));
    return std::string(out.data(), n);
}

/// RGB565 from eight-bit channels, as the cover tile holds them.
uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

}  // namespace

// ── the library ──

TEST_CASE("library query: absent, out of range and huge values are clamped") {
    CHECK(clamp_page(-1, -1).from == 0);
    CHECK(clamp_page(-1, -1).n == LIB_N_DEFAULT);
    CHECK(clamp_page(250, 0).n == 1);
    CHECK(clamp_page(250, 0).from == 250);
    CHECK(clamp_page(0, 5000).n == LIB_N_MAX);
    CHECK(clamp_page(99999999, 10).from == 1000000);
}

TEST_CASE("library page: the frame and the rows the web reads") {
    Card c(5);
    int rows = -1;
    const std::string j = page_of(c, clamp_page(1, 2), &rows);
    CHECK(rows == 2);
    CHECK(is_json(j));
    CHECK(j == "{\"gen\":42,\"busy\":false,\"count\":5,\"cursor\":3,\"root\":\"/sdcard\",\"from\":1,"
               "\"rows\":[\"Гурт/Альбом/02 - Пісня.flac\",\"Гурт/Альбом/03 - Пісня.flac\"]}");
}

TEST_CASE("library page: stops at the end of the card") {
    Card c(5);
    int rows = -1;
    std::string j = page_of(c, clamp_page(3, 200), &rows);
    CHECK(rows == 2);
    j = page_of(c, clamp_page(9, 10), &rows);
    CHECK(rows == 0);
    CHECK(j.find("\"rows\":[]}") != std::string::npos);
    CHECK(is_json(j));
}

TEST_CASE("library page: a busy index has no rows and no count") {
    Card c(5);
    int rows = -1;
    const std::string j = page_of(c, clamp_page(0, 10), &rows, LIB_JSON_CAP, true);
    CHECK(rows == 0);
    CHECK(j.find("\"busy\":true,\"count\":0,") != std::string::npos);
    CHECK(is_json(j));
}

TEST_CASE("library page: a full buffer ends the page early, never mid-row") {
    Card c(50);
    int rows = -1;
    const std::string j = page_of(c, clamp_page(0, 50), &rows, 400);
    CHECK(rows > 0);
    CHECK(rows < 50);
    CHECK(j.size() < 400);
    CHECK(is_json(j));
    CHECK(j.substr(j.size() - 3) == "\"]}");
}

TEST_CASE("library page: the root and the paths are escaped; an unreadable entry ends the page") {
    struct Odd {
        bool operator()(int i, char* out, size_t cap) const {
            if (i == 2) return false;
            snprintf(out, cap, "%s", i ? "a\\b" : "say \"hi\".mp3");
            return true;
        }
    } odd;
    std::vector<char> out(LIB_JSON_CAP);
    int rows = -1;
    const size_t n = library_page(out.data(), out.size(), 2, false, 4, 0, "/sd\"card", clamp_page(0, 4), odd, &rows);
    const std::string j(out.data(), n);
    CHECK(rows == 2);
    CHECK(j.find("\"root\":\"/sd\\\"card\"") != std::string::npos);
    CHECK(j.find("[\"say \\\"hi\\\".mp3\",\"a\\\\b\"]") != std::string::npos);
    CHECK(is_json(j));
}

TEST_CASE("library page: the widest entries fit a page of the largest size") {
    struct Wide {
        bool operator()(int, char* out, size_t cap) const {
            memset(out, 'x', cap - 1);
            out[cap - 1] = '\0';
            return true;
        }
    } wide;
    std::vector<char> out(LIB_JSON_CAP);
    int rows = -1;
    const size_t n = library_page(out.data(), out.size(), 0, false, 2048, 0, "/sdcard",
                                  clamp_page(0, LIB_N_MAX), wide, &rows);
    CHECK(rows == LIB_N_MAX);
    CHECK(n < LIB_JSON_CAP);
}

TEST_CASE("library page: a buffer too small for the frame gives nothing") {
    Card c(3);
    char small[32];
    int rows = -1;
    CHECK(library_page(small, sizeof(small), 1, false, 3, 0, "/sdcard", clamp_page(0, 3), c, &rows) == 0);
    CHECK(rows == 0);
}

// ── the cover ──

TEST_CASE("cover: the Player's tile per source — the rule screen_now.cpp draws by") {
    CoverSource t;
    CHECK(player_core::cover_tile_for(3, false, t));
    CHECK(t == CoverSource::Bluetooth);
    CHECK(player_core::cover_tile_for(4, false, t));
    CHECK(t == CoverSource::File);
    CHECK(player_core::cover_tile_for(5, false, t));
    CHECK(t == CoverSource::Radio);
    CHECK(player_core::cover_tile_for(5, true, t));
    CHECK(t == CoverSource::Network);
    CHECK(player_core::cover_tile_for(6, false, t));
    CHECK(t == CoverSource::AirPlay);
    for (int src : {-1, 0, 1, 2, 7}) {
        CHECK_FALSE(player_core::cover_tile_for(src, true, t));
        CHECK(t == CoverSource::File);
    }
}

TEST_CASE("cover id: 0 for none, positive and different per picture and per tile") {
    static uint16_t px[4];
    CoverView none;
    none.gen = 9;
    CHECK(player_core::cover_id(none, CoverSource::File) == 0);
    CoverView v;
    v.px = px;
    v.gen = 1;
    const int32_t a = player_core::cover_id(v, CoverSource::File);
    const int32_t b = player_core::cover_id(v, CoverSource::Radio);
    v.gen = 2;
    const int32_t c = player_core::cover_id(v, CoverSource::File);
    CHECK(a > 0);
    CHECK(a != b);
    CHECK(a != c);
    v.gen = 0xFFFFFFFFu;
    CHECK(player_core::cover_id(v, CoverSource::Radio) > 0);
}

TEST_CASE("cover BMP: the factor for a requested size") {
    CHECK(bmp_factor(280, -1) == 2);         // the default, 140
    CHECK(bmp_factor(280, 0) == 2);
    CHECK(bmp_factor(280, 140) == 2);
    CHECK(bmp_factor(280, 100) == 2);        // never under what was asked
    CHECK(bmp_factor(280, 56) == 5);
    CHECK(bmp_factor(280, 280) == 1);
    CHECK(bmp_factor(280, 4000) == 1);
    CHECK(bmp_factor(280, 1) == COVER_FACTOR_MAX);
}

TEST_CASE("cover BMP: header of a 140-pixel picture") {
    uint8_t h[BMP_HEADER];
    bmp_header(h, 140);
    CHECK(h[0] == 'B');
    CHECK(h[1] == 'M');
    CHECK(bmp_stride(140) == 420);
    CHECK(bmp_stride(70) == 212);            // 210, padded to four
    CHECK(le32(h + 2) == 54u + 420u * 140u);
    CHECK(le32(h + 10) == 54u);
    CHECK(le32(h + 14) == 40u);
    CHECK(le32(h + 18) == 140u);
    CHECK((int32_t)le32(h + 22) == -140);    // top row first
    CHECK(le16(h + 26) == 1);
    CHECK(le16(h + 28) == 24);
    CHECK(le32(h + 34) == 420u * 140u);
}

TEST_CASE("cover BMP: full scale stays full scale, and a block is averaged") {
    // A 4×4 tile: the top-left 2×2 white, the top-right red, the bottom half half white, half black.
    const uint16_t W = rgb565(255, 255, 255), R = rgb565(255, 0, 0), K = 0;
    const uint16_t px[16] = {
        W, W, R, R,
        W, W, R, R,
        W, K, K, K,
        K, W, K, K,
    };
    std::vector<uint8_t> bmp(bmp_size(2));
    bmp_write(px, 4, 2, bmp.data());
    const uint8_t* row0 = bmp.data() + BMP_HEADER;
    const uint8_t* row1 = row0 + bmp_stride(2);
    // blue, green, red
    CHECK(row0[0] == 255); CHECK(row0[1] == 255); CHECK(row0[2] == 255);
    CHECK(row0[3] == 0);   CHECK(row0[4] == 0);   CHECK(row0[5] == 255);
    CHECK(row0[6] == 0);   CHECK(row0[7] == 0);           // the row's padding
    CHECK(row1[0] == 128); CHECK(row1[1] == 128); CHECK(row1[2] == 128);   // two of four white
    CHECK(row1[3] == 0);   CHECK(row1[4] == 0);   CHECK(row1[5] == 0);
}

TEST_CASE("cover BMP: a whole tile at the default size is the size the handler allocates") {
    std::vector<uint16_t> tile(280 * 280, rgb565(0, 128, 255));
    const int k = bmp_factor(280, -1);
    std::vector<uint8_t> bmp(bmp_size(280 / k), 0xEE);
    bmp_write(tile.data(), 280, k, bmp.data());
    CHECK(bmp.size() == 54u + 420u * 140u);
    const uint8_t* last = bmp.data() + BMP_HEADER + bmp_stride(140) * 139 + 139 * 3;
    CHECK(last[0] == 255);
    CHECK(last[1] == 130);                   // 128 is 32 of 63: widened, (32 << 2) | (32 >> 4)
    CHECK(last[2] == 0);
}
