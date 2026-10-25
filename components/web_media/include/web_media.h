/**
 * @file web_media.h
 * @brief The bodies of GET /api/library and GET /api/cover (modules/ui_lvgl/src/ui_web_media.cpp;
 *        /api/library also modules/ui_headless).
 *
 * Header-only and free of IDF and LVGL, so the host suite holds them to their shape
 * (tests/host/test_web_media.cpp). The handlers do the reading — the card's index, the cover
 * tile — and these do the writing.
 *
 * A4 (the card's library, a page at a time) and A1 (the
 * Player's picture, as a thumbnail).
 */

#pragma once

#include "modesp/json_escape.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace web_media {

// ── GET /api/library?from=&n= ──

constexpr int LIB_N_DEFAULT = 100;
constexpr int LIB_N_MAX     = 200;
/// The widest entry: Playlist::MAX_REL, which the handler static_asserts against.
constexpr size_t LIB_REL_CAP = 192;
/// One page's body, in PSRAM. Two hundred paths of 191 bytes is 39 KB with the frame; a page
/// takes what fits and the web asks for the rest from where it stopped.
constexpr size_t LIB_JSON_CAP = 48 * 1024;

struct Page {
    int from = 0;
    int n    = LIB_N_DEFAULT;
};

/// from and n as the query gave them, −1 for absent: from ≥ 0, 1 ≤ n ≤ LIB_N_MAX.
inline Page clamp_page(long from, long n) {
    Page p;
    p.from = from > 0 ? (from > 1000000 ? 1000000 : (int)from) : 0;
    p.n = n < 0 ? LIB_N_DEFAULT : n < 1 ? 1 : n > LIB_N_MAX ? LIB_N_MAX : (int)n;
    return p;
}

/// {"gen","busy","count","cursor","root","from","rows":["<path under the root>",…]}.
///
/// `rel(i, buf, cap)` reads entry i's path relative to the root — Playlist::rel_at(). Rows stop
/// at `p.n`, at `count`, at the first entry that cannot be read, or before the one that would
/// not fit; the web goes on from `from + rows.length`. A busy index (the player is rebuilding
/// it) gets no rows at all. Returns the length, or 0 when even the frame does not fit; `*rows`
/// says how many went in.
template <class RelAt>
size_t library_page(char* out, size_t cap, uint32_t gen, bool busy, int count, int cursor,
                    const char* root, Page p, RelAt rel, int* rows) {
    if (rows) *rows = 0;
    if (!out || cap < 64) return 0;
    char esc_root[LIB_REL_CAP * 6 + 1];
    modesp::json_escape(esc_root, sizeof(esc_root), root ? root : "");
    if (busy) count = 0;
    int used = snprintf(out, cap,
                        "{\"gen\":%lu,\"busy\":%s,\"count\":%d,\"cursor\":%d,\"root\":\"%s\","
                        "\"from\":%d,\"rows\":[",
                        (unsigned long)gen, busy ? "true" : "false", count < 0 ? 0 : count,
                        cursor < 0 ? 0 : cursor, esc_root, p.from);
    if (used < 0 || (size_t)used + 3 > cap) return 0;
    size_t n = (size_t)used;
    int written = 0;
    char path[LIB_REL_CAP];
    char esc[LIB_REL_CAP * 6 + 1];
    for (int i = p.from; !busy && i < count && written < p.n; ++i) {
        if (!rel(i, path, sizeof(path))) break;
        path[sizeof(path) - 1] = '\0';
        const size_t len = modesp::json_escape(esc, sizeof(esc), path);
        const size_t need = len + 2 + (written ? 1 : 0);    // quotes, and the comma before
        if (n + need + 3 > cap) break;                      // "]}" and the NUL after it
        if (written) out[n++] = ',';
        out[n++] = '"';
        memcpy(out + n, esc, len);
        n += len;
        out[n++] = '"';
        ++written;
    }
    out[n++] = ']';
    out[n++] = '}';
    out[n] = '\0';
    if (rows) *rows = written;
    return n;
}

// ── GET /api/cover?px= ──
//
// The Player's tile is RGB565 in PSRAM, 280 a side (cover.h). It goes out as a 24-bit BMP —
// the format GET /api/screenshot already uses, which every browser shows — scaled down by a
// whole factor with a box average, so a 56-pixel thumbnail is not paid for with a 235 KB body.

constexpr uint32_t BMP_HEADER = 54;
constexpr int COVER_PX_DEFAULT = 140;    ///< 2.5 times the web's 56-pixel thumbnail
constexpr int COVER_FACTOR_MAX = 10;

/// The whole factor that takes a `tile`-pixel tile nearest to `px` without going under it;
/// 1 when `px` is the tile or more, COVER_FACTOR_MAX at the least. `px` ≤ 0 is the default.
inline int bmp_factor(int tile, int px) {
    if (px <= 0) px = COVER_PX_DEFAULT;
    int k = px >= tile ? 1 : tile / px;
    if (k < 1) k = 1;
    if (k > COVER_FACTOR_MAX) k = COVER_FACTOR_MAX;
    return k;
}

inline uint32_t bmp_stride(uint32_t side) { return (side * 3u + 3u) & ~3u; }
inline uint32_t bmp_size(uint32_t side) { return BMP_HEADER + bmp_stride(side) * side; }

namespace detail {
inline void put16(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
inline void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
}  // namespace detail

/// BITMAPFILEHEADER + BITMAPINFOHEADER for a `side`×`side`, 24-bit, top-row-first picture.
inline void bmp_header(uint8_t* h, uint32_t side) {
    memset(h, 0, BMP_HEADER);
    h[0] = 'B';
    h[1] = 'M';
    detail::put32(h + 2, bmp_size(side));
    detail::put32(h + 10, BMP_HEADER);
    detail::put32(h + 14, 40);
    detail::put32(h + 18, side);
    detail::put32(h + 22, (uint32_t)(-(int32_t)side));     // negative: the top row first
    detail::put16(h + 26, 1);
    detail::put16(h + 28, 24);
    detail::put32(h + 34, bmp_stride(side) * side);
}

/// Output row `y` of the picture a `tile`×`tile` RGB565 tile makes at `factor`: each pixel the
/// average of a factor×factor block, widened to eight bits per channel by repeating the top bits
/// (as the screenshot does), in BMP's blue-green-red order, the row padded with zeros.
inline void bmp_row(const uint16_t* px, int tile, int factor, int y, uint8_t* out) {
    const int side = tile / factor;
    const uint32_t n = (uint32_t)factor * (uint32_t)factor;
    uint8_t* o = out;
    for (int x = 0; x < side; ++x, o += 3) {
        uint32_t r = 0, g = 0, b = 0;
        for (int dy = 0; dy < factor; ++dy) {
            const uint16_t* row = px + (size_t)(y * factor + dy) * (size_t)tile + (size_t)x * factor;
            for (int dx = 0; dx < factor; ++dx) {
                const uint32_t v = row[dx];
                const uint32_t r5 = v >> 11, g6 = (v >> 5) & 0x3Fu, b5 = v & 0x1Fu;
                r += (r5 << 3) | (r5 >> 2);
                g += (g6 << 2) | (g6 >> 4);
                b += (b5 << 3) | (b5 >> 2);
            }
        }
        o[0] = (uint8_t)((b + n / 2) / n);
        o[1] = (uint8_t)((g + n / 2) / n);
        o[2] = (uint8_t)((r + n / 2) / n);
    }
    memset(o, 0, bmp_stride((uint32_t)side) - (uint32_t)side * 3u);
}

/// The whole BMP into `out` (bmp_size(tile / factor) bytes).
inline void bmp_write(const uint16_t* px, int tile, int factor, uint8_t* out) {
    const uint32_t side = (uint32_t)(tile / factor);
    bmp_header(out, side);
    for (uint32_t y = 0; y < side; ++y) bmp_row(px, tile, factor, (int)y, out + BMP_HEADER + y * bmp_stride(side));
}

}  // namespace web_media
