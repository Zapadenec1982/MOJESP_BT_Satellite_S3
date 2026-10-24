# ModESP Streamer

Firmware for a network stereo streamer with DSP, built on the **Guition JC4880P443C** panel
(ESP32-P4 with an ESP32-C6 for Wi-Fi, 4.3" 480×800 touch screen). It plays internet radio,
DLNA, AirPlay 2, Bluetooth, USB audio from a computer and files from microSD, runs them
through a 32-bit float DSP chain and drives an external DAC and class-D amplifier.

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5.5-blue?logo=espressif)](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus)](https://isocpp.org/)
[![License](https://img.shields.io/badge/License-PolyForm%20Noncommercial-blue)](LICENSE)

A personal project: one unit, built for one listening room and used every day. It is
published as a reference for anyone building something similar on the ESP32-P4. It is not a
product and comes with no support.

## Sources

| Source | What it does |
|---|---|
| Internet radio | Station catalogue from [Radio Browser](https://www.radio-browser.info) (country, genre, search), presets on the panel and in the web UI; MP3, AAC/HE-AAC and FLAC streams. The playing track's cover is looked up by its title |
| DLNA / UPnP | Media renderer for Music Assistant, Home Assistant and other control points, and a browser for media servers on the panel |
| AirPlay 2 | From an iPhone or a Mac; AirPlay 2 or classic RAOP profile ([docs/AIRPLAY_MODES.md](docs/AIRPLAY_MODES.md)) |
| Bluetooth | Through a separate ESP32 satellite board over I2S and a UART link ([ModESP_BT_Satellite](https://github.com/Zapadenec1982/ModESP_BT_Satellite)): SBC, AAC, aptX, aptX HD; covers and track info over AVRCP |
| USB audio | A UAC2 sound card for a computer: 44.1 to 96 kHz, 16 or 24 bit, asynchronous with feedback, no driver needed on Windows 10 1703+, macOS or Linux |
| microSD | FLAC, MP3, WAV and AAC/M4A from folders, with tags and embedded covers |

One source plays at a time. A source that starts playing takes over only when the selected
one is silent.

## Sound

- **The source's own sample rate, up to 96 kHz.** The output clock follows the source instead
  of converting everything to one rate. Only asynchronous sources (Bluetooth) go through the
  ASRC ([dbry/audio-resampler](https://github.com/dbry/audio-resampler)).
- **10-band parametric EQ** with ready-made sound presets and the user's own set; filters can
  be imported as REW or Equalizer APO text.
- **Loudness compensation** fitted to the ISO 226:2003 equal-loudness contours.
- **Subsonic filter** with bass-port protection, a **bass limiter** and a look-ahead
  **brickwall limiter** in front of the amplifier.
- **Spectrum display** on the panel.
- Planned with a second DAC: bi-amp, 2.1 and 4.0 output modes. The data model is already in
  `data/eq.json`; the DSP for it is not.

## Control

- The panel: LVGL 9 touch UI, plus a rotary encoder with a button.
- A web UI served by the panel (Svelte), with the same pages and DSP settings.
- Ukrainian, English, German and Polish, on the panel and on the web.
- MQTT with Home Assistant discovery is in the firmware and off by default. Home Assistant
  sees the player through DLNA and Music Assistant.

## Hardware

| Part | Role |
|---|---|
| Guition JC4880P443C | ESP32-P4 (2× RISC-V 360 MHz, 32 MB PSRAM, 16 MB flash) and ESP32-C6 (Wi-Fi 6) over SDIO with ESP-Hosted; 4.3" 480×800 IPS on ST7701S over MIPI-DSI; GT911 touch; microSD; USB HS and FS |
| PCM5102A | Stereo DAC on I2S; the P4 is the clock master, no MCLK |
| TPA3255 | Class-D amplifier |
| ESP32 Bluetooth satellite | A2DP sink, separate firmware and repository |
| Rotary encoder | Volume and navigation |

Pinout, signal path and the reasons behind them: [docs/CONCEPT.md](docs/CONCEPT.md) §3–5 and
[docs/AUDIO.md](docs/AUDIO.md). The board configuration is
[`boards/p4_player/board.json`](boards/p4_player/board.json).

Things worth knowing about this board:

- **The panel's ESP32-P4 is chip revision v1.3.** Components that require revision 3.0 do not
  run on it. `esp_audio_codec` is pinned below 2.6 for that reason.
- **Both USB ports' VBUS lines are tied together without a diode.** When the panel runs from its
  own 5 V supply, a computer plugged into the HS port is a second 5 V source on the same rail.
  Power the panel from that port, or put a Schottky diode in its VBUS line.
- **The C6 needs the ESP-Hosted slave firmware** from the same release as the host library
  (3.0.7): [docs/C6_UPDATE.md](docs/C6_UPDATE.md), `tools/c6_updater/`.

## Building

ESP-IDF **v5.5.5**, target `esp32p4`.

```bash
git clone --recursive https://github.com/Zapadenec1982/ModESP_Streamer.git
cd ModESP_Streamer
. "$IDF_PATH/export.sh"                    # or the EIM activation script for v5.5.5
pip install -r tools/requirements.txt      # once, into the IDF Python environment
idf.py set-target esp32p4
idf.py build
idf.py -p <port> flash
```

- A full build needs a lot of memory. On a 16 GB machine `ninja -C build -j4` is safer than
  all cores at once.
- On Windows, point `IDF_COMPONENT_CACHE_PATH` at a short path: esp_hosted 3.0.7 does not
  unpack into the default cache (paths over 260 characters).
- The built web UI is committed in `data/www/`. To rebuild it: `cd webui && npm ci && npm run deploy`.

Host tests (pytest plus a C++ doctest runner; the C++ part needs `g++` and `cmake`):

```bash
python tools/run_host_tests.py
python tools/run_host_tests.py --no-cpp    # Python suite only
```

### Flash layout

There are two LittleFS partitions (`partitions_p4_16mb.csv`):

| Partition | Holds | `idf.py flash` |
|---|---|---|
| `data` at 0x430000 | `board.json`, `bindings.json`, `ui.json`, the web UI, the factory `eq.json` | rewritten from `data.bin` |
| `userdata` at 0xF80000 | the user's EQ, stations, paired devices, the track to resume | never written |

`idf.py app-flash` writes only the application. A change to `board.json` reaches the panel
with a full `idf.py flash`, because it ships in `data.bin`. `board.json` has to stay under
4096 bytes (`ConfigService::MAX_JSON_SIZE`); the build refuses a larger one.

### On a running panel

- Logging ships at WARN. To open it up until the next reboot:
  `curl -X POST http://<panel>/api/loglevel -d '{"tag":"*","level":"info"}'`.
- The console is the P4's USB-Serial/JTAG, and DTR is its reset line: `idf.py monitor` restarts
  the panel each time it attaches. To watch a running panel, open the port with DTR and RTS
  both cleared (on Linux in one `TIOCMBIC` call, before anything else touches the lines).

## Architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│  PRODUCT MODULES                                                      │
│  ui_lvgl · audio (core, DSP) · player (microSD) · radio · dlna        │
│  airplay · bt (satellite link) · usb_audio · storage                  │
├──────────────────────────────────────────────────────────────────────┤
│  ModESP FRAMEWORK (control plane)                                     │
│  SharedState · PersistService · NVS · HAL · drivers                   │
│  Wi-Fi (ESP-Hosted → C6) · HTTP (REST) · WebSocket · MQTT · OTA       │
│  code generator (manifests → UI, headers, i18n) · Svelte web UI       │
├──────────────────────────────────────────────────────────────────────┤
│  ESP-IDF v5.5.5 · FreeRTOS · ESP32-P4                                 │
└──────────────────────────────────────────────────────────────────────┘
```

The framework owns state and commands through SharedState; the web UI, the panel and MQTT all
read and write the same keys. The audio core is a task on core 1 that takes commands through a
lock-free queue and publishes only slow state back: no sample crosses the event bus. The
file, radio, DLNA, AirPlay and USB sources share one PCM ring, owned by one producer at a
time; Bluetooth arrives on its own I2S input.

## Layout

```
main/                 app entry
components/           framework (modesp_core, modesp_services, modesp_hal, modesp_net,
                      modesp_mqtt), audio_dsp, audio_pipe, audio_resampler, airplay2,
                      esp_dlna, dlna_client, radio_catalog, cover, usb_uac, sat_link,
                      user_store, playlist, stations, …
modules/              product modules, selected in project.json
drivers/              dsi_st7701, gt911, i2s_std, rotary_encoder, sdmmc_card, uart_link, …
boards/p4_player/     board.json, bindings.json, sdkconfig.board
webui/                Svelte 4 web UI (built into data/www/)
tools/                code generator, font and icon builds, EQ fitting, host test runner
tools/c6_updater/     one-shot P4 image that flashes the ESP-Hosted firmware onto the C6
tools/sdio_soak/      SDIO soak test through the C6
tests/host/           C++ host tests (doctest)
docs/                 design documents and bench records
```

Most documents in `docs/` are in Ukrainian. Start with [docs/CONCEPT.md](docs/CONCEPT.md);
`docs/bench/` holds the measurements that settled decisions.

## Upstream

The control plane is a fork of [ModESP_Framework](https://github.com/Zapadenec1982/ModESP_Framework),
a manifest-driven ESP32 framework (JSON manifests → code generator → firmware, web UI and MQTT
topics). Fixes it needed for the ESP32-P4 are kept separable for upstream.

## License

This project's own code is source-available under the
[PolyForm Noncommercial License 1.0.0](LICENSE).

Third-party code in this tree and in the built images keeps its own licence. Among it: the
AirPlay 2 receiver core in `components/airplay2/` (its author's non-commercial licence),
Espressif's components (Apache-2.0; ESPRESSIF MIT and Espressif Modified MIT, which allow use
with Espressif chips only), LVGL, TinyUSB, libsodium and littlefs. The inventory is
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md); the licence texts a binary release ships with
are in [THIRD_PARTY_LICENSES.txt](THIRD_PARTY_LICENSES.txt).
