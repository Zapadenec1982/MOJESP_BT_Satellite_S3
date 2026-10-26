**English** · [Українська](S3_PORT.uk.md)

# ESP32-S3 port (headless)

The same player on an **ESP32-S3 N16R8** (16 MB quad flash, 8 MB octal PSRAM in the package) with
**no display**: internet radio, DLNA, AirPlay 2, Bluetooth through the satellite, USB audio and the
microSD card, the DSP chain and the web UI — controlled from the web, MQTT and an optional rotary
encoder. The board is `boards/s3_player`; the P4 panel (`boards/p4_player`) builds from the same
tree exactly as before.

Developed and checked on an ESP32-S3-DevKitC-1-style board (ESP32-S3 rev v0.2, "Embedded PSRAM
8MB (AP_3v3)", 16 MB flash, a CH343 USB-UART on the "COM" connector and the native USB on the
"USB" connector).

## What is different from the P4

| | P4 panel | S3 board |
|---|---|---|
| Wi-Fi | ESP32-C6 over SDIO (ESP-Hosted) | the S3's own radio |
| Display, touch | 480×800 MIPI-DSI, GT911 — `ui_lvgl` | none — `ui_headless` (below) |
| Audio clock | APLL, exact; trimmed to follow the satellite | 160 MHz PLL through the fractional divider: exact for 44.1/48/88.2/96 kHz at 256×; never trimmed |
| USB audio | high speed, 44.1–96 kHz | full speed, **44.1 and 48 kHz**, 16 and 24 bit |
| Console, flashing | USB-Serial/JTAG | **UART0, the "COM" connector** — the "USB" connector is the sound card |
| Cover art | decoded to a 280 px tile (JPEG engine + PPA) | kept as the file; the browser decodes it |
| Bluetooth pairing | confirmed on the panel | the satellite accepts on its own |
| Partition table | `partitions_p4_16mb.csv` | `partitions_s3_16mb.csv` (no `slave_fw`, table at 0x8000) |
| Lock file | `dependencies.lock` | `dependencies.lock.esp32s3` |

## Wiring

Pins as on the DevKitC-1 headers; every one of them can move — they are data in
`boards/s3_player/board.json`, not code.

| GPIO | Signal | To | Header |
|---|---|---|---|
| 5 | I2S0 BCK | PCM5102A BCK | J1 |
| 6 | I2S0 WS | PCM5102A LRCK | J1 |
| 7 | I2S0 DOUT | PCM5102A DIN | J1 |
| 4 | `amp_enable` | TPA3255 /RESET and PCM5102A XSMT; 10 kΩ pull-down, HIGH = playing | J1 |
| 15 | I2S-B BCLK | satellite GPIO13 | J1 |
| 16 | I2S-B WS | satellite GPIO26 | J1 |
| 17 | I2S-B DATA, in | satellite GPIO27 | J1 |
| 18 | UART1 TX | satellite GPIO16 (RX) | J1 |
| 8 | UART1 RX | satellite GPIO17 (TX) | J1 |
| 9, 10, 11, 12, 13, 14 | microSD D2, D3, CMD, CLK, D0, D1 | the card, in its own pin order | J1 |
| 1, 2 | encoder A, B | encoder (to GND, internal pull-ups) | J3 |
| 42 | encoder button | button to GND | J3 |
| 19, 20 | USB D−, D+ | the "USB" connector: UAC2 sound card | — |
| 43, 44 | UART0 TX, RX | the "COM" connector: console, flashing | — |

- **PCM5102A:** SCK → GND (its PLL locks to BCK; no MCLK is wired), FMT → GND, DEMP → GND, XSMT ← GPIO4.
- **Satellite:** a common ground. From link protocol 1.9 the S3 clocks the I2S line (GPIO15/16 are
  outputs then, driven from I2S0) and the stream plays bit-exact; an older satellite clocks it
  itself and gets a rebuffer every few minutes, as on the P4 (`MODESP_AUDIO_BT_DIRECT_CONTINUOUS`,
  [AUDIO.md](AUDIO.md)). GPIO17 feeds both I2S0 RX and I2S1 RX: one wire, two readers.
- **microSD:** 4-bit at 20 MHz. Fit 10 kΩ pull-ups on CMD and D0–D3 — the internal ones are too
  weak at that speed (the driver says so in the log when a card mounts only at 400 kHz). FAT32.
- **Not used, on purpose:** 0, 3, 45, 46 (strapping), 26–32 (flash), 33–37 (octal PSRAM),
  38 and 48 (the DevKit's RGB LED, depending on its revision).

## Building and flashing

ESP-IDF v5.5.5, target `esp32s3`. The S3 builds in its own directory with its own sdkconfig, so a
P4 build can live next to it:

```bash
idf.py -B build_s3 -D SDKCONFIG=build_s3/sdkconfig set-target esp32s3
idf.py -B build_s3 -D SDKCONFIG=build_s3/sdkconfig build
idf.py -B build_s3 -D SDKCONFIG=build_s3/sdkconfig -p /dev/ttyACM0 flash monitor
```

- Flash and monitor over the **COM** connector (`/dev/ttyACM0` with a CH343, `/dev/ttyUSB0` with a
  CP2102). Once the firmware starts the sound card, the USB connector is no longer the chip's
  USB-Serial/JTAG; to flash through it, hold BOOT while pressing RESET.
- `generated/`, `data/` and `managed_components/` are shared by both builds. A configure of the
  other board rewrites them (and the component manager removes the other target's components),
  and the build that comes next re-runs its own configure before it compiles
  (`generated/project_info.h` is a configure input), so **alternating** P4 and S3 builds is safe.
  Two builds running **at the same time** are not: use a second worktree for that.
- The first start has no Wi-Fi credentials: the player opens an access point `ModESP-XXXX`
  (open, the last two bytes of the MAC); join it, open `http://192.168.4.1` and set the network
  on the Wi-Fi page (System → Wi-Fi), as on the P4.

## What runs differently, and why

### Memory

The S3 has 512 KB of SRAM against the P4's 768 KB, and the firmware's static data alone filled
what was left of it. Four settings make it fit (all in `boards/s3_player/sdkconfig.board`):

- **Code and read-only data from PSRAM** (`CONFIG_SPIRAM_XIP_FROM_PSRAM`). Besides speed, it keeps
  the cache on through flash writes — IDF takes a mutex instead of stopping the cores — so
  interrupts and tasks that are not in IRAM keep running while NVS or `eq.json` is written.
- **Static data in PSRAM** (`CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` and
  `main/psram_bss.lf`): the control plane — SharedState, the services, the module objects, the
  HTTP handlers' buffers — 148 KB of it. The audio module's object stays internal: the audio task
  touches it every block.
- **FreeRTOS, heap and ring-buffer code out of IRAM** (safe for the reason above), and IDF's default
  caches (32 KB data, 16 KB instruction).
- **A 40 ms I2S ring** (4 × 10 ms) instead of the P4's 80 ms: the margin was for a flash erase
  holding the cores, which does not happen here (`drivers/i2s_std`, `TX_DMA_DESCS`).

Measured on the board: 71 KB of internal heap free after start (22 KB before the last two steps),
4.6 MB of PSRAM free.

### Audio clock

There is no audio PLL on the S3. The master runs from the 160 MHz PLL through the I2S fractional
divider, which is exact at 256× for every rate the player outputs (44.1, 48, 88.2, 96 kHz); a
retune reads back the MCLK actually made and refuses one more than 50 ppm off (IDF's divider search
overflows at 176.4 kHz — the output stops at 96 kHz anyway). The satellite input in 32-bit slots
fits on the same 160 MHz without the master's help, so the P4's "raise the master's MCLK so the
slave fits" plan does not exist here, and the BT port parks at 48 kHz. `can_trim()` is false: the
audio core takes its resampler where it would have trimmed the clock (`components/audio_dsp/
include/i2s_clock.h`, `drivers/i2s_std`).

### CPU: the DSP chain

The same chain costs the S3 (240 MHz Xtensa) far more than the P4 (360 MHz RISC-V), and the first
measurement said why: 33 % of a core at 48 kHz, and per stage, the two limiters took 1 400 of the
1 650 cycles a frame while the filters took ~55 cycles a section. The limiters' loops call
`fmaxf`/`fminf` five and six times a frame; on RISC-V each is one instruction, but the S3's FPU has
no max or min, so each was a call into libm. `dsp_max`/`dsp_min` (`components/audio_dsp/include/
dsp_kernels.h`) are a compare and a conditional move on Xtensa (`olt.s`, `movt.s`) and stay
`fmaxf`/`fminf` everywhere else, so the P4 and the host tests run exactly what they ran.

| 48 kHz, subsonic + loudness + bass limiter + brickwall, no EQ bands | average | worst block |
|---|---|---|
| as ported | 33–38 % | 53–88 % |
| hot DSP in IRAM (`main/dsp_iram.lf`) | 32–33 % | 51–55 % |
| and `dsp_max`/`dsp_min` | 17 % | 24 % |
| and one filter section a pass | **~16 %** | — |

At 96 kHz that is about a third of a core. One section a pass: two SVF sections need more than
the 16 FP registers Xtensa has, and the pair spilled to the stack (~52 cycles a section against
~35 alone); the output is bit-identical either way and the P4 keeps its pairs (`DspChain::paired_`).
Per stage now, in cycles a stereo frame: subsonic ~90, loudness ~150, bass limiter ~400,
brickwall ~105. Ready-made alternatives were compared and none is cheaper on the S3 at this
quality (Espressif's `esp_audio_effects` is int-only, binary-only and slower per filter; esp-dsp's
biquad runs plain-FPU code on the S3 at ~17 cycles a sample a section, no faster than the SVF, and
is the Direct Form whose noise the SVF replaced). The DSP's per-block functions sit in IRAM on the S3
(`main/dsp_iram.lf`): out of the 16 KB instruction cache the two cores share, they do not wait on
PSRAM while Wi-Fi runs on the other core.

### USB audio

Full speed has one 1023-byte isochronous packet a millisecond, which 96 kHz/24-bit fits — but the
S3's OTG controller has 1 KB of FIFO RAM for all endpoints, and TinyUSB 0.21 sizes the receive FIFO
for two packets of the largest one. At 96 kHz that does not fit, and TinyUSB enumerates anyway with
a FIFO too small for its own packets. So the S3 offers 44.1 and 48 kHz, 16 and 24 bit; a
compile-time check holds the arithmetic (`components/usb_uac`). Feedback is 16.16 per 1 ms frame,
what TinyUSB sends for UAC2 and what Windows' UAC2 driver expects (per TinyUSB's notes). Only a
Linux host has been tried; the USB spec's full-speed format is 10.14 in three bytes, and macOS is
the host most likely to hold a device to it — check there before relying on it (the fallback would
be a UAC1 configuration at full speed, as TinyUSB's own example does).

### The UI module: `ui_headless`

`ui_lvgl` owns more than the glass: the web's Player picture (`ui.cover`, `GET /api/cover`), the
Library (`GET /api/library`), the web's colour scheme and default language (`ui.theme`, `ui.lang`)
and the encoder. `modules/ui_headless` keeps all of that without LVGL — same keys, same routes, so
the web UI is the same file — and `project.json`'s board overlay puts it in `ui_lvgl`'s place on
the S3 (`boards.s3_player`: `exclude_modules`, `add_modules`; `tools/generate_ui.py --board`).

The encoder without a screen:

| | |
|---|---|
| turn | volume, 2 % a detent; a turn also ends a mute |
| press | play/pause of whatever is playing, in that source's own commands (card, radio, DLNA, Bluetooth, AirPlay) |
| hold (650 ms) | mute on/off |

### Cover art

The cover is kept as the file that arrived, JPEG or PNG (up to 512 KB), and `GET /api/cover` sends
it as it is; the browser decodes and crops it. Its colour for the Player's background is worked out
from an eighth-scale decode by the ROM's TJpgDec (baseline JPEG; none for PNG or progressive).
`CONFIG_COVER_DECODE_TILE` selects this (`components/cover`, `src/cover_bytes.cpp`); the P4 keeps
decoding tiles.

### Names

The player calls itself **S3 Player** — on Bluetooth, AirPlay, DLNA and as the USB sound card —
until it is renamed (`project.json`, `boards.s3_player.system.device_name`; generated into
`generated/project_info.h`).

## Checked on the board (25.10.2026)

- Boots, all modules start, main loop at 100 Hz; Wi-Fi radio up (access point visible).
- USB: enumerates as a UAC2 device at full speed (Linux: 44.1/48 kHz, S16_LE and S32_LE/24 bit);
  48 kHz/16-bit, 44.1 kHz/16-bit and 48 kHz/24-bit played with the source switching to USB by
  itself, the output retuning between rates (clock stopped 130–230 µs), every frame arriving
  (48000/48000, 44100/44100) and no late audio blocks.
- On the home network (26.10.2026; Wi-Fi station, RSSI −46 dBm, 7 h up without a restart):
  - DLNA, `tools/dlna_acceptance.py --renderer-only --rate-switches` against the renderer: SOAP
    faults, SetNext/Next/Previous, automatic advancement, end of track, metadata, Pause/Resume/
    Stop, GENA events, 44.1 ↔ 48 kHz switches both ways — all pass. Its "ranged GET after a Seek"
    check does not: the firmware refuses to seek a WAV over HTTP on purpose (radio_module.cpp,
    seek_stream), and the fixtures are WAV.
  - AirPlay 1 (RAOP), `tools/airplay_send.py` with a quiet tone and a JPEG cover: RECORD,
    metadata, progress and artwork accepted, the source switches itself to AirPlay, no underruns,
    no resend requests, ui.cover and its colour published; three sessions in a row return the
    memory they took. The AirPlay 2 half (pairing, PTP, buffered AAC) needs a real Apple sender.
  - Internet radio over HTTPS, pushed through DLNA: Radio France FIP as MP3 128 kbit/s and as AAC
    192 kbit/s — TLS, decoding and buffering hold (buffer 50–60 %, no underruns), the DSP at
    11.5 % of a core, 43 KB of internal RAM free while TLS runs.
- Not connected at the time, so not checked: the DAC and amplifier, the satellite, the microSD
  card, the encoder; MQTT was not exercised.

The first AirPlay session found one more S3 limit: upstream creates its receiver with a 12 KB
internal-RAM stack, and with Wi-Fi up the S3 had no 12 KB in one piece — RECORD answered 500. On
the S3 the upstream files' tasks now have PSRAM stacks (`components/airplay2/port/redirect.h`;
safe there because XIP from PSRAM keeps the cache on through flash writes), created and deleted
with IDF's `...WithCaps` calls; the upstream files stay byte-identical.
