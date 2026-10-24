/**
 * @file gt911_driver.h
 * @brief GT911 capacitive touch backend (lcd seam, capability "touch").
 *
 * Sits on a board I2C bus (find_i2c_bus) as one more device — the bus is shared with
 * the codec and the JP1 header. Owns its esp_lcd_panel_io (I2C) and esp_lcd_touch
 * handles; the UI module hands the latter to lvgl_port_add_touch().
 */

#pragma once

#include "modesp/hal/lcd_port.h"
#include "modesp/hal/hal_types.h"
#include "etl/string.h"

class Gt911Driver : public modesp::lcd::ILcdPort,
                    public modesp::lcd::ITouchPort {
public:
    void configure(const char* role, const modesp::I2CTouchConfig* cfg,
                   modesp::I2CBusResource* bus);

    // ILcdPort — this driver is the touch half of the LCD family
    modesp::lcd::ITouchPort* as_touch() override { return this; }

    // ITouchPort
    bool init() override;
    esp_lcd_touch_s* handle() const override { return tp_; }
    bool is_healthy() const override { return initialized_; }

private:
    etl::string<16> role_;
    const modesp::I2CTouchConfig* cfg_ = nullptr;
    modesp::I2CBusResource*       bus_ = nullptr;

    esp_lcd_panel_io_t* io_ = nullptr;
    esp_lcd_touch_s*    tp_ = nullptr;

    bool configured_  = false;
    bool initialized_ = false;
};
