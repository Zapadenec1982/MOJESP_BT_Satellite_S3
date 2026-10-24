/**
 * @file gpio_output_driver.h
 * @brief Discrete GPIO output actuator (capability binary_out).
 *
 * HAL has already configured the pin push-pull and parked it in the OFF level
 * (init_gpio_outputs); this driver only flips it. Used for the amplifier enable /
 * DAC un-mute line (GPIO34 on the player: low = muted, held low until the audio
 * core streams).
 */

#pragma once

#include "modesp/hal/driver_interfaces.h"
#include "modesp/hal/hal_types.h"
#include "etl/string.h"

class GpioOutputDriver : public modesp::IActuatorDriver {
public:
    void configure(const char* role, modesp::GpioOutputResource* res);

    bool init() override;
    void update(uint32_t dt_ms) override {}
    bool set(bool state) override;
    bool get_state() const override { return state_; }
    const char* role() const override { return role_.c_str(); }
    const char* type() const override { return "gpio_output"; }
    bool is_healthy() const override { return initialized_; }
    uint32_t switch_count() const override { return switches_; }

private:
    etl::string<16> role_;
    modesp::GpioOutputResource* res_ = nullptr;
    bool     state_       = false;
    uint32_t switches_    = 0;
    bool     configured_  = false;
    bool     initialized_ = false;
};
