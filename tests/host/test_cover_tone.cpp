/**
 * @file test_cover_tone.cpp
 * @brief HOST tests for the colour of a cover tile and the Player's background made from it
 *        (components/cover, cover_tone and cover_backdrop):
 *        black bars and black grounds are not a colour, a vivid subject beats a white ground, the
 *        larger of two colours wins, an all-black picture has no colour at all; the background
 *        keeps the hue, has one lightness for every picture and is never vivid.
 */

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include "doctest.h"
#include "../../components/cover/include/cover.h"

using namespace player_core;

namespace {

constexpr int N = COVER_PX;

uint16_t c565(int r, int g, int b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

struct Tile {
    std::vector<uint16_t> px = std::vector<uint16_t>(N * N, 0);
    void fill(int x0, int y0, int x1, int y1, uint16_t c) {
        for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) px[y * N + x] = c;
    }
    uint32_t tone() const { return cover_tone(px.data(), N, N); }
};

int r_of(uint32_t v) { return (int)(v >> 16 & 0xFF); }
int g_of(uint32_t v) { return (int)(v >> 8 & 0xFF); }
int b_of(uint32_t v) { return (int)(v & 0xFF); }

/// Within a step of RGB565 of the colour asked for.
bool near(uint32_t v, int r, int g, int b) {
    return std::abs(r_of(v) - r) <= 8 && std::abs(g_of(v) - g) <= 4 && std::abs(b_of(v) - b) <= 8;
}

}  // namespace

TEST_CASE("cover tone: the letterbox's black bars are not the colour") {
    Tile t;
    t.fill(0, 60, N, 220, c565(200, 30, 40));          // a wide picture, black above and below
    CHECK(near(t.tone(), 200, 30, 40));
}

TEST_CASE("cover tone: a coloured subject on a white ground is the colour") {
    Tile t;
    t.fill(0, 0, N, N, c565(245, 245, 245));
    t.fill(60, 60, 213, 213, c565(30, 60, 200));       // 30 % of the tile
    CHECK(near(t.tone(), 30, 60, 200));
}

TEST_CASE("cover tone: of two colours, the larger") {
    Tile t;
    t.fill(0, 0, N, N, c565(230, 140, 30));
    t.fill(0, 168, N, N, c565(20, 160, 150));          // 40 % teal under 60 % orange
    CHECK(near(t.tone(), 230, 140, 30));
}

TEST_CASE("cover tone: a grey picture is grey, an all-black one has none, no picture has none") {
    Tile grey;
    grey.fill(0, 0, N, N, c565(120, 120, 120));
    CHECK(near(grey.tone(), 120, 120, 120));
    Tile black;
    CHECK(black.tone() == 0);
    CHECK(cover_tone(nullptr, N, N) == 0);
    CHECK(cover_tone(grey.px.data(), 0, N) == 0);
    // A colour is never 0, which means none: the darkest that counts is still a colour.
    Tile dim;
    dim.fill(0, 0, N, N, c565(40, 0, 0));
    CHECK(dim.tone() != 0);
}

namespace {

/// HSL lightness and saturation of an RGB888 colour, 0..1.
void lightness_sat(uint32_t v, double& l, double& s) {
    const double r = r_of(v) / 255.0, g = g_of(v) / 255.0, b = b_of(v) / 255.0;
    const double mx = std::fmax(r, std::fmax(g, b)), mn = std::fmin(r, std::fmin(g, b));
    l = (mx + mn) / 2.0;
    s = mx == mn ? 0.0 : (mx - mn) / (1.0 - std::fabs(2.0 * l - 1.0));
}

}  // namespace

TEST_CASE("cover backdrop: the hue stays, the lightness is one for all, never vivid") {
    CHECK(cover_backdrop(0) == 0);                     // no picture, no tint
    // the colours the tone tests find, a pure primary each way, white and grey
    const uint32_t colours[] = { 0xC81E28, 0x1E3CC8, 0xE68C1E, 0x14A096, 0xFF0000, 0x00FF00,
                                 0x0000FF, 0xFFFF00, 0xF5F5F5, 0x787878, 0x400000 };
    for (uint32_t c : colours) {
        CAPTURE(c);
        const uint32_t v = cover_backdrop(c);
        REQUIRE(v != 0);
        double l, s;
        lightness_sat(v, l, s);
        CHECK(std::fabs(l - 0.20) < 0.01);
        CHECK(s < 0.46);
    }
    // the hue: the channel that leads the picture's colour leads its background
    const uint32_t red = cover_backdrop(0xC81E28), blue = cover_backdrop(0x1E3CC8);
    CHECK(r_of(red) > g_of(red) + 40);
    CHECK(r_of(red) > b_of(red) + 40);
    CHECK(b_of(blue) > r_of(blue) + 40);
    CHECK(b_of(blue) > g_of(blue) + 30);
    // a grey picture gives a grey background; a muted colour keeps its own saturation
    const uint32_t grey = cover_backdrop(0x787878);
    CHECK(r_of(grey) == g_of(grey));
    CHECK(g_of(grey) == b_of(grey));
    double l, s;
    lightness_sat(cover_backdrop(0x8C7864), l, s);     // a brown, saturation 0.17
    CHECK(std::fabs(s - 0.17) < 0.03);
}
