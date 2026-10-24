/**
 * @file ppa_geometry.h
 * @brief Which rotated blocks hang the PPA on IDF 5.5.5 (ESP32-P4 v1.3, DIG-734). No IDF, no LVGL.
 *
 * The SRM engine cuts a block into transfer units; a last unit smaller than the DMA2D FIFO (12 x
 * 128 bits) is not counted unless the macro-block order is bypassed, and the transfer then never
 * raises its done interrupt. IDF 5.5.5's ppa_srm.c decides the bypass from the block as given;
 * for 90/270 degrees the hardware cuts it rotated, so some blocks get no bypass they need
 * (esp-idf#19023, fixed on master by 469aa16). Pinned on the host by
 * tests/host/test_ppa_geometry.cpp; used by src/ppa_flush.cpp.
 */

#pragma once

#include <cstdint>

namespace ppa_geom {

/// DIG-734 as 469aa16 computes it, for RGB888 out at scale 1 with 16-row macro blocks (the
/// macro-block size of every revision below 3.0): would units of `bw` x `bh` end in one smaller
/// than the DMA2D FIFO?
inline bool leftover_under_fifo(uint32_t bw, uint32_t bh) {
    uint32_t wl = bw % 32; if (wl == 0) wl = 32;
    uint32_t hl = bh % 16; if (hl == 0) hl = 16;
    return (bw > 32 || bh > 16) && wl * hl * 24 < 12 * 128;
}

/// True when IDF 5.5.5 would leave a `w` x `h` block (as LVGL hands it over) without the bypass
/// it needs. `quarter`: rotated by 90 or 270 degrees.
inline bool would_hang(uint32_t w, uint32_t h, bool quarter) {
    return quarter && leftover_under_fifo(h, w) && !leftover_under_fifo(w, h);
}

/// The tallest block LVGL may flush here (a full-screen rotated panel is 480 rows high).
constexpr uint32_t MAX_ROWS = 480;

/// Split the rows of a `w`-wide block rotated by 90/270 degrees into stripes, none of which
/// hangs, tallest stripe first where there is a choice. Writes the stripe heights to `rows` and
/// returns how many; 1 with `rows[0] == h` when the block needs no split; 0 when no split works
/// (narrow blocks: the caller rotates those on the CPU) or it would need more than `max`.
inline int plan_stripes(uint32_t w, uint32_t h, uint16_t* rows, int max) {
    if (h == 0 || max < 1) return 0;
    if (!would_hang(w, h, true)) { rows[0] = (uint16_t)h; return 1; }
    if (h > MAX_ROWS) return 0;
    // last[r]: the height of the last stripe of a safe split of the first r rows, 0 if none.
    uint16_t last[MAX_ROWS + 1] = {};
    last[0] = 0xFFFF;                        // the empty split
    for (uint32_t r = 1; r <= h; r++) {
        for (uint32_t s = r; s >= 1; s--) {
            if (last[r - s] != 0 && !would_hang(w, s, true)) { last[r] = (uint16_t)s; break; }
        }
    }
    if (last[h] == 0) return 0;
    int n = 0;
    for (uint32_t r = h; r > 0; r -= last[r]) n++;
    if (n > max) return 0;
    int i = n;
    for (uint32_t r = h; r > 0; r -= last[r]) rows[--i] = last[r];
    return n;
}

} // namespace ppa_geom
