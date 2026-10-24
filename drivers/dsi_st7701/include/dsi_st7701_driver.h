/**
 * @file dsi_st7701_driver.h
 * @brief ST7701S MIPI-DSI panel backend (lcd seam, capability "lcd").
 *
 * Owns everything below the esp_lcd handles: MIPI PHY LDO, DSI bus, DBI command
 * channel, DPI video panel (frame buffers in PSRAM), reset line and the LEDC backlight.
 * Geometry/timings/pins come from board.json (DsiDisplayConfig) — nothing is hardcoded
 * here except the panel's vendor init table.
 */

#pragma once

#include "modesp/hal/lcd_port.h"
#include "modesp/hal/hal_types.h"
#include "etl/string.h"

struct esp_lcd_dsi_bus_t;        // esp_lcd_dsi_bus_handle_t
struct ldo_regulator_channel_t;  // esp_ldo_channel_handle_t

class DsiSt7701Driver : public modesp::lcd::ILcdPort,
                        public modesp::lcd::ILcdPanel {
public:
    void configure(const char* role, const modesp::DsiDisplayConfig* cfg);

    // ILcdPort — this driver is the panel half of the LCD family
    modesp::lcd::ILcdPanel* as_panel() override { return this; }

    // ILcdPanel
    bool init() override;
    modesp::lcd::LcdGeometry geometry() const override;
    esp_lcd_panel_t*    panel() const override { return panel_; }
    esp_lcd_panel_io_t* io()    const override { return io_; }
    void set_backlight(uint8_t percent) override;
    bool is_healthy() const override { return initialized_; }

private:
    bool init_phy_power();
    bool init_backlight();

    etl::string<16> role_;
    const modesp::DsiDisplayConfig* cfg_ = nullptr;

    ldo_regulator_channel_t* ldo_   = nullptr;
    esp_lcd_dsi_bus_t*       bus_   = nullptr;
    esp_lcd_panel_io_t*      io_    = nullptr;
    esp_lcd_panel_t*         panel_ = nullptr;

    bool backlight_ready_ = false;
    bool configured_      = false;
    bool initialized_     = false;
};
