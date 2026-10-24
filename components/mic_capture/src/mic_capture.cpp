#include "mic_capture.h"
#include "mic_capture_buffer.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

// Where IDF registers the USB Serial/JTAG VFS (usb_serial_jtag_vfs.c): /dev/usbserjtag when it
// is the console, /dev/secondary when it only copies a UART console.
#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
#define MIC_USB_CONSOLE "/dev/usbserjtag"
#elif defined(CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG)
#define MIC_USB_CONSOLE "/dev/secondary"
#endif
#if !defined(CONFIG_MODESP_BOARD_P4_PLAYER) || !defined(MIC_USB_CONSOLE)
#error "Mic capture requires the verified p4_player wiring and a USB Serial/JTAG console"
#endif

namespace mic_capture {
namespace {
constexpr unsigned RATE = 48000, BLOCK = 480, PRE = RATE / 2, CAPACITY = RATE * 2;
constexpr const char* TAG = "MicCapture";
i2c_master_bus_handle_t bus;
i2s_chan_handle_t rx;
const audio_codec_ctrl_if_t* ctrl;
const audio_codec_if_t* codec;
bool rx_enabled;
int16_t* storage;
Buffer* capture;
// All buffer and hardware ownership stays on mic_capture task. ISR counters are
// lock-free; the audio marker uses a bounded critical section, never the PSRAM pointer.
std::atomic<bool> awaiting{false};
std::atomic<uint32_t> received{0}, overflow{0}, marker{0}, marker_ms{0};
std::atomic<uint32_t> generation{0};
portMUX_TYPE marker_lock = portMUX_INITIALIZER_UNLOCKED;
uint32_t processed, started_ms, capture_ms, captured_marker_ms;
unsigned gain_db = 18, capture_gain = 18;
const char* capture_codec = "unknown";
bool active;
static_assert(std::atomic<uint32_t>::is_always_lock_free, "ISR counters must be lock-free");

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
bool IRAM_ATTR on_receive(i2s_chan_handle_t, i2s_event_data_t* event, void*) {
    received.fetch_add(event->size / 4, std::memory_order_relaxed);
    return false;
}
bool IRAM_ATTR on_overflow(i2s_chan_handle_t, i2s_event_data_t*, void*) {
    overflow.fetch_add(1, std::memory_order_relaxed);
    return false;
}

void stop_hardware() {
    portENTER_CRITICAL(&marker_lock);
    awaiting.store(false);
    generation.fetch_add(1, std::memory_order_relaxed);
    portEXIT_CRITICAL(&marker_lock);
    active = false;
    // Codec shutdown while clocks still run; never control either amplifier.
    if (codec) { audio_codec_delete_codec_if(codec); codec = nullptr; }
    if (ctrl) { audio_codec_delete_ctrl_if(ctrl); ctrl = nullptr; }
    if (rx) {
        if (rx_enabled) i2s_channel_disable(rx);
        i2s_del_channel(rx);
        rx = nullptr;
    }
    rx_enabled = false;
}

bool init_hardware() {
    // The four strap addresses are from Espressif es7210_reg.h (8-bit 0x80..86).
    // Discover, don't assume the missing board strap sheet. Ambiguity is a failure.
    int address = -1, found = 0;
    for (int candidate = 0x40; candidate <= 0x43; ++candidate) {
        if (i2c_master_probe(bus, candidate, 50) == ESP_OK) { address = candidate; ++found; }
    }
    bool use_es8311 = false;
    if (found == 0) {
        // Some Guition module revisions expose ES8311 ADC on the schematic net
        // named ES7210_SDOUT. Require its read-only identity BEFORE any setup.
        // Everest ES8311 datasheet rev10: CE selects0x18/19; FD=83, FE=11.
        for (int candidate = 0x18; candidate <= 0x19; ++candidate) {
            if (i2c_master_probe(bus, candidate, 50) != ESP_OK) continue;
            i2c_device_config_t dc{};
            dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
            dc.device_address = candidate;
            dc.scl_speed_hz = 400000;
            i2c_master_dev_handle_t device = nullptr;
            if (i2c_master_bus_add_device(bus, &dc, &device) != ESP_OK) continue;
            uint8_t hi = 0, lo = 0, reg = 0xfd;
            const auto a = i2c_master_transmit_receive(device, &reg, 1, &hi, 1, 100);
            reg = 0xfe;
            const auto b = i2c_master_transmit_receive(device, &reg, 1, &lo, 1, 100);
            i2c_master_bus_rm_device(device);
            ESP_LOGI(TAG, "codec identity at0x%02x: %02x %02x", candidate, hi, lo);
            if (a == ESP_OK && b == ESP_OK && hi == 0x83 && lo == 0x11) {
                address = candidate; ++found; use_es8311 = true;
            }
        }
    }
    if (found != 1) {
        ESP_LOGE(TAG, "expected one supported microphone codec, found %d", found);
        return false;
    }
    capture_codec = use_es8311 ? "ES8311" : "ES7210";
    capture_gain = use_es8311 ? (gain_db / 6) * 6 : gain_db;
    // CONCEPT section4: onboard NS4150 must remain disabled. External amplifier
    // uses GPIO34 and belongs to AudioModule; never change that pin here.
    if (gpio_set_level(GPIO_NUM_11, 0) != ESP_OK ||
        gpio_set_direction(GPIO_NUM_11, GPIO_MODE_OUTPUT) != ESP_OK) return false;
    ESP_LOGI(TAG, "%s at0x%02x; I2S2 XTAL, 48000Hz/16bit, MIC1 gain %udB", capture_codec, address, capture_gain);
    i2s_chan_config_t ch = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_2, I2S_ROLE_MASTER);
    ch.dma_desc_num = 6;
    ch.dma_frame_num = BLOCK;
    if (i2s_new_channel(&ch, nullptr, &rx) != ESP_OK) return false;
    i2s_std_config_t cfg{};
    cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE);
    // Rev1.3 has no PLL160 for I2S. XTAL fractional divider gives nominal48k,
    // with NO APLL reference, allowing the external DAC to retune independently.
    cfg.clk_cfg.clk_src = I2S_CLK_SRC_XTAL;
    cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    cfg.gpio_cfg.mclk = GPIO_NUM_13;
    cfg.gpio_cfg.bclk = GPIO_NUM_12;
    cfg.gpio_cfg.ws = GPIO_NUM_10;
    cfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
    cfg.gpio_cfg.din = GPIO_NUM_48;
    if (i2s_channel_init_std_mode(rx, &cfg) != ESP_OK) return false;
    i2s_event_callbacks_t callbacks{};
    callbacks.on_recv = on_receive;
    callbacks.on_recv_q_ovf = on_overflow;
    if (i2s_channel_register_event_callback(rx, &callbacks, nullptr) != ESP_OK) return false;
    audio_codec_i2c_cfg_t i2c{};
    i2c.port = 0;
    i2c.addr = address << 1; // esp_codec_dev uses 8-bit addresses, IDF probe uses7.
    i2c.bus_handle = bus;
    ctrl = audio_codec_new_i2c_ctrl(&i2c);
    if (!ctrl) return false;
    if (use_es8311) {
        es8311_codec_cfg_t cc{};
        cc.ctrl_if = ctrl;
        cc.codec_mode = ESP_CODEC_DEV_WORK_MODE_ADC;
        cc.pa_pin = -1;
        cc.use_mclk = true;
        cc.no_dac_ref = true;
        cc.mclk_div = 256;
        codec = es8311_codec_new(&cc);
    } else {
        es7210_codec_cfg_t cc{};
        cc.ctrl_if = ctrl;
        cc.mic_selected = ES7210_SEL_MIC1;
        cc.mclk_src = ES7210_MCLK_FROM_PAD;
        cc.mclk_div = 256;
        codec = es7210_codec_new(&cc);
    }
    if (!codec) return false;
    esp_codec_dev_sample_info_t fs{};
    fs.sample_rate = RATE;
    fs.bits_per_sample = 16;
    fs.channel = 2; // Philips stereo framing, keep MIC1's left slot only.
    // v1.4.0 enable rewrites gains, so apply the requested gain AFTER enable.
    if (codec->set_fs(codec, &fs) != 0 || codec->enable(codec, true) != 0 ||
        codec->set_mic_gain(codec, capture_gain) != 0) return false;
    if (codec->mute_mic && codec->mute_mic(codec, false) != 0) return false;
    received.store(0); overflow.store(0); marker.store(0); marker_ms.store(0);
    if (i2s_channel_enable(rx) != ESP_OK) return false;
    rx_enabled = true;
    return true;
}

void status() {
    const char* state = !capture ? "idle" :
        capture->state() == State::ready ? "ready" :
        capture->state() == State::triggered ? "triggered" :
        active ? (awaiting.load() ? "armed" : "warming") : "idle";
    printf("@MIC_STATUS state=%s samples=%u pre=%u rate=%u gain=%u next_gain=%u overflow=%lu marker_ms=%lu armed_ms=%lu codec=%s\n",
           state, capture ? unsigned(capture->size()) : 0, capture ? unsigned(capture->pre_count()) : 0,
           RATE, capture && capture->state() == State::ready ? capture_gain : gain_db, gain_db,
           (unsigned long)overflow.load(), (unsigned long)captured_marker_ms, (unsigned long)capture_ms, capture_codec);
}

void arm() {
    stop_hardware();
    if (!storage) storage = static_cast<int16_t*>(heap_caps_malloc(CAPACITY * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    // Buffer object itself has static lifetime; never allocate in the audio task.
    static Buffer buffer(nullptr, CAPACITY, PRE);
    if (!storage) { ESP_LOGE(TAG, "capture allocation failed"); return; }
    buffer = Buffer(storage, CAPACITY, PRE);
    capture = &buffer;
    capture->reset();
    captured_marker_ms = capture_ms = 0;
    if (!init_hardware()) { stop_hardware(); ESP_LOGE(TAG, "initialization failed; playback retained"); return; }
    processed = 0;
    started_ms = now_ms();
    active = true;
    ESP_LOGI(TAG, "warming for1s, then arming for next playback start (45s timeout)");
}

void dump() {
    if (!capture || capture->state() != State::ready || active) { status(); return; }
    WavHeader header{};
    if (!make_wav_header(capture->size(), header)) return;
    const auto spans = capture->spans();
    const size_t total = 44 + capture->size() * 2;
    printf("@MIC_BEGIN bytes=%u pre=%u marker_ms=%lu armed_ms=%lu rate=%u gain=%u codec=%s\n",
           unsigned(total), unsigned(capture->trigger_index()), (unsigned long)captured_marker_ms,
           (unsigned long)capture_ms, RATE, capture_gain, capture_codec);
    uint32_t checksum = 2166136261u;
    constexpr char hex[] = "0123456789abcdef";
    for (size_t offset = 0; offset < total; offset += 128) {
        char line[257];
        size_t n = total - offset; if (n > 128) n = 128;
        for (size_t i = 0; i < n; ++i) {
            const size_t pos = offset + i;
            uint8_t b;
            if (pos < 44) b = header[pos];
            else {
                const size_t sample = (pos - 44) / 2;
                const int16_t value = sample < spans[0].size ? spans[0].data[sample] : spans[1].data[sample - spans[0].size];
                b = static_cast<uint16_t>(value) >> (((pos - 44) & 1) * 8);
            }
            checksum = (checksum ^ b) * 16777619u;
            line[2*i] = hex[b >> 4]; line[2*i+1] = hex[b & 15];
        }
        line[2*n] = 0;
        printf("@MIC_DATA %u %s\n", unsigned(offset), line);
        vTaskDelay(1); // Frozen capture: never block acquisition on UART/Wi-Fi/SD.
    }
    printf("@MIC_END fnv1a=%08lx\n", (unsigned long)checksum);
}

void command(const char* text) {
    if (strcmp(text, "mic arm") == 0) arm();
    else if (strcmp(text, "mic status") == 0) status();
    else if (strcmp(text, "mic trigger") == 0) { mark_start(); status(); }
    else if (strcmp(text, "mic dump") == 0) dump();
    else if (strcmp(text, "mic cancel") == 0) { stop_hardware(); if (capture) capture->reset(); status(); }
    else if (strncmp(text, "mic gain ", 9) == 0) {
        char* end = nullptr; const long value = strtol(text+9, &end, 10);
        if (!active && end != text+9 && *end == 0 && value >= 0 && value <= 30 && value % 3 == 0) {
            gain_db = value; status();
        } else ESP_LOGW(TAG, "gain needs idle capture and 0..30dB in steps of3");
    }
}

void task(void*) {
    // IDF5.5.5 nonblocking VFS reads count bytes in the driver's RX ring; the
    // boot console's polling-only backend otherwise reports zero forever.
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t usb{};
        usb.tx_buffer_size = 1024;
        usb.rx_buffer_size = 256;
        if (usb_serial_jtag_driver_install(&usb) != ESP_OK) {
            ESP_LOGE(TAG, "USB RX driver unavailable"); vTaskDelete(nullptr); return;
        }
    }
    usb_serial_jtag_vfs_use_driver();
    const int fd = open(MIC_USB_CONSOLE, O_RDONLY | O_NONBLOCK);
    if (fd < 0) { ESP_LOGE(TAG, "USB console unavailable"); vTaskDelete(nullptr); return; }
    char line[64]; size_t used = 0; bool discard = false;
    int16_t stereo[BLOCK * 2], mono[BLOCK];
    ESP_LOGI(TAG, "idle; USB commands: mic arm/status/trigger/dump/cancel/gain N");
    for (;;) {
        char input[64]; const int count = read(fd, input, sizeof(input));
        for (int i = 0; i < count; ++i) {
            const char c = input[i];
            if (c == '\r' || c == '\n') {
                if (!discard && used) { line[used] = 0; command(line); }
                used = 0; discard = false;
            } else if (used < sizeof(line)-1 && !discard) line[used++] = c;
            else discard = true;
        }
        if (!active) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        size_t bytes = 0;
        const esp_err_t result = i2s_channel_read(rx, stereo, sizeof(stereo), &bytes, 100);
        if (result != ESP_OK || bytes != sizeof(stereo) || overflow.load() || now_ms() - started_ms > 45000) {
            ESP_LOGE(TAG, "capture invalid: read=%s bytes=%u overflows=%lu elapsed=%lu", esp_err_to_name(result),
                     unsigned(bytes), (unsigned long)overflow.load(), (unsigned long)(now_ms()-started_ms));
            stop_hardware(); capture->reset(); status(); continue;
        }
        processed += BLOCK;
        if (processed <= RATE) {
            if (processed == RATE) {
                capture->arm(); capture_ms = now_ms();
                portENTER_CRITICAL(&marker_lock);
                awaiting.store(true, std::memory_order_release);
                portEXIT_CRITICAL(&marker_lock);
                ESP_LOGI(TAG, "ARMED: switch a track; 0.5s history +1.5s after start");
            }
            continue;
        }
        for (unsigned i = 0; i < BLOCK; ++i) mono[i] = stereo[2*i];
        capture->push(mono, BLOCK);
        const uint32_t target = marker.load(std::memory_order_acquire);
        if (target && processed >= target && capture->state() == State::armed) {
            capture->trigger(); captured_marker_ms = marker_ms.load();
            // Mark is quantized to next DMA block (<=10ms), not sample-exact
            // alignment with external DAC; acoustic propagation adds delay too.
        }
        if (capture->state() == State::ready) {
            stop_hardware();
            int peak = 0; unsigned clipped = 0;
            for (auto span : capture->spans()) for (size_t i = 0; i < span.size; ++i) {
                const int v = span.data[i]; const int a = v < 0 ? -v : v;
                if (a > peak) peak = a;
                if (v == 32767 || v == -32768) ++clipped;
            }
            ESP_LOGI(TAG, "READY: peak=%d clipped=%u; mic dump exports WAV", peak, clipped);
            status();
        }
    }
}
} // namespace

void mark_start() {
    const uint32_t session = generation.load(std::memory_order_acquire);
    if (!awaiting.load(std::memory_order_acquire)) return;
    // Serialize claim/publication with cancel/rearm. No I/O or allocation and
    // only a few instructions under this lock; an old marker cannot cross sessions.
    portENTER_CRITICAL(&marker_lock);
    if (session == generation.load(std::memory_order_relaxed) &&
        awaiting.exchange(false, std::memory_order_acq_rel)) {
        marker_ms.store(now_ms(), std::memory_order_relaxed);
        marker.store(received.load(std::memory_order_relaxed) + BLOCK, std::memory_order_release);
    }
    portEXIT_CRITICAL(&marker_lock);
}

void start(i2c_master_bus_handle_t handle) {
    static bool started = false;
    if (started || !handle) return;
    bus = handle;
    started = xTaskCreatePinnedToCore(task, "mic_capture", 6144, nullptr, 6, nullptr, 0) == pdPASS;
    if (!started) ESP_LOGE(TAG, "task allocation failed");
}
} // namespace mic_capture
