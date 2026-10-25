#include "mic_input.h"
#include "sdkconfig.h"

#if defined(CONFIG_MODESP_BOARD_P4_PLAYER)

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include <atomic>

namespace mic_input {
namespace {

constexpr const char* TAG = "MicInput";
constexpr unsigned BLOCK = 480;                 // frames per DMA buffer: 10 ms
i2s_chan_handle_t rx = nullptr;
bool rx_enabled = false;
const audio_codec_ctrl_if_t* ctrl = nullptr;
const audio_codec_if_t* codec = nullptr;
std::atomic<uint32_t> overflow{0};
uint32_t overflow_seen = 0;

bool IRAM_ATTR on_overflow(i2s_chan_handle_t, i2s_event_data_t*, void*) {
    overflow.fetch_add(1, std::memory_order_relaxed);
    return false;
}

int find_es8311(i2c_master_bus_handle_t bus) {
    // Everest ES8311 datasheet rev10: CE selects 0x18/0x19; read-only identity FD=0x83, FE=0x11.
    // Nothing is written before the identity matches.
    for (int addr = 0x18; addr <= 0x19; ++addr) {
        if (i2c_master_probe(bus, addr, 50) != ESP_OK) continue;
        i2c_device_config_t dc{};
        dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dc.device_address = addr;
        dc.scl_speed_hz = 400000;
        i2c_master_dev_handle_t dev = nullptr;
        if (i2c_master_bus_add_device(bus, &dc, &dev) != ESP_OK) continue;
        uint8_t hi = 0, lo = 0, reg = 0xfd;
        const esp_err_t a = i2c_master_transmit_receive(dev, &reg, 1, &hi, 1, 100);
        reg = 0xfe;
        const esp_err_t b = i2c_master_transmit_receive(dev, &reg, 1, &lo, 1, 100);
        i2c_master_bus_rm_device(dev);
        if (a == ESP_OK && b == ESP_OK && hi == 0x83 && lo == 0x11) return addr;
    }
    return -1;
}

}  // namespace

bool open(i2c_master_bus_handle_t bus, unsigned gain_db) {
    close();
    if (!bus) return false;
    const int addr = find_es8311(bus);
    if (addr < 0) { ESP_LOGE(TAG, "no ES8311 at 0x18/0x19"); return false; }
    // docs/CONCEPT.md, "On-board pins": the onboard NS4150 stays off. The external amplifier
    // (GPIO34) is the audio module's; never touched here.
    if (gpio_set_level(GPIO_NUM_11, 0) != ESP_OK || gpio_set_direction(GPIO_NUM_11, GPIO_MODE_OUTPUT) != ESP_OK)
        return false;
    i2s_chan_config_t ch = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_2, I2S_ROLE_MASTER);
    ch.dma_desc_num = 8;
    ch.dma_frame_num = BLOCK;
    if (i2s_new_channel(&ch, nullptr, &rx) != ESP_OK) { rx = nullptr; return false; }
    i2s_std_config_t cfg{};
    cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE);
    // Rev 1.3 has no PLL160 for I2S; XTAL's fractional divider gives 48 kHz on average and takes
    // no APLL reference, so the DAC's clock can move on its own.
    cfg.clk_cfg.clk_src = I2S_CLK_SRC_XTAL;
    cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    cfg.gpio_cfg.mclk = GPIO_NUM_13;
    cfg.gpio_cfg.bclk = GPIO_NUM_12;
    cfg.gpio_cfg.ws = GPIO_NUM_10;
    cfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
    cfg.gpio_cfg.din = GPIO_NUM_48;
    bool ok = i2s_channel_init_std_mode(rx, &cfg) == ESP_OK;
    i2s_event_callbacks_t cb{};
    cb.on_recv_q_ovf = on_overflow;
    ok = ok && i2s_channel_register_event_callback(rx, &cb, nullptr) == ESP_OK;
    if (ok) {
        audio_codec_i2c_cfg_t i2c{};
        i2c.port = 0;
        i2c.addr = addr << 1;                       // esp_codec_dev wants the 8-bit address
        i2c.bus_handle = bus;
        ctrl = audio_codec_new_i2c_ctrl(&i2c);
        ok = ctrl != nullptr;
    }
    if (ok) {
        es8311_codec_cfg_t cc{};
        cc.ctrl_if = ctrl;
        cc.codec_mode = ESP_CODEC_DEV_WORK_MODE_ADC;
        cc.pa_pin = -1;
        cc.use_mclk = true;
        cc.no_dac_ref = true;
        cc.mclk_div = 256;
        codec = es8311_codec_new(&cc);
        ok = codec != nullptr;
    }
    if (ok) {
        esp_codec_dev_sample_info_t fs{};
        fs.sample_rate = RATE;
        fs.bits_per_sample = 16;
        fs.channel = 2;                             // Philips stereo framing; MIC1 is the left slot
        // esp_codec_dev 1.4.0's enable rewrites the gain, so the gain goes in after it.
        const unsigned g = gain_db > 30 ? 30 : (gain_db / 6) * 6;
        ok = codec->set_fs(codec, &fs) == 0 && codec->enable(codec, true) == 0 &&
             codec->set_mic_gain(codec, (float)g) == 0 && (!codec->mute_mic || codec->mute_mic(codec, false) == 0);
    }
    ok = ok && i2s_channel_enable(rx) == ESP_OK;
    if (!ok) { ESP_LOGE(TAG, "ES8311 at 0x%02x: start failed", addr); close(); return false; }
    rx_enabled = true;
    overflow_seen = overflow.load();
    ESP_LOGI(TAG, "ES8311 at 0x%02x, I2S2 XTAL %u Hz/16 bit, MIC1 gain %u dB", addr, RATE, gain_db);
    return true;
}

bool read(float* out, size_t n, uint32_t timeout_ms, float* peak) {
    if (!rx_enabled) return false;
    int16_t buf[BLOCK * 2];
    float pk = 0.0f;
    size_t done = 0;
    while (done < n) {
        const size_t want = (n - done) < BLOCK ? (n - done) : BLOCK;
        size_t got = 0;
        if (i2s_channel_read(rx, buf, want * 4, &got, timeout_ms) != ESP_OK || got != want * 4) return false;
        for (size_t i = 0; i < want; i++) {
            const float v = (float)buf[2 * i] * (1.0f / 32768.0f);
            out[done + i] = v;
            const float a = v < 0.0f ? -v : v;
            if (a > pk) pk = a;
        }
        done += want;
    }
    if (peak) *peak = pk;
    const uint32_t o = overflow.load();
    const bool whole = o == overflow_seen;
    overflow_seen = o;
    return whole;
}

void discard(uint32_t ms) {
    if (!rx_enabled) return;
    int16_t buf[BLOCK * 2];
    for (uint32_t t = 0; t < ms; t += 10) {
        size_t got = 0;
        if (i2s_channel_read(rx, buf, sizeof(buf), &got, 100) != ESP_OK) break;
    }
    overflow_seen = overflow.load();
}

void close() {
    // The codec first, while its clocks still run; neither amplifier is touched.
    if (codec) { audio_codec_delete_codec_if(codec); codec = nullptr; }
    if (ctrl) { audio_codec_delete_ctrl_if(ctrl); ctrl = nullptr; }
    if (rx) {
        if (rx_enabled) i2s_channel_disable(rx);
        i2s_del_channel(rx);
        rx = nullptr;
    }
    rx_enabled = false;
}

}  // namespace mic_input

#else   // another board: no verified wiring, no microphone

namespace mic_input {
bool open(i2c_master_bus_handle_t, unsigned) { return false; }
bool read(float*, size_t, uint32_t, float*) { return false; }
void discard(uint32_t) {}
void close() {}
}  // namespace mic_input

#endif
