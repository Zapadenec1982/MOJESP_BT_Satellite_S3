/**
 * @file i2s_std_driver.h
 * @brief I²S standard-mode stereo transport (audio seam: IAudioSink + IAudioPort).
 *
 * One board `i2s_buses` entry → one driver instance. Master with dout+din = full
 * duplex on one controller (DAC + ADC in one clock domain); slave with din only =
 * capture from an external master (BT module). Pins, role, fs, slot width and MCLK
 * multiple come from board.json (I2SBusResource); DMA depth is a driver constant sized
 * for the Boya-flash cache stall (docs/CONCEPT.md, "Board constraints").
 */

#pragma once

#include "modesp/hal/audio_sink.h"
#include "modesp/hal/hal_types.h"
#include "etl/string.h"
#include <atomic>

struct i2s_channel_obj_t;   // i2s_chan_handle_t

class I2sStdDriver : public modesp::audio::IAudioSink,
                     public modesp::audio::IAudioPort {
public:
    void configure(const char* role, const modesp::I2SBusResource* bus);

    // ── IAudioSink (legacy 16-bit mono path, kept for the framework's player seam) ──
    bool init() override;                                   // also IAudioPort::init
    modesp::audio::AudioCaps caps() const override;
    bool start(uint32_t sample_rate_hz) override;
    size_t write(const int16_t* pcm, size_t frames, uint32_t timeout_ms) override;
    void stop() override;                                   // also IAudioPort::stop
    bool is_healthy() const override { return initialized_; }   // both seams
    const char* name() const override { return "i2s_std"; }     // both seams
    modesp::audio::IAudioPort* as_port() override { return this; }

    // ── IAudioPort (full-duplex stereo int32) ──
    modesp::audio::AudioPortCaps port_caps() const override;
    modesp::audio::AudioPortDiagnostics diagnostics() const override;
    bool start() override;
    bool set_rate(uint32_t hz, uint16_t mclk_multiple) override;
    bool set_format(uint32_t hz, uint8_t slot_bits) override;
    bool can_trim() const override;
    bool trim_ppm(int32_t ppm, int32_t* applied_ppm) override;
    void set_line_input(bool on) override;
    modesp::audio::LinkPins link_pins() const override;
    bool set_link_master(const modesp::audio::LinkPins* pins) override;
    uint8_t slot_bits() const override { return slot_bits_; }
    uint32_t rate() const override { return rate_; }
    size_t write(const int32_t* stereo, size_t frames, uint32_t timeout_ms) override;
    size_t read(int32_t* stereo, size_t frames, uint32_t timeout_ms) override;

    /// Frames per DMA descriptor and descriptor counts (DMA-SRAM budget: see .cpp).
    static constexpr uint32_t DMA_FRAMES   = 480;   // 10 ms @ 48 kHz
    static constexpr uint32_t TX_DMA_DESCS = 8;     // 80 ms ring; a duplex port gets it for RX too
    /// An RX-only (slave) port is sized in BYTES, not milliseconds, and the board now opens
    /// it at its WIDEST slot so IDF never has to reallocate - see park_bt_off_pll(). That
    /// turns the byte budget from a peak the system meets occasionally into a constant it
    /// lives with always, so it is set at what the NARROW port used to cost:
    ///
    ///     120 frames x 2 slots x 4 bytes x 6 descriptors = 5760 B
    ///
    /// 240 frames bought 30 ms and cost 11520 B at this width, and those extra 5760 bytes
    /// are not free here: DMA-capable internal RAM is down to about 2.5 KB while a stream
    /// runs, and taking them turned "DLNA plays" into "DLNA starts at 0.49x". 15 ms is the
    /// trade, behind a reader task at priority 23 on core 1 that does nothing but move
    /// samples into a one-second ring in PSRAM.
    static constexpr uint32_t RX_DMA_FRAMES = 120;
    static constexpr uint32_t RX_DMA_DESCS  = 6;

private:
    etl::string<16> role_;
    const modesp::I2SBusResource* bus_ = nullptr;
    i2s_channel_obj_t* tx_ = nullptr;
    i2s_channel_obj_t* rx_ = nullptr;
    bool configured_  = false;
    bool initialized_ = false;
    uint32_t rate_ = 0;        ///< what the master clock is running at now
    /// MCLK as a multiple of the rate. Starts at the board's and moves when a format needs a
    /// different audio PLL — the PLL is twice this MCLK (i2s_clock.h).
    uint16_t mclk_mult_ = 256;
    bool running_     = false;
    /// set_rate() filled every TX descriptor with keepalive just before it stopped the clock,
    /// so the start() that ends the retune does not seed them again (see set_rate()).
    bool tx_ring_seeded_ = false;
    /// Where IDF's TX writer stands inside its current descriptor, in frames: every write
    /// fills descriptors in order from the start of one (start() leaves it at a boundary),
    /// so this is everything written since start() modulo DMA_FRAMES.
    uint32_t tx_pos_frames_ = 0;
    // What the receive channel is doing with the audio PLL, tracked here because the I2S
    // driver's own two answers disagree after a failure and the disagreement is fatal.
    /// The width the channel is actually configured for, which after a peer's announcement
    /// is no longer necessarily the one board.json opened it with.
    uint8_t slot_bits_ = 16;
    /// The slave's bclk_div as last configured: 8 from init (I2S_STD_CLK_DEFAULT_CONFIG), then
    /// whatever set_format() chose. Needed to know which transitions IDF will accept.
    uint32_t slave_bclk_div_ = 8;

    // See set_rate(): the reference is dropped before the call that can fail, and the
    // stored clock source is only updated after it succeeds.
    bool rx_cfg_apll_ = false;   ///< the source the channel's stored config names
    bool rx_apll_ref_ = false;   ///< whether a reference on the PLL is actually held

    /// How many holders the audio PLL has at this instant, as this file's bookkeeping knows
    /// it, or 0 when it cannot honestly say. IDF exposes no such counter, and the number is
    /// what decides whether a retune of the PLL is permitted, silently ignored, or fatal.
    int apll_refs() const;

    int32_t trim_ppm_ = 0;       ///< where the clock sits relative to rate_; reset by set_rate
    bool    trim_warned_ = false;///< the refusal is said once per retune, not once per block
public:
    /// What the receive-DMA interrupt counts. Public only so the file-scope ISR callbacks
    /// can name it; nothing outside this driver touches it.
    struct RxEvents {
        std::atomic<uint32_t> overflows{0}, blocks{0};
        std::atomic<uint32_t> hw_rx_fifo_ovf{0}, hw_tx_fifo_udf{0}, hw_rx_hung{0};
        int port = -1;   ///< whose I2S receive FIFO the hung bit is read from
    };
private:
    RxEvents rx_events_;
    bool line_input_ = true;     ///< set_line_input(): MCLK driven and RX enabled while running
    modesp::audio::LinkPins link_{};  ///< set_link_master(): the peer's pins this port clocks, or none
    bool rx_enabled_ = false;    ///< what i2s_channel_enable/disable last did to rx_
    std::atomic<uint32_t> tx_ovf_{0};
    std::atomic<uint32_t> rx_short_{0}, tx_short_{0};
    std::atomic<uint32_t> trim_attempts_{0}, trim_applied_{0};
};
