/**
 * @file vlist_window.h
 * @brief Which entry each of VList's pooled rows shows as the list scrolls. No LVGL.
 *
 * A VList (src/ui_m3_widgets.h) shows a list of any length with a fixed pool of rows. Every
 * time the scroll crosses a row boundary the window of entries moves by one, and a row that
 * shows an entry still inside the window must keep it: filling a row costs a text layout per
 * label (LVGL measures the text again and places the ellipsis), and refilling the whole pool
 * for a window that moved by one row is what made a flick through a list stutter.
 *
 * Split out of the widget so the assignment can be pinned on the host
 * (tests/host/test_vlist_window.cpp).
 */

#pragma once

namespace vlist {

/// The most rows a pool may have. VList has 10.
constexpr int MAX_POOL = 32;

/// Give the window [first, first + pool) ∩ [0, count) to the rows.
///
/// `shown[k]` is the entry row k shows now, −1 for none. On return `want[k]` is the entry it
/// must show, −1 for none (hidden). A row that shows an entry inside the window keeps it; the
/// entries no row shows go, in order, to the rows that were free or showed an entry outside
/// the window. Returns how many rows change entry, i.e. need filling.
inline int assign(const int* shown, int pool, int first, int count, int* want) {
    if (pool > MAX_POOL) pool = MAX_POOL;
    bool covered[MAX_POOL] = {};
    for (int k = 0; k < pool; k++) {
        const int e = shown[k];
        const int slot = e - first;
        if (e >= 0 && e < count && slot >= 0 && slot < pool && !covered[slot]) {
            covered[slot] = true;           // keeps it; a second row with the same entry does not
            want[k] = e;
        } else {
            want[k] = -2;                   // gets an entry below, or is hidden
        }
    }
    int changed = 0;
    int slot = 0;
    for (int k = 0; k < pool; k++) {
        if (want[k] != -2) continue;
        while (slot < pool && (covered[slot] || first + slot < 0 || first + slot >= count)) slot++;
        if (slot < pool) {
            want[k] = first + slot;
            covered[slot] = true;
            slot++;
        } else {
            want[k] = -1;
        }
        if (want[k] != shown[k]) changed++;
    }
    return changed;
}

} // namespace vlist
