/**
 * @file lcd_port.h
 * @brief Seam of the graphic-LCD family (MIPI-DSI/RGB panel + touch controller).
 *
 * The UI module (modules/ui_lvgl) owns LVGL and the screen content; a driver behind this
 * seam owns the hardware: DSI bus, DPI panel, PHY LDO, reset, backlight PWM (ILcdPanel)
 * or the touch controller device (ITouchPort). The module never sees pins, timings,
 * init tables or chip registers — it only receives the esp_lcd handles that
 * esp_lvgl_port needs, plus a backlight setter.
 *
 * Distinct from display_port.h (semantic character-OSD seam: MainView/MenuView pushed
 * to AT7456E/AMT630A-class backends) and from panel::IPanelPort (BLE LED panel).
 * One driver = one capability ("lcd" or "touch"), so bindings stay one-hardware-id-
 * per-driver; ILcdPort::as_panel()/as_touch() is the capability cast (no RTTI).
 *
 * IDF types are forward-declared only — this header stays buildable on the host
 * (tests/host) and pulls no esp_lcd headers into modesp_hal.
 */

#pragma once

#include <cstdint>

// Opaque esp_lcd / esp_lcd_touch objects (esp_lcd_panel_handle_t = esp_lcd_panel_t*,
// esp_lcd_panel_io_handle_t = esp_lcd_panel_io_t*, esp_lcd_touch_handle_t = esp_lcd_touch_s*).
struct esp_lcd_panel_t;
struct esp_lcd_panel_io_t;
struct esp_lcd_touch_s;

namespace modesp::lcd {

/// Pixel geometry of the bound panel (native orientation, before any LVGL rotation).
struct LcdGeometry {
    uint16_t h_res = 0;         ///< panel-native width (before any UI rotation)
    uint16_t v_res = 0;         ///< panel-native height
    uint8_t  color_bits = 16;   ///< 16 = RGB565, 24 = RGB888
    uint16_t rotation = 0;      ///< UI rotation the board asks for: 0/90/180/270 degrees
};

/// Graphic panel. init() brings the hardware up (power, bus, panel init table, reset,
/// backlight); afterwards panel()/io() are valid until the driver is torn down.
class ILcdPanel {
public:
    virtual ~ILcdPanel() = default;
    virtual bool init() = 0;
    virtual LcdGeometry geometry() const = 0;
    virtual esp_lcd_panel_t*    panel() const = 0;   ///< esp_lcd_panel_handle_t
    virtual esp_lcd_panel_io_t* io() const = 0;      ///< esp_lcd_panel_io_handle_t (DBI/SPI command channel)
    /// Backlight 0..100 %. Safe from any task (no LVGL lock needed).
    virtual void set_backlight(uint8_t percent) = 0;
    virtual bool is_healthy() const = 0;
};

/// Touch controller. init() probes the chip; handle() feeds lvgl_port_add_touch().
class ITouchPort {
public:
    virtual ~ITouchPort() = default;
    virtual bool init() = 0;
    virtual esp_lcd_touch_s* handle() const = 0;     ///< esp_lcd_touch_handle_t
    virtual bool is_healthy() const = 0;
};

/// What a "lcd"-category driver factory returns. A driver implements one of the two
/// capabilities and returns itself from the matching cast; the other stays nullptr.
class ILcdPort {
public:
    virtual ~ILcdPort() = default;
    virtual ILcdPanel*  as_panel() { return nullptr; }
    virtual ITouchPort* as_touch() { return nullptr; }
};

} // namespace modesp::lcd
