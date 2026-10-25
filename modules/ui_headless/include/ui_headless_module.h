/**
 * @file ui_headless_module.h
 * @brief The player's UI module for a board without a display.
 *
 * On the P4 panel, ui_lvgl owns more than the glass: the web's Player gets its picture through
 * it (ui.cover, GET /api/cover), the web's Library its rows (GET /api/library), the web's colour
 * scheme and default language follow its keys (ui.theme, ui.lang), and the encoder is read by
 * it. A board with no display (the ESP32-S3, docs/S3_PORT.md) keeps all of that — under the same
 * keys and routes, so the web needs no second version — without LVGL, without a panel and
 * without decoding a single pixel: the cover goes to the browser as the file that arrived
 * (components/cover, CONFIG_COVER_DECODE_TILE=n).
 *
 * The encoder with no screen to interpret it:
 *   - turn  — volume, 2 % a detent (a turn also ends a mute: wanting sound at that level);
 *   - press — play/pause of whatever is playing, in that source's own commands, as the panel's
 *             Player button sends them;
 *   - hold  — mute on/off (650 ms, fired while still held, as the panel's hold).
 *
 * One of ui_lvgl and ui_headless is in a build, never both: project.json's board overlay
 * (boards.<board>.exclude_modules / add_modules) picks.
 */

#pragma once

#include "modesp/base_module.h"
#include <cstdint>

namespace modesp { class DriverManager; struct BindingTable; class HAL; class ISensorDriver; }

class UiHeadlessModule : public modesp::BaseModule {
public:
    UiHeadlessModule();

    void on_bind(modesp::DriverManager& drivers, const modesp::BindingTable& bindings,
                 modesp::HAL& hal) override;
    bool on_init() override;
    void on_update(uint32_t dt_ms) override;

private:
    void poll_encoder(uint32_t dt_ms);
    void play_pause();
    void mirror_cover();

    modesp::ISensorDriver* encoder_    = nullptr;
    modesp::ISensorDriver* encoder_sw_ = nullptr;

    int32_t  last_pos_     = 0;
    bool     last_sw_      = false;
    uint32_t sw_held_ms_   = 0;
    bool     sw_hold_done_ = false;

    uint32_t cover_ms_ = 0;
    int32_t  cover_id_ = -1;     ///< what ui.cover says; -1 before the first publish
};
