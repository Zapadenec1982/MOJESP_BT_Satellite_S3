/**
 * @file hal_types.h
 * @brief HAL system limits, config structs, and resource types
 *
 * All data structures used by the HAL layer:
 *   - BoardConfig:  parsed from board.json at boot
 *   - BindingTable: parsed from bindings.json at boot
 *   - Resource structs: initialized hardware handles
 *
 * Zero heap allocation — all containers are fixed-size ETL.
 */

#pragma once

#include "etl/string.h"
#include "etl/vector.h"
#include "etl/array.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

namespace modesp {

// ═══════════════════════════════════════════════════════════════
// System limits
// ═══════════════════════════════════════════════════════════════

static constexpr size_t MAX_RELAYS         = 8;
static constexpr size_t MAX_ONEWIRE_BUSES  = 4;
static constexpr size_t MAX_ADC_CHANNELS   = 8;
static constexpr size_t MAX_PWM_CHANNELS   = 8;
static constexpr size_t MAX_ENCODERS       = 2;
static constexpr size_t MAX_BINDINGS       = 24;
static constexpr size_t MAX_SENSORS        = 8;
static constexpr size_t MAX_ACTUATORS      = 8;
static constexpr size_t MAX_I2C_BUSES      = 2;
static constexpr size_t MAX_SDMMC_SLOTS    = 1;
static constexpr size_t MAX_I2C_EXPANDERS  = 4;
static constexpr size_t MAX_I2C_DISPLAYS   = 2;
static constexpr size_t MAX_DSI_DISPLAYS   = 1;   // MIPI-DSI panel (ESP32-P4 has one DSI host)
static constexpr size_t MAX_I2C_TOUCH      = 1;   // touch controller on an I2C bus
static constexpr size_t MAX_UART_BUSES     = 2;   // UART0 — консоль; лишаються 1/2
static constexpr size_t MAX_I2S_BUSES      = 2;   // player: I2S-A duplex (DAC+ADC) + I2S-B slave (BT); P4 has 3 controllers
static constexpr size_t MAX_EXPANDER_IOS   = 16;   // Outputs + Inputs через expanders
static constexpr size_t MAX_REMOTE_DEVICES = 16;   // remote devices (any transport): board.json factory seed + runtime /data/devices.json subscriptions (merged)
static constexpr size_t MAX_BLE_DEVICES    = MAX_REMOTE_DEVICES;   // legacy alias (transitional)
static constexpr size_t MAX_BINDING_SETTINGS = 6;  // per-binding driver settings

// ═══════════════════════════════════════════════════════════════
// String types for IDs and names
// ═══════════════════════════════════════════════════════════════

/// Board-level identifier length. A module that resolves a binding id into its own buffer
/// sizes it from here (see BindingTable::hardware_for).
static constexpr size_t MAX_HAL_ID_LEN = 16;
using HalId         = etl::string<MAX_HAL_ID_LEN>;
using Role          = etl::string<16>;
using DriverType    = etl::string<16>;
using ModuleName    = etl::string<16>;
using SensorAddress = etl::string<24>;  // "28:FF:AA:BB:CC:DD:EE:01"

// ═══════════════════════════════════════════════════════════════
// Board config structs (parsed from board.json)
// ═══════════════════════════════════════════════════════════════

struct GpioOutputConfig {
    HalId id;
    gpio_num_t gpio;
    bool active_high;
};

struct OneWireBusConfig {
    HalId id;
    gpio_num_t gpio;
};

struct GpioInputConfig {
    HalId id;
    gpio_num_t gpio;
    bool pull_up;       // true = pull-up, false = pull-down
    bool invert = false; // true = NC contact / active-low (door, limit switch)
};

struct AdcChannelConfig {
    HalId id;
    gpio_num_t gpio;
    uint8_t atten;      // 0=0dB, 2=2.5dB, 6=6dB, 11=11dB (0-3.3V)
};

/// Інкрементальний поворотний енкодер: два піни квадратури (CLK=A, DT=B).
/// Кнопка енкодера (за наявності) описується ОКРЕМО як gpio_input і
/// прив'язується драйвером digital_input — енкодер сюди не входить.
struct EncoderConfig {
    HalId id;               // "enc_0"
    gpio_num_t clk;         // канал A
    gpio_num_t dt;          // канал B
    bool pull_up = true;    // true = pull-up (типово для механічних енкодерів)
};

struct I2CBusConfig {
    HalId id;               // "i2c_0"
    gpio_num_t sda;
    gpio_num_t scl;
    uint32_t freq_hz;       // 100000 (100kHz standard)
};

/// A microSD / TF slot on the SDMMC peripheral.
///
/// Pins are configuration, not constants, for a specific reason: the ESP32-P4 has ONE SDMMC
/// controller with two slots, ESP-Hosted already owns one of them for the C6, and the card
/// slot's wiring on this panel (docs/CONCEPT.md «microSD») was recorded as "verify against the
/// schematic before initialising". Putting it in board.json means correcting it later is an
/// edit to a data file rather than to firmware, and means a wrong guess fails loudly at mount
/// instead of being baked in.
struct SdmmcSlotConfig {
    HalId      id;                  // "sd_0"
    uint8_t    slot      = 0;       // SDMMC slot index; ESP-Hosted uses the other one
    uint8_t    width     = 4;       // 1 or 4 data lines
    gpio_num_t clk       = GPIO_NUM_NC;
    gpio_num_t cmd       = GPIO_NUM_NC;
    gpio_num_t d0        = GPIO_NUM_NC;
    gpio_num_t d1        = GPIO_NUM_NC;
    gpio_num_t d2        = GPIO_NUM_NC;
    gpio_num_t d3        = GPIO_NUM_NC;
    gpio_num_t cd        = GPIO_NUM_NC;   // card detect, -1 when not routed
    uint32_t   freq_khz  = 20000;         // conservative until the wiring is confirmed
    /// Internal LDO channel powering the socket, or 0 for none. This board already runs the
    /// MIPI PHY from channel 3, so a card slot fed the same way is plausible — but which
    /// channel, and at what voltage, has to come from the schematic. Left at 0 until then:
    /// driving an unknown LDO channel at a guessed voltage can damage whatever it feeds.
    uint8_t    ldo_chan  = 0;
    uint16_t   ldo_mv    = 3300;
    HalId      mount_point;               // "/sd"
};

struct I2CExpanderConfig {
    HalId id;               // "relay_exp" або "input_exp"
    HalId bus_id;           // "i2c_0"
    HalId chip;             // "pcf8574"
    uint8_t address;        // 0x24
    uint8_t pin_count;      // 8
};

struct I2CExpanderOutputConfig {
    HalId id;               // "relay_1" (hardware_id для bindings)
    HalId expander_id;      // "relay_exp"
    uint8_t pin;            // 0-7
    bool active_high;       // false для KC868-A6 (relay active-LOW)
};

struct I2CExpanderInputConfig {
    HalId id;               // "din_1"
    HalId expander_id;      // "input_exp"
    uint8_t pin;            // 0-7
    bool invert;            // true для KC868-A6 (opto-isolated active-LOW)
};

/// I²C-дисплей (multi-address пристрій на шині). На відміну від expander —
/// чіп сам володіє своїми адресами (банками), HAL лише надає bus_handle.
/// Бекенд (driver у bindings) обирає, як саме керувати чіпом.
struct I2CDisplayConfig {
    HalId   id;             // "disp_0" (hardware_id для bindings)
    HalId   bus_id;         // "i2c_0" — посилання на i2c_buses
    HalId   chip;           // "amt630a"
    uint8_t cols;           // символьна сітка OSD
    uint8_t rows;
    int8_t  cal_x = 0;      // overscan-калібровка OSD: піксельний зсув по X (per-panel hardware-конфіг)
    int8_t  cal_y = 0;      // overscan-калібровка OSD: піксельний зсув по Y
};

/// MIPI-DSI panel (ESP32-P4). Config-only, like I2CDisplayConfig: HAL stores the
/// geometry/timing/pins from board.json, the lcd driver (drivers/dsi_st7701) owns the
/// DSI bus, DPI panel, PHY LDO, reset line and backlight PWM. Timings are board facts —
/// only from the panel datasheet / vendor BSP (docs/CONCEPT.md «Референсні проєкти»),
/// never guessed.
///
/// NO DEFAULTS for the timings, and that is the point. They used to hold the JC4880P443C's
/// numbers — 480x800, hsync 12, vfp 166, 2 lanes at 500 Mbps — in a header shared by every
/// board. A second panel whose board.json forgot a line would have been driven at this
/// panel's timings and shown a rolling or torn picture, with nothing in the log naming the
/// missing field. They are required in board.json now (config_service refuses an entry that
/// omits one), and a zero here is only ever a parse that never ran.
struct DsiDisplayConfig {
    HalId      id;                          // "lcd_0" (hardware_id for bindings)
    HalId      chip;                        // "st7701"
    uint8_t    lanes            = 0;        // DSI data lanes
    uint16_t   lane_mbps        = 0;        // lane bit rate, Mbps
    uint16_t   dpi_mhz          = 0;        // DPI pixel clock, MHz
    uint16_t   h_res            = 0;
    uint16_t   v_res            = 0;
    uint16_t   hsync            = 0;        // hsync pulse width, px
    uint16_t   hbp              = 0;        // horizontal back porch
    uint16_t   hfp              = 0;        // horizontal front porch
    uint16_t   vsync            = 0;        // vsync pulse width, lines
    uint16_t   vbp              = 0;
    uint16_t   vfp              = 0;
    uint8_t    color_bits       = 0;        // 16 = RGB565, 24 = RGB888
    gpio_num_t reset_gpio       = GPIO_NUM_NC;
    gpio_num_t backlight_gpio   = GPIO_NUM_NC;
    bool       backlight_active_high = true;
    uint32_t   backlight_pwm_hz = 20000;
    int8_t     ldo_chan         = -1;       // MIPI PHY LDO channel (P4: 3), -1 = not used
    uint16_t   ldo_mv           = 0;        // required when ldo_chan >= 0; a panel fact
    uint16_t   rotation         = 0;        // UI rotation in degrees: 0/90/180/270. The DPI panel
                                            // cannot swap axes itself (esp_lcd_panel_swap_xy is
                                            // unsupported), so LVGL rotates — on the P4 through
                                            // the PPA. Touch is rotated separately by the touch
                                            // controller's own swap/mirror flags.
};

/// Capacitive touch controller on a board I2C bus (GT911 etc.). Config-only: the
/// touch driver resolves the bus via find_i2c_bus() and owns its device handle.
struct I2CTouchConfig {
    HalId      id;                          // "touch_0" (hardware_id for bindings)
    HalId      bus_id;                      // "i2c_0"
    HalId      chip;                        // "gt911"
    // No default: 0x5D is the GT911's ADDR-low address, and a board that strapped it high
    // answers at 0x14. Guessing here made one controller's wiring every board's.
    uint8_t    address   = 0;
    gpio_num_t int_gpio  = GPIO_NUM_NC;     // -1 = not wired (polling)
    gpio_num_t rst_gpio  = GPIO_NUM_NC;
    uint16_t   x_max     = 0;               // 0 = take from the bound panel
    uint16_t   y_max     = 0;
    bool       swap_xy   = false;
    bool       mirror_x  = false;
    bool       mirror_y  = false;
};

/// UART-шина (серійний потоковий пристрій: радар присутності, PM-сенсор тощо).
/// HAL встановлює драйвер UART і володіє портом; driver лише читає байти.
/// port: номер периферії UART (0 — консоль, тож лишаються 1/2).
struct UartBusConfig {
    HalId      id;                     // "uart_1" (hardware_id для bindings)
    int        port      = 1;          // UART_NUM_1 / UART_NUM_2
    gpio_num_t tx        = GPIO_NUM_NC; // MCU TX → пристрій RX (-1 якщо не підключено)
    gpio_num_t rx        = GPIO_NUM_NC; // MCU RX ← пристрій TX
    uint32_t   baud_rate = 256000;     // LD2410 за замовчуванням 256000 8N1
    uint16_t   rx_buffer = 256;        // RX ring buffer (мін. 256 > HW FIFO)
};

/// I²S-шина (аудіо-вихід: MAX98357A Class-D amp тощо). На відміну від UART,
/// HAL зберігає лише конфіг (піни + sample_rate + SD-пін) — фактичний I2S-канал
/// створює драйвер, бо handle (i2s_chan_handle_t) разом із DMA-буферами та
/// фоновим audio-таском належить драйверу (як ADC oneshot). HAL лишається без
/// залежності від driver/i2s_std.h.
/// player-fork: full-duplex (dout + din on one controller = one clock domain), MCLK
/// output, slave role (external master such as a BT module) and slot width — all
/// config-only, the audio driver builds the channels.
struct I2SBusConfig {
    HalId      id;                       // "i2s_0" (hardware_id для bindings)
    int8_t     port           = -1;           // controller index (I2S_NUM_x); -1 = by order
    bool       master         = true;         // false = slave (BCLK/WS are inputs)
    gpio_num_t mclk           = GPIO_NUM_NC;  // master clock out (ADC SCKI 256fs); -1 = none
    gpio_num_t bclk           = GPIO_NUM_NC;  // bit clock (BCLK/SCK)
    gpio_num_t ws             = GPIO_NUM_NC;  // word select (LRCLK/WS)
    gpio_num_t dout           = GPIO_NUM_NC;  // serial data → DIN підсилювача / DAC
    gpio_num_t din            = GPIO_NUM_NC;  // serial data ← ADC / external source; -1 = TX only
    gpio_num_t sd             = GPIO_NUM_NC;  // SD/shutdown (hardware-mute), -1 якщо підтягнутий
    uint32_t   sample_rate_hz = 16000;        // частота дискретизації (аларм-тони: 16k достатньо)
    uint8_t    slot_bits      = 16;           // slot width: 16 or 32 (24-bit DAC/ADC data in 32-bit slots)
    uint16_t   mclk_multiple  = 256;          // MCLK = mclk_multiple × fs (PCM1808 SCKI: 256fs); slave: ≥ 8 × slot_bits × 2
};

/// Remote device (off-board sensor/actuator reached over a transport). HAL зберігає лише
/// transport-тег + identity-blob — config-only, як I2CDisplayConfig. Декод/кеш живе у
/// транспортному компоненті (напр. modesp_ble BleCentral), а драйвер читає його через свій
/// hardware_type. Транспорт-генерик за задумом: "ble" сьогодні, "lora"/"mqtt"/"espnow" далі.
struct RemoteDeviceConfig {
    HalId           id;                 // "ble_xiaomi_bthome" (hardware_id для bindings)
    etl::string<8>  transport = "ble";  // transport-тег; "ble" сьогодні, "lora"/"mqtt"/"espnow" далі
    etl::string<40> identity;           // transport identity blob (BLE observer MAC "a4:c1:38:b4:dc:11"); empty for connect
    etl::string<24> name;               // connect device (panel) adv-name prefix; empty for observers
};
using BleDeviceConfig = RemoteDeviceConfig;   // legacy alias (transitional)

struct BoardConfig {
    etl::string<24> board_name;
    etl::string<8>  board_version;
    etl::vector<GpioOutputConfig, MAX_RELAYS>                     gpio_outputs;
    etl::vector<OneWireBusConfig, MAX_ONEWIRE_BUSES>              onewire_buses;
    etl::vector<GpioInputConfig, MAX_ADC_CHANNELS>                gpio_inputs;
    etl::vector<AdcChannelConfig, MAX_ADC_CHANNELS>               adc_channels;
    etl::vector<EncoderConfig, MAX_ENCODERS>                      encoders;
    etl::vector<I2CBusConfig, MAX_I2C_BUSES>                      i2c_buses;
    etl::vector<I2CExpanderConfig, MAX_I2C_EXPANDERS>             i2c_expanders;
    etl::vector<I2CExpanderOutputConfig, MAX_EXPANDER_IOS>        expander_outputs;
    etl::vector<I2CExpanderInputConfig, MAX_EXPANDER_IOS>         expander_inputs;
    etl::vector<I2CDisplayConfig, MAX_I2C_DISPLAYS>               i2c_displays;
    etl::vector<DsiDisplayConfig, MAX_DSI_DISPLAYS>               dsi_displays;
    etl::vector<SdmmcSlotConfig, MAX_SDMMC_SLOTS>                 sdmmc_slots;
    etl::vector<I2CTouchConfig, MAX_I2C_TOUCH>                    i2c_touch;
    etl::vector<UartBusConfig, MAX_UART_BUSES>                   uart_buses;
    etl::vector<I2SBusConfig, MAX_I2S_BUSES>                     i2s_buses;
    etl::vector<RemoteDeviceConfig, MAX_REMOTE_DEVICES>         remote_devices;
};

// ═══════════════════════════════════════════════════════════════
// Binding config (parsed from bindings.json)
// ═══════════════════════════════════════════════════════════════

/// One per-binding driver setting (e.g. "beta" → 3900). Every driver setting
/// is numeric and fits exactly in a float (ints up to 2^24), so a single float
/// store covers int and float settings alike.
struct BindingSetting {
    etl::string<16> key;
    float           value = 0.0f;
};

struct Binding {
    HalId         hardware_id;
    Role          role;
    DriverType    driver_type;
    ModuleName    module_name;
    SensorAddress address;      // Опціональна ROM адреса / канал (multi-sensor)
    etl::vector<BindingSetting, MAX_BINDING_SETTINGS> settings;  // per-binding driver config

    /// Look up a per-binding setting by key; returns `def` when not present.
    float setting_or(const char* key, float def) const {
        for (const auto& s : settings) {
            if (s.key == key) return s.value;
        }
        return def;
    }
};

struct BindingTable {
    etl::vector<Binding, MAX_BINDINGS> bindings;

    /// The hardware id bound to (module, role), or nullptr.
    ///
    /// This is how a module finds its hardware without knowing what the board called it.
    /// The alternative, and what was here, is a literal: find_uart_bus("uart_sat"),
    /// find_sdmmc_slot("sd_0"), find_i2s_bus("i2s_b"). Those names are BOARD vocabulary, and
    /// a module that spells one has quietly decided which board it runs on — a second board
    /// that calls its card slot anything else gets a module that silently does nothing.
    /// bindings.json is where that mapping already lives for every driver-backed peripheral.
    /// Copied into `out` rather than returned by reference: this table is reparsed when
    /// bindings are uploaded, while a module keeps the answer it got in on_bind() until the
    /// next boot. `out` is always NUL-terminated; false, and `out` emptied, when there is no
    /// such binding. Give it MAX_HAL_ID_LEN + 1 bytes.
    bool hardware_for(const char* module, const char* role, char* out, size_t out_sz) const {
        if (!out || out_sz == 0) return false;
        out[0] = 0;
        for (const auto& b : bindings) {
            if (b.module_name == module && b.role == role) {
                const char* src = b.hardware_id.c_str();
                size_t i = 0;
                for (; src[i] && i < out_sz - 1; ++i) out[i] = src[i];
                out[i] = 0;
                return true;
            }
        }
        return false;
    }
};

// ═══════════════════════════════════════════════════════════════
// HAL resources (initialized hardware)
// ═══════════════════════════════════════════════════════════════

struct GpioOutputResource {
    HalId id;
    gpio_num_t gpio;
    bool active_high;
    bool initialized = false;
};

struct OneWireBusResource {
    HalId id;
    gpio_num_t gpio;
    bool initialized = false;
};

struct GpioInputResource {
    HalId id;
    gpio_num_t gpio;
    bool pull_up;
    bool invert = false;   // NC contact / active-low
    bool initialized = false;
};

struct AdcChannelResource {
    HalId id;
    gpio_num_t gpio;
    uint8_t atten;
    bool initialized = false;
};

struct EncoderResource {
    HalId id;
    gpio_num_t clk;
    gpio_num_t dt;
    bool pull_up = true;
    bool initialized = false;
};

struct I2CBusResource {
    HalId id;
    i2c_master_bus_handle_t bus_handle = nullptr;
    uint32_t freq_hz = 100000;   // швидкість шини (per-device scl_speed_hz)
    bool initialized = false;
};

struct I2CExpanderResource {
    HalId id;
    i2c_master_dev_handle_t dev_handle = nullptr;
    uint8_t address     = 0;
    uint8_t pin_count   = 8;
    uint8_t output_state = 0xFF;   // Все HIGH = все OFF (PCF8574 power-on default)
    bool initialized    = false;

    /// Записати output_state в PCF8574 (один I2C byte write)
    bool write_state();
    /// Прочитати всі 8 входів з PCF8574 (один I2C byte read)
    bool read_state(uint8_t& input_byte);
};

struct UartBusResource {
    HalId      id;
    int        port      = 1;        // uart_port_t (зберігаємо як int — без важкого include)
    gpio_num_t tx        = GPIO_NUM_NC;  // a driver that has to ask the pin what it sees
    gpio_num_t rx        = GPIO_NUM_NC;  // needs to know which pin that is
    uint32_t   baud_rate = 256000;
    bool       initialized = false;
    /// The driver's event queue: a `QueueHandle_t`, held as void* for the same reason
    /// `port` is held as int — this header is public, and freertos/queue.h is not free
    /// (it is also not mocked in the host build, which compiles this file).
    ///
    /// A consumer that blocks on it is told when data arrives instead of polling, and —
    /// the reason it exists — is told about UART_FIFO_OVF and UART_BUFFER_FULL. Without
    /// the queue the driver drops those bytes in silence: the reader sees a frame that
    /// never completes and has no way to tell a lost byte from a partner that stopped
    /// talking. Null when the driver was installed without one.
    void*      rx_events = nullptr;
};

/// I²S-ресурс: лише конфіг (піни + sample_rate + SD). Канал створює драйвер —
/// HAL не тримає i2s_chan_handle_t, тож не тягне driver/i2s_std.h у hal_types.h.
struct I2SBusResource {
    HalId      id;
    int8_t     port           = 0;
    bool       master         = true;
    gpio_num_t mclk           = GPIO_NUM_NC;
    gpio_num_t bclk           = GPIO_NUM_NC;
    gpio_num_t ws             = GPIO_NUM_NC;
    gpio_num_t dout           = GPIO_NUM_NC;
    gpio_num_t din            = GPIO_NUM_NC;
    gpio_num_t sd             = GPIO_NUM_NC;
    uint32_t   sample_rate_hz = 16000;
    uint8_t    slot_bits      = 16;
    uint16_t   mclk_multiple  = 256;
    bool       initialized    = false;
};

} // namespace modesp
