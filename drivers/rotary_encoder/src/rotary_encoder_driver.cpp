/**
 * @file rotary_encoder_driver.cpp
 * @brief PCNT quadrature decoder — IDF examples/peripherals/pcnt/rotary_encoder recipe.
 */

#include "rotary_encoder_driver.h"
#include "esp_log.h"
#include "driver/pulse_cnt.h"

static const char* TAG = "RotaryEnc";

void RotaryEncoderDriver::configure(const char* role, modesp::EncoderResource* enc,
                                    uint8_t steps_per_detent, bool invert) {
    role_ = role;
    enc_ = enc;
    steps_per_detent_ = steps_per_detent ? steps_per_detent : 1;
    invert_ = invert;
    configured_ = (enc != nullptr);
}

bool RotaryEncoderDriver::init() {
    if (!configured_) {
        ESP_LOGE(TAG, "Driver not configured");
        return false;
    }
    if (initialized_) return true;

    pcnt_unit_config_t unit_cfg = {};
    unit_cfg.high_limit = PCNT_HIGH_LIMIT;
    unit_cfg.low_limit  = PCNT_LOW_LIMIT;
    esp_err_t err = pcnt_new_unit(&unit_cfg, &unit_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[%s] pcnt unit: %s", role_.c_str(), esp_err_to_name(err));
        return false;
    }

    // Mechanical contacts: swallow sub-microsecond glitches; the two-channel quadrature
    // decode below rejects bounce on one line while the other is stable.
    pcnt_glitch_filter_config_t filter_cfg = {};
    filter_cfg.max_glitch_ns = 1000;
    pcnt_unit_set_glitch_filter(unit_, &filter_cfg);

    // Channel A: edges on CLK, direction from DT; channel B: edges on DT, direction from
    // CLK → 4 counts per detent on an EC11. (pcnt enables the input pull-ups itself.)
    pcnt_chan_config_t a_cfg = {};
    a_cfg.edge_gpio_num  = enc_->clk;
    a_cfg.level_gpio_num = enc_->dt;
    pcnt_chan_config_t b_cfg = {};
    b_cfg.edge_gpio_num  = enc_->dt;
    b_cfg.level_gpio_num = enc_->clk;
    if ((err = pcnt_new_channel(unit_, &a_cfg, &chan_a_)) != ESP_OK ||
        (err = pcnt_new_channel(unit_, &b_cfg, &chan_b_)) != ESP_OK) {
        ESP_LOGE(TAG, "[%s] pcnt channel: %s", role_.c_str(), esp_err_to_name(err));
        return false;
    }
    pcnt_channel_set_edge_action(chan_a_, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(chan_a_, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_channel_set_edge_action(chan_b_, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(chan_b_, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

    if ((err = pcnt_unit_enable(unit_)) != ESP_OK ||
        (err = pcnt_unit_clear_count(unit_)) != ESP_OK ||
        (err = pcnt_unit_start(unit_)) != ESP_OK) {
        ESP_LOGE(TAG, "[%s] pcnt start: %s", role_.c_str(), esp_err_to_name(err));
        return false;
    }

    raw_ = 0;
    last_count_ = 0;
    initialized_ = true;
    ESP_LOGI(TAG, "[%s] PCNT on GPIO %d/%d, %u edges/detent%s",
             role_.c_str(), (int)enc_->clk, (int)enc_->dt,
             (unsigned)steps_per_detent_, invert_ ? ", inverted" : "");
    return true;
}

void RotaryEncoderDriver::update(uint32_t /*dt_ms*/) {
    if (!initialized_) return;

    int count = 0;
    if (pcnt_unit_get_count(unit_, &count) != ESP_OK) {
        if (consecutive_errors_ < MAX_CONSECUTIVE_ERRORS) consecutive_errors_++;
        return;
    }
    consecutive_errors_ = 0;

    const int delta = count - last_count_;
    if (delta != 0) {
        raw_ += invert_ ? -delta : delta;
        last_count_ = count;
    }

    // Keep the hardware counter far from its limits so it never wraps between polls.
    if (count > RECENTRE_AT || count < -RECENTRE_AT) {
        pcnt_unit_clear_count(unit_);
        last_count_ = 0;
    }
}

bool RotaryEncoderDriver::read(float& value) {
    if (!initialized_) return false;
    value = (float)(raw_ / (int32_t)steps_per_detent_);
    return true;
}

// ═══════════════════════════════════════════════════════════════
// Driver factory + registration (optional via CONFIG_MODESP_DRIVER_ROTARY_ENCODER)
// ═══════════════════════════════════════════════════════════════

#include "modesp/hal/driver_registry.h"
#include "modesp/hal/hal.h"
#include "etl/string_view.h"

namespace {
RotaryEncoderDriver s_pool[modesp::MAX_ENCODERS];
size_t              s_n = 0;

modesp::ISensorDriver* rotary_encoder_factory(const modesp::Binding& b, modesp::HAL& hal) {
    if (s_n >= modesp::MAX_ENCODERS) {
        ESP_LOGE(TAG, "Encoder pool exhausted");
        return nullptr;
    }
    auto* enc = hal.find_encoder(
        etl::string_view(b.hardware_id.c_str(), b.hardware_id.size()));
    if (!enc) {
        ESP_LOGE(TAG, "Encoder '%s' not found in HAL", b.hardware_id.c_str());
        return nullptr;
    }
    auto& drv = s_pool[s_n++];
    drv.configure(b.role.c_str(), enc,
                  (uint8_t)b.setting_or("steps_per_detent", 4.0f),
                  b.setting_or("invert", 0.0f) > 0.5f);
    return &drv;
}
} // namespace

MODESP_REGISTER_SENSOR(rotary_encoder, &rotary_encoder_factory)
