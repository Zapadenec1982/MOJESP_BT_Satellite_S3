/**
 * @file cover_tone.cpp
 * @brief The colour of a cover tile, for the Player's background.
 *        See cover_tone() in cover.h.
 *
 * Pure: no ESP-IDF, so the host tests run it. The tile is RGB565 and every fourth pixel each way
 * is read — 4 900 of a 280-pixel tile, a colour's worth, run once when a picture is published.
 *
 * A coarse histogram (two bits a channel, 64 cells) finds where the picture's colour is, and the
 * pixels of the winning cell are averaged, so the answer is a colour that is in the picture
 * rather than the muddy mean of all of them. Black is skipped: the hardware path letterboxes a
 * cover that is not square with black bars, and a black ground says nothing about the colour.
 * A vivid pixel counts for more than a grey one, so a coloured subject on a white ground wins.
 *
 * cover_backdrop() then turns that colour into the Player's background: its hue, at one
 * lightness for every picture, with the saturation capped — a red cover gives a deep red, not a
 * red, and the title stays as readable on it as on the plain surface.
 */

#include "cover.h"
#include <cmath>

namespace player_core {
namespace {

constexpr int STEP = 4;          ///< every 4th pixel, each way
constexpr int DARK = 32;         ///< a pixel whose brightest channel is under this is "black"
constexpr float BACK_L = 0.20f;   ///< the backdrop's lightness (HSL), the design's 21 %
constexpr float BACK_S = 0.45f;   ///< the most saturation it may have

inline void rgb888(uint16_t p, int& r, int& g, int& b) {
    r = (p >> 11) & 0x1F;
    g = (p >> 5) & 0x3F;
    b = p & 0x1F;
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
}

inline int cell(int r, int g, int b) { return (r >> 6) << 4 | (g >> 6) << 2 | (b >> 6); }

inline int max3(int a, int b, int c) { return a > b ? (a > c ? a : c) : (b > c ? b : c); }
inline int min3(int a, int b, int c) { return a < b ? (a < c ? a : c) : (b < c ? b : c); }

}  // namespace

uint32_t cover_tone(const uint16_t* px, int w, int h) {
    if (!px || w <= 0 || h <= 0) return 0;
    uint32_t weight[64] = {};
    int counted = 0;
    for (int y = STEP / 2; y < h; y += STEP) {
        for (int x = STEP / 2; x < w; x += STEP) {
            int r, g, b;
            rgb888(px[y * w + x], r, g, b);
            const int mx = max3(r, g, b);
            if (mx < DARK) continue;
            const int sat = (mx - min3(r, g, b)) * 255 / mx;
            weight[cell(r, g, b)] += 64 + 3 * (uint32_t)sat;
            counted++;
        }
    }
    if (!counted) return 0;
    int best = 0;
    for (int c = 1; c < 64; c++) if (weight[c] > weight[best]) best = c;
    uint32_t sr = 0, sg = 0, sb = 0, n = 0;
    for (int y = STEP / 2; y < h; y += STEP) {
        for (int x = STEP / 2; x < w; x += STEP) {
            int r, g, b;
            rgb888(px[y * w + x], r, g, b);
            if (max3(r, g, b) < DARK || cell(r, g, b) != best) continue;
            sr += (uint32_t)r;
            sg += (uint32_t)g;
            sb += (uint32_t)b;
            n++;
        }
    }
    const uint32_t v = (sr / n) << 16 | (sg / n) << 8 | (sb / n);
    return v ? v : 1;                      // 0 is "no colour"
}

uint32_t cover_backdrop(uint32_t rgb) {
    if (!rgb) return 0;
    const float r = (float)(rgb >> 16 & 0xFF) / 255.0f;
    const float g = (float)(rgb >> 8 & 0xFF) / 255.0f;
    const float b = (float)(rgb & 0xFF) / 255.0f;
    const float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    const float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    const float d = mx - mn;
    // hue in sixths of the circle, and the HSL saturation; a grey has neither
    float h = 0.0f, s = 0.0f;
    if (d > 0.0f) {
        s = d / (1.0f - std::fabs(mx + mn - 1.0f));
        if (mx == r)      h = std::fmod((g - b) / d + 6.0f, 6.0f);
        else if (mx == g) h = (b - r) / d + 2.0f;
        else              h = (r - g) / d + 4.0f;
    }
    if (s > BACK_S) s = BACK_S;
    const float c = (1.0f - std::fabs(2.0f * BACK_L - 1.0f)) * s;
    const float x = c * (1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f));
    const float m = BACK_L - c / 2.0f;
    float o[3];
    switch ((int)h % 6) {
    case 0:  o[0] = c; o[1] = x; o[2] = 0; break;
    case 1:  o[0] = x; o[1] = c; o[2] = 0; break;
    case 2:  o[0] = 0; o[1] = c; o[2] = x; break;
    case 3:  o[0] = 0; o[1] = x; o[2] = c; break;
    case 4:  o[0] = x; o[1] = 0; o[2] = c; break;
    default: o[0] = c; o[1] = 0; o[2] = x; break;
    }
    uint32_t v = 0;
    for (float ch : o) v = v << 8 | (uint32_t)std::lround((ch + m) * 255.0f);
    return v;
}

}  // namespace player_core
