/**
 * @file ui_screenshot.h
 * @brief GET /api/screenshot — what the panel is showing, as a BMP. See ui_screenshot.cpp.
 */

#pragma once

#include <cstdint>
#include "esp_lcd_types.h"

/// Remember where the pixels are and ask for the route. Call from on_init, once the panel
/// is up: the route table is read when the HTTP server starts, after every module's init.
/// `w`/`h` are the panel's own (unrotated) size, `rotation` is board.json's, in degrees.
void ui_screenshot_init(esp_lcd_panel_handle_t panel, uint16_t w, uint16_t h,
                        uint8_t color_bits, uint16_t rotation);
