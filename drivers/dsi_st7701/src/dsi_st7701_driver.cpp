/**
 * @file dsi_st7701_driver.cpp
 * @brief ST7701S MIPI-DSI panel backend — bring-up sequence from the vendor BSP.
 *
 * Sequence (docs/CONCEPT.md §8б, csvke/esp32_p4_jc4880p433c_bsp bsp_display.c):
 *   MIPI PHY LDO → DSI bus → DBI io → esp_lcd_st7701 (vendor init table, DPI panel) →
 *   reset → init → display on → LEDC backlight (left at 0 %, the UI module sets it).
 */

#include "dsi_st7701_driver.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_st7701.h"
#include "esp_ldo_regulator.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "DsiSt7701";

namespace {

// ─────────────────────────────────────────────────────────────────────
// ST7701S init sequence for the JC4880P443C 480x800 panel.
// Source: csvke/esp32_p4_jc4880p433c_bsp, src/bsp_display.c (Apache-2.0,
// SPDX-FileCopyrightText: 2024 Espressif Systems). Register values are panel
// facts — copied verbatim, not tuned here. Named payload arrays instead of C99
// compound literals (those are temporaries in C++).
// ─────────────────────────────────────────────────────────────────────
#define ST7701_DATA(name, ...) static const uint8_t name[] = { __VA_ARGS__ };

ST7701_DATA(d_bk3,   0x77, 0x01, 0x00, 0x00, 0x13)
ST7701_DATA(d_ef08,  0x08)
ST7701_DATA(d_bk0,   0x77, 0x01, 0x00, 0x00, 0x10)
ST7701_DATA(d_c0,    0x63, 0x00)
ST7701_DATA(d_c1,    0x0D, 0x02)
ST7701_DATA(d_c2,    0x10, 0x08)
ST7701_DATA(d_cc,    0x10)
ST7701_DATA(d_b0_g,  0x80, 0x09, 0x53, 0x0C, 0xD0, 0x07, 0x0C, 0x09, 0x09, 0x28, 0x06, 0xD4, 0x13, 0x69, 0x2B, 0x71)
ST7701_DATA(d_b1_g,  0x80, 0x94, 0x5A, 0x10, 0xD3, 0x06, 0x0A, 0x08, 0x08, 0x25, 0x03, 0xD3, 0x12, 0x66, 0x6A, 0x0D)
ST7701_DATA(d_bk1,   0x77, 0x01, 0x00, 0x00, 0x11)
ST7701_DATA(d_b0,    0x5D)
ST7701_DATA(d_b1,    0x58)
ST7701_DATA(d_b2,    0x87)
ST7701_DATA(d_b3,    0x80)
ST7701_DATA(d_b5,    0x4E)
ST7701_DATA(d_b7,    0x85)
ST7701_DATA(d_b8,    0x21)
ST7701_DATA(d_b9,    0x10, 0x1F)
ST7701_DATA(d_bb,    0x03)
ST7701_DATA(d_bc,    0x00)
ST7701_DATA(d_c1b,   0x78)
ST7701_DATA(d_c2b,   0x78)
ST7701_DATA(d_d0,    0x88)
ST7701_DATA(d_e0,    0x00, 0x3A, 0x02)
ST7701_DATA(d_e1,    0x04, 0xA0, 0x00, 0xA0, 0x05, 0xA0, 0x00, 0xA0, 0x00, 0x40, 0x40)
ST7701_DATA(d_e2,    0x30, 0x00, 0x40, 0x40, 0x32, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00)
ST7701_DATA(d_e3,    0x00, 0x00, 0x33, 0x33)
ST7701_DATA(d_e4,    0x44, 0x44)
ST7701_DATA(d_e5,    0x09, 0x2E, 0xA0, 0xA0, 0x0B, 0x30, 0xA0, 0xA0, 0x05, 0x2A, 0xA0, 0xA0, 0x07, 0x2C, 0xA0, 0xA0)
ST7701_DATA(d_e6,    0x00, 0x00, 0x33, 0x33)
ST7701_DATA(d_e7,    0x44, 0x44)
ST7701_DATA(d_e8,    0x08, 0x2D, 0xA0, 0xA0, 0x0A, 0x2F, 0xA0, 0xA0, 0x04, 0x29, 0xA0, 0xA0, 0x06, 0x2B, 0xA0, 0xA0)
ST7701_DATA(d_eb,    0x00, 0x00, 0x4E, 0x4E, 0x00, 0x00, 0x00)
ST7701_DATA(d_ec,    0x08, 0x01)
ST7701_DATA(d_ed,    0xB0, 0x2B, 0x98, 0xA4, 0x56, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF7, 0x65, 0x4A, 0x89, 0xB2, 0x0B)
ST7701_DATA(d_ef,    0x08, 0x08, 0x08, 0x45, 0x3F, 0x54)
ST7701_DATA(d_bk_off,0x77, 0x01, 0x00, 0x00, 0x00)
ST7701_DATA(d_zero,  0x00)

#define ST7701_CMD(code, payload, delay) { (code), (payload), sizeof(payload), (delay) }

const st7701_lcd_init_cmd_t s_init_cmds[] = {
    ST7701_CMD(0xFF, d_bk3,    0),
    ST7701_CMD(0xEF, d_ef08,   0),
    ST7701_CMD(0xFF, d_bk0,    0),
    ST7701_CMD(0xC0, d_c0,     0),
    ST7701_CMD(0xC1, d_c1,     0),
    ST7701_CMD(0xC2, d_c2,     0),
    ST7701_CMD(0xCC, d_cc,     0),
    ST7701_CMD(0xB0, d_b0_g,   0),
    ST7701_CMD(0xB1, d_b1_g,   0),
    ST7701_CMD(0xFF, d_bk1,    0),
    ST7701_CMD(0xB0, d_b0,     0),
    ST7701_CMD(0xB1, d_b1,     0),
    ST7701_CMD(0xB2, d_b2,     0),
    ST7701_CMD(0xB3, d_b3,     0),
    ST7701_CMD(0xB5, d_b5,     0),
    ST7701_CMD(0xB7, d_b7,     0),
    ST7701_CMD(0xB8, d_b8,     0),
    ST7701_CMD(0xB9, d_b9,     0),
    ST7701_CMD(0xBB, d_bb,     0),
    ST7701_CMD(0xBC, d_bc,     0),
    ST7701_CMD(0xC1, d_c1b,    0),
    ST7701_CMD(0xC2, d_c2b,    0),
    ST7701_CMD(0xD0, d_d0,     0),
    ST7701_CMD(0xE0, d_e0,     0),
    ST7701_CMD(0xE1, d_e1,     0),
    ST7701_CMD(0xE2, d_e2,     0),
    ST7701_CMD(0xE3, d_e3,     0),
    ST7701_CMD(0xE4, d_e4,     0),
    ST7701_CMD(0xE5, d_e5,     0),
    ST7701_CMD(0xE6, d_e6,     0),
    ST7701_CMD(0xE7, d_e7,     0),
    ST7701_CMD(0xE8, d_e8,     0),
    ST7701_CMD(0xEB, d_eb,     0),
    ST7701_CMD(0xEC, d_ec,     0),
    ST7701_CMD(0xED, d_ed,     0),
    ST7701_CMD(0xEF, d_ef,     0),
    ST7701_CMD(0xFF, d_bk_off, 0),
    ST7701_CMD(0x11, d_zero,   120),   // sleep out
    ST7701_CMD(0x29, d_zero,   20),    // display on
};

// Backlight PWM: LEDC low-speed timer/channel 1 (BSP defaults), 10-bit duty.
constexpr ledc_timer_t   BL_TIMER   = LEDC_TIMER_1;
constexpr ledc_channel_t BL_CHANNEL = LEDC_CHANNEL_1;
constexpr uint32_t       BL_DUTY_MAX = 1023;

} // namespace

void DsiSt7701Driver::configure(const char* role, const modesp::DsiDisplayConfig* cfg) {
    role_ = role;
    cfg_ = cfg;
    configured_ = (cfg != nullptr);
}

modesp::lcd::LcdGeometry DsiSt7701Driver::geometry() const {
    modesp::lcd::LcdGeometry g;
    if (cfg_) {
        g.h_res = cfg_->h_res;
        g.v_res = cfg_->v_res;
        g.color_bits = cfg_->color_bits;
        g.rotation = cfg_->rotation;
    }
    return g;
}

bool DsiSt7701Driver::init_phy_power() {
    if (cfg_->ldo_chan < 0) return true;   // PHY powered externally / by hardware
    esp_ldo_channel_config_t ldo_cfg = {};
    ldo_cfg.chan_id = cfg_->ldo_chan;
    ldo_cfg.voltage_mv = cfg_->ldo_mv;
    esp_err_t err = esp_ldo_acquire_channel(&ldo_cfg, &ldo_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[%s] MIPI PHY LDO ch%d %d mV: %s", role_.c_str(),
                 cfg_->ldo_chan, cfg_->ldo_mv, esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "[%s] MIPI PHY LDO ch%d = %d mV", role_.c_str(), cfg_->ldo_chan, cfg_->ldo_mv);
    return true;
}

bool DsiSt7701Driver::init_backlight() {
    if (cfg_->backlight_gpio == GPIO_NUM_NC) return true;   // always-on backlight

    ledc_timer_config_t t = {};
    t.speed_mode      = LEDC_LOW_SPEED_MODE;
    t.duty_resolution = LEDC_TIMER_10_BIT;
    t.timer_num       = BL_TIMER;
    t.freq_hz         = cfg_->backlight_pwm_hz;
    t.clk_cfg         = LEDC_AUTO_CLK;
    esp_err_t err = ledc_timer_config(&t);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[%s] backlight timer: %s", role_.c_str(), esp_err_to_name(err));
        return false;
    }

    ledc_channel_config_t ch = {};
    ch.gpio_num   = cfg_->backlight_gpio;
    ch.speed_mode = LEDC_LOW_SPEED_MODE;
    ch.channel    = BL_CHANNEL;
    ch.intr_type  = LEDC_INTR_DISABLE;
    ch.timer_sel  = BL_TIMER;
    ch.duty       = 0;                                   // dark until the UI has drawn a frame
    ch.hpoint     = 0;
    ch.flags.output_invert = cfg_->backlight_active_high ? 0 : 1;
    err = ledc_channel_config(&ch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[%s] backlight channel: %s", role_.c_str(), esp_err_to_name(err));
        return false;
    }
    backlight_ready_ = true;
    return true;
}

bool DsiSt7701Driver::init() {
    if (!configured_) {
        ESP_LOGE(TAG, "Driver not configured");
        return false;
    }
    if (initialized_) return true;

    if (!init_phy_power()) return false;

    // DSI bus (lanes + bit rate from board.json)
    esp_lcd_dsi_bus_config_t bus_cfg = {};
    bus_cfg.bus_id = 0;
    bus_cfg.num_data_lanes = cfg_->lanes;
    bus_cfg.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
    bus_cfg.lane_bit_rate_mbps = cfg_->lane_mbps;
    esp_err_t err = esp_lcd_new_dsi_bus(&bus_cfg, &bus_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[%s] DSI bus: %s", role_.c_str(), esp_err_to_name(err));
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(50));   // let the PHY settle before the first DBI transfer (BSP)

    // DBI command channel
    esp_lcd_dbi_io_config_t dbi_cfg = {};
    dbi_cfg.virtual_channel = 0;
    dbi_cfg.lcd_cmd_bits = 8;
    dbi_cfg.lcd_param_bits = 8;
    err = esp_lcd_new_panel_io_dbi(bus_, &dbi_cfg, &io_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[%s] DBI io: %s", role_.c_str(), esp_err_to_name(err));
        return false;
    }

    // DPI video panel: timings from board.json, two frame buffers, DMA2D for the
    // draw-buffer → frame-buffer copy.
    const bool rgb888 = (cfg_->color_bits == 24);
    esp_lcd_dpi_panel_config_t dpi_cfg = {};
    dpi_cfg.virtual_channel = 0;
    dpi_cfg.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpi_cfg.dpi_clock_freq_mhz = cfg_->dpi_mhz;
    dpi_cfg.pixel_format = rgb888 ? LCD_COLOR_PIXEL_FORMAT_RGB888 : LCD_COLOR_PIXEL_FORMAT_RGB565;
    dpi_cfg.num_fbs = 2;
    dpi_cfg.video_timing.h_size = cfg_->h_res;
    dpi_cfg.video_timing.v_size = cfg_->v_res;
    dpi_cfg.video_timing.hsync_pulse_width = cfg_->hsync;
    dpi_cfg.video_timing.hsync_back_porch  = cfg_->hbp;
    dpi_cfg.video_timing.hsync_front_porch = cfg_->hfp;
    dpi_cfg.video_timing.vsync_pulse_width = cfg_->vsync;
    dpi_cfg.video_timing.vsync_back_porch  = cfg_->vbp;
    dpi_cfg.video_timing.vsync_front_porch = cfg_->vfp;
    dpi_cfg.flags.use_dma2d = 1;

    st7701_vendor_config_t vendor_cfg = {};
    vendor_cfg.init_cmds = s_init_cmds;
    vendor_cfg.init_cmds_size = sizeof(s_init_cmds) / sizeof(s_init_cmds[0]);
    vendor_cfg.mipi_config.dsi_bus = bus_;
    vendor_cfg.mipi_config.dpi_config = &dpi_cfg;
    vendor_cfg.flags.use_mipi_interface = 1;

    esp_lcd_panel_dev_config_t dev_cfg = {};
    dev_cfg.reset_gpio_num = cfg_->reset_gpio;
    dev_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    dev_cfg.bits_per_pixel = rgb888 ? 24 : 16;
    dev_cfg.vendor_config = &vendor_cfg;

    err = esp_lcd_new_panel_st7701(io_, &dev_cfg, &panel_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[%s] new panel: %s", role_.c_str(), esp_err_to_name(err));
        return false;
    }
    if ((err = esp_lcd_panel_reset(panel_)) != ESP_OK ||
        (err = esp_lcd_panel_init(panel_)) != ESP_OK ||
        (err = esp_lcd_panel_disp_on_off(panel_, true)) != ESP_OK) {
        ESP_LOGE(TAG, "[%s] panel bring-up: %s", role_.c_str(), esp_err_to_name(err));
        return false;
    }

    if (!init_backlight()) return false;

    initialized_ = true;
    ESP_LOGI(TAG, "[%s] %ux%u RGB%u, %u lanes @ %u Mbps, DPI %u MHz, reset GPIO %d, backlight GPIO %d",
             role_.c_str(), (unsigned)cfg_->h_res, (unsigned)cfg_->v_res,
             (unsigned)cfg_->color_bits, (unsigned)cfg_->lanes, (unsigned)cfg_->lane_mbps,
             (unsigned)cfg_->dpi_mhz, (int)cfg_->reset_gpio, (int)cfg_->backlight_gpio);
    return true;
}

void DsiSt7701Driver::set_backlight(uint8_t percent) {
    if (!backlight_ready_) return;
    if (percent > 100) percent = 100;
    const uint32_t duty = (BL_DUTY_MAX * percent) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL);
}

// ═══════════════════════════════════════════════════════════════
// Driver factory + registration (optional via CONFIG_MODESP_DRIVER_DSI_ST7701)
// ═══════════════════════════════════════════════════════════════

#include "modesp/hal/driver_registry.h"
#include "modesp/hal/hal.h"
#include "etl/string_view.h"

namespace {
DsiSt7701Driver s_pool[modesp::MAX_DSI_DISPLAYS];
size_t          s_n = 0;

modesp::lcd::ILcdPort* dsi_st7701_factory(const modesp::Binding& b, modesp::HAL& hal) {
    if (s_n >= modesp::MAX_DSI_DISPLAYS) {
        ESP_LOGE(TAG, "DSI panel pool exhausted");
        return nullptr;
    }
    auto* cfg = hal.find_dsi_display(
        etl::string_view(b.hardware_id.c_str(), b.hardware_id.size()));
    if (!cfg) {
        ESP_LOGE(TAG, "DSI display '%s' not found in board.json", b.hardware_id.c_str());
        return nullptr;
    }
    auto& drv = s_pool[s_n++];
    drv.configure(b.role.c_str(), cfg);
    return &drv;
}
} // namespace

MODESP_REGISTER_LCD(dsi_st7701, &dsi_st7701_factory)
