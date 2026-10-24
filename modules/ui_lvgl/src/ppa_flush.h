/**
 * @file ppa_flush.h
 * @brief The flush for a rotated DSI panel: the PPA turns each block straight into a frame
 *        buffer, LVGL draws the next block while it does, and with two frame buffers a frame is
 *        composed off the glass and a scrolling list is shifted rather than drawn (ppa_flush.cpp).
 */
#pragma once

#include <cstdint>
#include "esp_lcd_types.h"
#include "lvgl.h"

/// Get the frame buffer(s) and a PPA client for the direct flush. `panel_w` x `panel_h` is the
/// panel's own, unrotated size — the frame buffer's. Before the display is created, so the
/// caller can pick the draw format by the answer.
bool ppa_flush_prepare(esp_lcd_panel_handle_t panel, uint32_t panel_w, uint32_t panel_h);

/// Replace the display's flush with the direct one. Under the LVGL lock, before the first
/// refresh; the display draws RGB888 or XRGB8888 (the PPA converts the latter as it rotates).
/// False, and the port's flush left in place, without a prepared PPA or for another format.
bool ppa_flush_install(lv_display_t* disp);

/// Frames are composed in the frame buffer that is not on the glass (CONFIG_UI_DOUBLE_FB and a
/// panel with two of them).
bool ppa_flush_double();

/// From a list's LV_EVENT_SCROLL: its content moved down by `dy` logical pixels (up when
/// negative). With two frame buffers the next frame shifts the list's window instead of drawing
/// it, when that is exact; otherwise nothing changes. Under the LVGL lock, as events are.
void ppa_flush_list_scrolled(lv_obj_t* scroller, int32_t dy);

/// The list's entries were replaced: what the glass shows of it is a basis for a shift again only
/// once the frame that draws them is there. Under the LVGL lock.
void ppa_flush_list_reset(lv_obj_t* scroller);

/// A small object that floats over everything and may be over a list as it scrolls — the touch
/// ring. It does not stop a list's shift: where it was and where it is are drawn again instead.
/// Deleting the object forgets it (an LV_EVENT_DELETE callback); `on` false forgets it sooner.
/// False when four are registered already. Under the LVGL lock.
bool ppa_flush_floating(lv_obj_t* obj, bool on);

/// The frame buffer on the glass (or about to be, between frames), for a screenshot.
uint8_t* ppa_flush_glass();

/// The frame buffer nothing scans and nothing writes, under the LVGL lock between frames: waits
/// for a swap still on its way. Null when single-buffered or if the swap never lands.
uint8_t* ppa_flush_idle_back();

/// What composing frames has cost since the last reset, for the scroll bench.
struct PpaFlushStats {
    uint32_t frames;       ///< frames composed and swapped onto the glass
    uint32_t shifted;      ///< frames that shifted a list's window
    uint32_t dropped;      ///< shifts given up: the window was drawn whole instead
    uint32_t ineligible;   ///< scrolls of a list a shift could not copy exactly
    uint32_t swap_late;    ///< swaps never reported by the DPI driver (40 ms)
    uint32_t copy_fail;    ///< DMA2D copies refused or not finished in 50 ms
    uint32_t wait_us;      ///< waiting for a swap to reach the glass (compositor task)
    uint32_t sync_us;      ///< copying the last frame's changes into the buffer behind
    uint32_t shift_us;     ///< shifting lists' windows
    uint32_t block_wait_us;   ///< a frame's first block waiting for those copies (LVGL task)
    // why shifts did not happen (bench diagnostics)
    uint32_t hook_hit, hook_miss, covered, not_seen, too_soon, moved, too_far;
    uint32_t overlaid;     ///< frames drawn with something over the watched list's window
    int16_t  miss_hook[4], miss_got[4];   ///< the last missed hook: expected and received areas
};
void ppa_flush_stats(PpaFlushStats* out, bool reset);

/// The DPI frame buffers (fb[1] null when the panel has only one) and their size, unrotated.
struct PpaFlushFbs {
    uint8_t* fb[2];
    uint32_t w, h;
};
/// False before ppa_flush_prepare has succeeded.
bool ppa_flush_frame_buffers(PpaFlushFbs* out);

/// Blocks the PPA refused since boot (shown as dropped; the area's next refresh repaints it).
uint32_t ppa_flush_refused();
/// Blocks sent as several stripes because whole they would have hung the PPA (esp-idf#19023).
uint32_t ppa_flush_split();
/// Blocks too narrow for any safe stripe, rotated on the CPU instead.
uint32_t ppa_flush_cpu();
