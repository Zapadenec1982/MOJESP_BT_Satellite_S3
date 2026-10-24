/**
 * @file ui_scroll_bench.h
 * @brief GET /api/ui/scrollbench — a timed, scripted scroll (CONFIG_UI_SCROLL_BENCH only).
 *
 * Both functions are empty unless the option is set, so callers need no #if of their own.
 */
#pragma once

#include <cstdint>

/// Hook the display's refresh events and register the route. Call once, after the LVGL
/// display exists; takes the LVGL lock itself.
void ui_scroll_bench_init();

/// VList's scroll handler reports the time it spent placing rows. Counted only inside a
/// frame the bench is timing. LVGL task only.
void ui_scroll_bench_note_place(uint32_t us);

/// The main loop reports each turn (UiLvglModule::on_update), so a run can say whether the
/// list kept the 100 Hz loop waiting. Main loop only.
void ui_scroll_bench_note_main_turn();
