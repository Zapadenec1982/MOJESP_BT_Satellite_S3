/**
 * @file ui_theme.h
 * @brief The LEGACY token set, kept for the screens not yet redrawn in Material 3.
 *
 * The design tokens proper are in src/ui_m3.h (colour roles, shape, type). This struct is
 * filled from those roles by ui_theme.cpp, so the old screens change colour with the new
 * ones and nobody maintains two palettes. New code names an m3 role; it does not add to
 * this struct. The struct leaves with the last old screen (docs/PLAN_UI_M3_2026-09-24.md, U6).
 */

#pragma once

#include "lvgl.h"
#include "ui_m3.h"
#include <cstdint>

/// Which scheme is active. Persisted as `ui.theme`; the same numbers as the schemes.
using UiThemeId = m3::Scheme;

struct UiTheme {
    const char* name;          ///< shown in Settings

    // ── colours (0xRRGGBB) ──
    uint32_t bg;               ///< screen gradient, top
    uint32_t bg2;              ///< screen gradient, bottom
    uint32_t panel;            ///< card / rail background
    uint32_t panel_on;         ///< card background, selected
    uint32_t hair;             ///< hairlines, borders, meter track
    uint32_t text;             ///< primary text
    uint32_t dim;              ///< secondary text
    uint32_t faint;            ///< labels, disabled
    uint32_t acc;              ///< the one accent
    uint32_t acc_lo;           ///< accent, darkened — borders and gradient ends
    uint32_t red;              ///< clipping, mute, danger

    // ── depth (the "rich" mode of the design) ──
    lv_opa_t glow_opa;         ///< accent glow strength behind hero objects
    lv_opa_t card_shadow_opa;  ///< drop shadow under cards

    // ── faces ──
    const lv_font_t* f_cap;    ///< 12 semibold — letter-spaced small caps
    const lv_font_t* f_body;   ///< 14 regular
    const lv_font_t* f_body_b; ///< 14 semibold
    const lv_font_t* f_lead;   ///< 18 regular — the "subtitle" line
    const lv_font_t* f_clock;  ///< 22 semibold
    const lv_font_t* f_title;  ///< 30 semibold — track title
    const lv_font_t* f_num;    ///< 38 semibold, digits only — volume
    const lv_font_t* f_num_xl; ///< 64 semibold, digits only — standby clock
    const lv_font_t* f_icon_s; ///< 16 px icons
    const lv_font_t* f_icon_m; ///< 22 px icons
    const lv_font_t* f_icon_l; ///< 26 px icons
    const lv_font_t* f_icon_xl;///< 32 px icons — the transport row
};

/// The active theme. Never null; defaults to AMBER before ui_theme_set() runs.
const UiTheme& ui_theme();
UiThemeId      ui_theme_id();
/// Returns true when the active theme actually changed (so the caller can rebuild).
bool           ui_theme_set(UiThemeId id);
const UiTheme& ui_theme_at(UiThemeId id);

/// Convenience: lv_color_t straight from a token.
static inline lv_color_t ui_col(uint32_t rgb) { return lv_color_hex(rgb); }
