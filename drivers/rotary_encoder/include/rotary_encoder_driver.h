/**
 * @file rotary_encoder_driver.h
 * @brief Incremental rotary encoder on the PCNT peripheral (ISensorDriver).
 *
 * HAL owns the two quadrature pins (EncoderResource: input + pull); this driver owns
 * the PCNT unit and its two channels (mirrors the ADC-oneshot ownership rule). The
 * counter is read in update() at the main-loop rate and accumulated into a 32-bit
 * position, so the 16-bit hardware counter never wraps. read() returns the position
 * in detents (raw edges / steps_per_detent).
 */

#pragma once

#include "modesp/hal/driver_interfaces.h"
#include "modesp/hal/hal_types.h"
#include "etl/string.h"

struct pcnt_unit_t;   // pcnt_unit_handle_t
struct pcnt_chan_t;   // pcnt_channel_handle_t

class RotaryEncoderDriver : public modesp::ISensorDriver {
public:
    void configure(const char* role, modesp::EncoderResource* enc,
                   uint8_t steps_per_detent, bool invert);

    bool init() override;
    void update(uint32_t dt_ms) override;
    bool read(float& value) override;   // position in detents (signed)
    bool is_healthy() const override {
        return initialized_ && consecutive_errors_ < MAX_CONSECUTIVE_ERRORS;
    }
    const char* role() const override { return role_.c_str(); }
    const char* type() const override { return "rotary_encoder"; }
    uint32_t error_count() const override { return consecutive_errors_; }

    /// Raw accumulated edge count (4 per detent on an EC11).
    int32_t raw_position() const { return raw_; }

private:
    etl::string<16> role_;
    modesp::EncoderResource* enc_ = nullptr;
    pcnt_unit_t* unit_   = nullptr;
    pcnt_chan_t* chan_a_ = nullptr;
    pcnt_chan_t* chan_b_ = nullptr;

    uint8_t steps_per_detent_ = 4;
    bool    invert_ = false;
    int32_t raw_ = 0;
    int     last_count_ = 0;
    uint8_t consecutive_errors_ = 0;
    bool    configured_  = false;
    bool    initialized_ = false;

    // Hardware counter range and the re-centre threshold (polled at 100 Hz, a hand
    // cannot produce anywhere near 10k edges between two polls).
    static constexpr int PCNT_HIGH_LIMIT = 30000;
    static constexpr int PCNT_LOW_LIMIT  = -30000;
    static constexpr int RECENTRE_AT     = 10000;
    static constexpr uint8_t MAX_CONSECUTIVE_ERRORS = 5;
};
