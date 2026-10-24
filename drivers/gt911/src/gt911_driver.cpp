/**
 * @file gt911_driver.cpp
 * @brief GT911 touch backend over esp_lcd_touch_gt911 (vendor BSP bsp_touch.c recipe).
 */

#include "gt911_driver.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"

static const char* TAG = "Gt911";

void Gt911Driver::configure(const char* role, const modesp::I2CTouchConfig* cfg,
                            modesp::I2CBusResource* bus) {
    role_ = role;
    cfg_ = cfg;
    bus_ = bus;
    configured_ = (cfg != nullptr && bus != nullptr && bus->bus_handle != nullptr);
}

bool Gt911Driver::init() {
    if (!configured_) {
        ESP_LOGE(TAG, "Driver not configured");
        return false;
    }
    if (initialized_) return true;

    // I2C command channel (field-by-field: ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG() is a C
    // designated initializer whose order is not valid C++).
    esp_lcd_panel_io_i2c_config_t io_cfg = {};
    io_cfg.dev_addr = cfg_->address;
    io_cfg.control_phase_bytes = 1;
    io_cfg.dc_bit_offset = 0;
    io_cfg.lcd_cmd_bits = 16;
    io_cfg.lcd_param_bits = 0;
    io_cfg.flags.disable_control_phase = 1;
    io_cfg.scl_speed_hz = bus_->freq_hz;
    esp_err_t err = esp_lcd_new_panel_io_i2c_v2(bus_->bus_handle, &io_cfg, &io_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[%s] panel io: %s", role_.c_str(), esp_err_to_name(err));
        return false;
    }

    esp_lcd_touch_config_t tp_cfg = {};
    tp_cfg.x_max = cfg_->x_max;
    tp_cfg.y_max = cfg_->y_max;
    tp_cfg.rst_gpio_num = cfg_->rst_gpio;     // GPIO_NUM_NC → no reset pulse
    tp_cfg.int_gpio_num = cfg_->int_gpio;     // GPIO_NUM_NC → polled by LVGL
    tp_cfg.levels.reset = 0;
    tp_cfg.levels.interrupt = 0;
    tp_cfg.flags.swap_xy  = cfg_->swap_xy ? 1 : 0;
    tp_cfg.flags.mirror_x = cfg_->mirror_x ? 1 : 0;
    tp_cfg.flags.mirror_y = cfg_->mirror_y ? 1 : 0;

    err = esp_lcd_touch_new_i2c_gt911(io_, &tp_cfg, &tp_);
    if (err != ESP_OK) {
        // Chip absent / not answering at this address — the UI keeps running without touch.
        ESP_LOGE(TAG, "[%s] GT911 @0x%02X on '%s': %s", role_.c_str(),
                 cfg_->address, bus_->id.c_str(), esp_err_to_name(err));
        esp_lcd_panel_io_del(io_);
        io_ = nullptr;
        return false;
    }

    initialized_ = true;
    // The orientation flags are printed because they are the one setting here that cannot be
    // reasoned about from the schematic: the controller's native frame is whatever its own
    // config block says, not necessarily the panel's. On this board it is landscape 800x480
    // while the display is portrait 480x800, so a landscape UI needs no transform at all.
    ESP_LOGI(TAG, "[%s] GT911 @0x%02X on '%s' (%u kHz), %ux%u, int=%d rst=%d, swap=%d mx=%d my=%d",
             role_.c_str(), cfg_->address, bus_->id.c_str(),
             (unsigned)(bus_->freq_hz / 1000), (unsigned)cfg_->x_max, (unsigned)cfg_->y_max,
             (int)cfg_->int_gpio, (int)cfg_->rst_gpio,
             (int)cfg_->swap_xy, (int)cfg_->mirror_x, (int)cfg_->mirror_y);
    return true;
}

// ═══════════════════════════════════════════════════════════════
// Driver factory + registration (optional via CONFIG_MODESP_DRIVER_GT911)
// ═══════════════════════════════════════════════════════════════

#include "modesp/hal/driver_registry.h"
#include "modesp/hal/hal.h"
#include "etl/string_view.h"

namespace {
Gt911Driver s_pool[modesp::MAX_I2C_TOUCH];
size_t      s_n = 0;

modesp::lcd::ILcdPort* gt911_factory(const modesp::Binding& b, modesp::HAL& hal) {
    if (s_n >= modesp::MAX_I2C_TOUCH) {
        ESP_LOGE(TAG, "GT911 pool exhausted");
        return nullptr;
    }
    auto* cfg = hal.find_i2c_touch(
        etl::string_view(b.hardware_id.c_str(), b.hardware_id.size()));
    if (!cfg) {
        ESP_LOGE(TAG, "I2C touch '%s' not found in board.json", b.hardware_id.c_str());
        return nullptr;
    }
    auto* bus = hal.find_i2c_bus(
        etl::string_view(cfg->bus_id.c_str(), cfg->bus_id.size()));
    if (!bus || !bus->bus_handle) {
        ESP_LOGE(TAG, "I2C bus '%s' for touch '%s' not initialized",
                 cfg->bus_id.c_str(), b.hardware_id.c_str());
        return nullptr;
    }
    auto& drv = s_pool[s_n++];
    drv.configure(b.role.c_str(), cfg, bus);
    return &drv;
}
} // namespace

MODESP_REGISTER_LCD(gt911, &gt911_factory)
