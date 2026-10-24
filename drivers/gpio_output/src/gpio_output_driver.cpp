/**
 * @file gpio_output_driver.cpp
 * @brief Discrete GPIO output actuator.
 */

#include "gpio_output_driver.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char* TAG = "GpioOut";

void GpioOutputDriver::configure(const char* role, modesp::GpioOutputResource* res) {
    role_ = role;
    res_ = res;
    configured_ = (res != nullptr);
}

bool GpioOutputDriver::init() {
    if (!configured_ || !res_->initialized) {
        ESP_LOGE(TAG, "Driver not configured");
        return false;
    }
    state_ = false;   // HAL parked the pin OFF
    initialized_ = true;
    ESP_LOGI(TAG, "[%s] GPIO %d (active_%s), OFF", role_.c_str(), (int)res_->gpio,
             res_->active_high ? "HIGH" : "LOW");
    return true;
}

bool GpioOutputDriver::set(bool state) {
    if (!initialized_) return false;
    if (state != state_) switches_++;
    state_ = state;
    const int level = res_->active_high ? (state ? 1 : 0) : (state ? 0 : 1);
    return gpio_set_level(res_->gpio, level) == ESP_OK;
}

// ═══════════════════════════════════════════════════════════════
// Driver factory + registration (optional via CONFIG_MODESP_DRIVER_GPIO_OUTPUT)
// ═══════════════════════════════════════════════════════════════

#include "modesp/hal/driver_registry.h"
#include "modesp/hal/hal.h"
#include "etl/string_view.h"

namespace {
GpioOutputDriver s_pool[modesp::MAX_ACTUATORS];
size_t           s_n = 0;

modesp::IActuatorDriver* gpio_output_factory(const modesp::Binding& b, modesp::HAL& hal) {
    if (s_n >= modesp::MAX_ACTUATORS) {
        ESP_LOGE(TAG, "GPIO output pool exhausted");
        return nullptr;
    }
    auto* res = hal.find_gpio_output(
        etl::string_view(b.hardware_id.c_str(), b.hardware_id.size()));
    if (!res) {
        ESP_LOGE(TAG, "GPIO output '%s' not found in HAL", b.hardware_id.c_str());
        return nullptr;
    }
    auto& drv = s_pool[s_n++];
    drv.configure(b.role.c_str(), res);
    return &drv;
}
} // namespace

MODESP_REGISTER_ACTUATOR(gpio_output, &gpio_output_factory)
