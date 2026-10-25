# Third-party code in ModESP Streamer

What this repository carries, and what goes into the firmware image (`modesp_p4_player.bin`)
and the web UI (`data.bin`), where it comes from and the licence **as found in the files**,
on 2026-10-25, before the repository was opened. This is an inventory of facts for whoever has
to decide what they mean; it is not legal advice.

This project's own code is under the [PolyForm Noncommercial License 1.0.0](LICENSE). **Nothing
listed below is**: every body of third-party code keeps its own licence, and a file with a
licence notice of its own keeps it, changes made here included. The texts themselves are in
[THIRD_PARTY_LICENSES.txt](THIRD_PARTY_LICENSES.txt), generated from the files named there by
`tools/third_party_licenses.py`.

"In the image" below is checked, not assumed: against the symbols of the ELF built from
`dependencies.lock`, not only against what the build compiles.

## In this repository

| Body of code | Where in this tree | Licence as found in the files | Notes |
|---|---|---|---|
| AirPlay 2 receiver core, [rbouteiller/airplay-esp32](https://github.com/rbouteiller/airplay-esp32) at `811d5f8` | `components/airplay2/upstream/` byte-identical to upstream; `components/airplay2/port/` adapted copies and this project's glue | `components/airplay2/LICENSE`: "Non-Commercial License, Copyright (c) 2026 Remi Bouteiller" — use, copy, modify and distribute "for non-commercial purposes only"; commercial use needs the holder's written permission | `components/airplay2/UPSTREAM.md` lists what was taken and every change. `modules/airplay` is its only user; without both, the firmware builds and AirPlay is not offered. Upstream carries the AirPort Express RSA key (`rtsp/rtsp_rsa.c`) and FairPlay setup tables (`rtsp/rtsp_fairplay.c`) that other open-source receivers also publish |
| esp_dlna, from [espressif/esp-adf](https://github.com/espressif/esp-adf) `release/v2.x`, `examples/advanced_examples/dlna` | `components/esp_dlna/` | File headers: "ESPRESSIF MIT License, Copyright (c) 2019 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>", "Permission is hereby granted for use on all ESPRESSIF SYSTEMS products" | Fetched 2026-09-18. Four changes, each marked `p4-player:`, listed in `components/esp_dlna/README.md` |
| audio-resampler, [dbry/audio-resampler](https://github.com/dbry/audio-resampler) | `components/audio_resampler/` | `license.txt`: BSD-3-Clause, "Copyright (c) 2006 - 2025 David Bryant" | Master of 2026-03. Local changes are marked `MODESP:` in `resampler.c` and `resampler.h` |
| stb_image 2.30, [nothings/stb](https://github.com/nothings/stb) | `components/cover/src/stb_image.h` | End of file: MIT, "Copyright (c) 2017 Sean Barrett", or public domain (Unlicense), at the user's choice | Unchanged |
| jsmn, [zserge/jsmn](https://github.com/zserge/jsmn) | `components/jsmn/jsmn.h` | File header: MIT, "Copyright (c) 2010 Serge Zaitsev" | Unchanged |
| Embedded Template Library 20.40.1, [ETLCPP/etl](https://github.com/ETLCPP/etl) | `components/etl/etl`, a git submodule | `LICENSE`: MIT; headers: "Copyright(c) 2014 John Wellbelove" | Referenced by commit, fetched by `git submodule update --init` |
| ST7701S init sequence | `drivers/dsi_st7701/src/dsi_st7701_driver.cpp` | Apache-2.0, "SPDX-FileCopyrightText: 2024 Espressif Systems", as in [csvke/esp32_p4_jc4880p433c_bsp](https://github.com/csvke/esp32_p4_jc4880p433c_bsp) `src/bsp_display.c` | Register values copied unchanged; the file says so where they are |
| Roboto, glyphs | `modules/ui_lvgl/src/fonts/ui_font_{300,400,500,600}_*.c` | SIL OFL 1.1, "Copyright 2011 The Roboto Project Authors" (`@fontsource/roboto` 5.x) | Rasterised by `tools/fonts/build_fonts.py` with `lv_font_conv`; the source fonts are not in the tree |
| Material Symbols Rounded, glyphs | `modules/ui_lvgl/src/fonts/ui_font_icons_*.c`; a woff2 subset inline in `webui/src/styles/icons.css` | Apache-2.0, [google/material-design-icons](https://github.com/google/material-design-icons) at `27e9ef1` (`tools/fonts/symbols.lock`) | The woff2 keeps the font's name table, copyright and licence entries included |
| Svelte 4.2.20 runtime | `data/www/*.js.gz`, the built web UI | `LICENSE.md`: MIT, "Copyright (c) 2016-23 [these people](https://github.com/sveltejs/svelte/graphs/contributors)" | Compiled into the bundle by rollup; minification drops the comment, so the text travels in `THIRD_PARTY_LICENSES.txt` |
| C6 co-processor updater | `tools/c6_updater/` | Apache-2.0, "SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD" | From esp_hosted 3.0.7 `examples/ota/coprocessor_ota/mcu_host`. Files changed here carry `SPDX-FileContributor: 2026 Zapadenec1982`, the notice Apache-2.0 §4(b) asks of modified files. A separate one-shot image, not the player firmware |
| doctest | `tests/host/doctest.h` | File header: "Copyright (c) 2016-2023 Viktor Kirilov ... Distributed under the MIT Software License"; the `LICENSE.txt` it points to is not in the tree, the MIT text is in `THIRD_PARTY_LICENSES.txt` | Host tests only |
| EQ preset curves | `components/audio_dsp/src/eq_presets.cpp`, from `tools/eq/fit_presets.py` | — | Numeric settings: Winamp's presets as Audacious ships them, Winamp's band frequencies as VLC's `equalizer_presets.h` lists them, refitted here onto ten peaking bands. No code taken |
| Deezer API reply | `tests/host/fixtures/radio_catalog/deezer_*.json` | — | A captured reply of Deezer's public search API, kept as a parser fixture |

## Fetched at build time

From the [ESP Component Registry](https://components.espressif.com), at the versions in
`dependencies.lock`, into `managed_components/` (not in this repository).

| Component | Version | Licence (the component's `LICENSE`) | In the image |
|---|---|---|---|
| espressif/esp_audio_codec | 2.5.0 | Espressif Modified MIT: "Permission is hereby granted for use EXCLUSIVELY with Espressif Systems products" | Yes, prebuilt `libesp_audio_codec.a`, `libesp_audio_simple_dec.a`. The decoders linked are MP3 and AAC ("The Android Open Source Project", Apache-2.0), FLAC (Xiph.Org, BSD-style `COPYING.Xiph`) and ALAC (Apple, Apache-2.0), as [ESP-ADF's Copyrights and Licenses page](https://docs.espressif.com/projects/esp-adf/en/latest/COPYRIGHT.html) lists them; the component ships no copy of those notices |
| espressif/media_lib_sal | 1.0.2 | Espressif Modified MIT | Yes |
| espressif/esp_media_protocols | 1.0.1 | ESPRESSIF MIT, "for use on all ESPRESSIF SYSTEMS products" | Yes, prebuilt `libesp_media_protocols.a` (SSDP, UPnP, SOAP and GENA for DLNA) |
| lvgl/lvgl | 9.3.0 | MIT, "Copyright (c) 2025 LVGL Kft" | Yes. Of its built-in fonts only `lv_font_montserrat_14` is linked: Montserrat (OFL 1.1, "The Montserrat Project Authors") with Font Awesome 5 Free symbols (OFL 1.1, Fonticons, Inc.). Its bundled libraries (ThorVG, lodepng, its TLSF and printf) are off in `sdkconfig` and absent from the ELF |
| espressif/tinyusb | 0.21.0~2 | MIT, "Copyright (c) 2012-2026, hathach (tinyusb.org)" | Yes |
| espressif/libsodium | 1.0.21 | ISC, "Copyright (c) 2013-2026 Frank Denis" | Yes |
| joltwallet/littlefs | 1.20.4 | MIT, "Copyright 2020 Brian Pugh"; littlefs itself BSD-3-Clause, "Copyright (c) 2022, The littlefs authors. Copyright (c) 2017, Arm Limited" | Yes |
| espressif/esp_hosted | 3.0.7 | Apache-2.0 for the host side; protobuf-c BSD-2-Clause, "Copyright (c) 2008-2022, Dave Benson and the protobuf-c authors". The Linux kernel module under `host/linux/` is GPL-2.0-only | Yes; the GPL-2.0 kernel module is not compiled (absent from `compile_commands.json`) |
| espressif/mqtt | 1.1.0 | Apache-2.0; `lib/mqtt_msg.c` BSD-3-Clause, "SPDX-FileCopyrightText: 2014 Stephen Robinson" | Yes |
| espressif/esp-dsp 1.8.2, esp_codec_dev 1.4.0, esp_lcd_st7701 2.0.2~2, esp_lcd_touch 1.2.1, esp_lcd_touch_gt911 1.2.1, esp_lvgl_port 2.9.0, esp_wifi_remote 1.6.4, mdns 1.12.0 | | Apache-2.0, Espressif Systems | Yes |
| espressif/esp_libsrtp 1.0.0 (BSD-3-Clause, Cisco Systems), eppp_link 1.1.6, wifi_remote_over_eppp 0.3.3, esp_serial_slave_link 1.1.2, cmake_utilities 0.5.3 | | Apache-2.0 unless noted | No: resolved or compiled as dependencies, nothing of them in the ELF |

## ESP-IDF v5.5.5 and the toolchain

ESP-IDF is Apache-2.0, "Copyright (C) 2015-2023 Espressif Systems"; its own list of third-party
code is `docs/en/COPYRIGHT.rst` in the IDF tree. In this image:

| Body of code | Licence as found | Source of the text |
|---|---|---|
| newlib (C library) | BSD-style, various holders | `components/newlib/COPYING.NEWLIB` |
| GCC runtime (libgcc, libstdc++) | GPL-3.0 with the GCC Runtime Library Exception 3.1 | Toolchain. The exception lets the image be distributed under terms of the distributor's choice; no notice is required |
| FreeRTOS kernel | MIT, Amazon.com, Inc. | `components/freertos/FreeRTOS-Kernel/LICENSE.md` |
| lwIP | BSD-3-Clause, Swedish Institute of Computer Science, and per-file holders | `components/lwip/lwip/COPYING` |
| Mbed TLS | Apache-2.0 OR GPL-2.0-or-later | Taken under Apache-2.0 |
| FatFs | BSD-style, "Copyright (C) 2022, ChaN" | `components/fatfs/src/ff.h` header |
| SD/MMC protocol layer | ISC-style, "Copyright (c) 2006 Uwe Stuehler" (OpenBSD) | `components/sdmmc/sdmmc_common.c` header |
| TLSF allocator | BSD-3-Clause, "2006-2016 Matthew Conte" | `components/heap/tlsf/tlsf.c` SPDX |
| UBSAN runtime | BSD-2-Clause, "Copyright (c) 2016, Linaro Limited" | `components/esp_system/ubsan.c` header |
| http_parser | MIT, Igor Sysoev / Joyent, Inc. | `components/http_parser/LICENSE.txt` |
| protobuf-c | BSD-2-Clause | `components/protobuf-c/protobuf-c/LICENSE` |

The ESP32-P4's mask ROM holds code under third-party licences too (newlib, TJpgDec and others,
listed in the same `COPYRIGHT.rst`); it is in the chip, not in anything this repository builds.

## Not in this repository

- **ESP32-C6 co-processor firmware.** Built from esp_hosted's slave side (Apache-2.0) with
  ESP-IDF for the C6, Espressif's binary Wi-Fi libraries included. This repository builds and
  ships none of it (`docs/C6_UPDATE.md`).
- **Bluetooth satellite.** The separate repository `modesp-bt-satellite`, with its own licence
  and its own `THIRD_PARTY_NOTICES.md`.

## A binary release

What the terms above ask of whoever hands out a built image rather than this source. Recorded
here, not resolved. The CI keeps a size report, not an image.

- **Ship `LICENSE` and `THIRD_PARTY_LICENSES.txt` with the image.** Apache-2.0 §4(a), clause 2
  of every BSD licence above (littlefs, lwIP, TLSF, libFLAC, protobuf-c, `mqtt_msg.c`,
  audio-resampler, UBSAN), the MIT and ISC notices and OFL §2 (Roboto, Montserrat, Font
  Awesome) each want their text handed out with a binary. Regenerate the file first: it is
  read from the dependencies the image was built with.
- **Espressif Modified MIT, ESPRESSIF MIT** (esp_audio_codec, media_lib_sal,
  esp_media_protocols, esp_dlna): use with Espressif products only. The image runs on an
  ESP32-P4.
- **AirPlay 2 receiver core**: non-commercial only, like this project's own code. A commercial
  release needs Remi Bouteiller's written permission, or `components/airplay2` and
  `modules/airplay` removed (`components/airplay2/UPSTREAM.md`).
- **Patents** are a separate question from copyright: AAC, decoded by esp_audio_codec, is
  still under patent licensing for commercial devices.

## Re-checking

The licence columns quote the files, so `grep -rn -i "copyright\|SPDX\|licen" <path>` on a
row's path reproduces it. What the image links, after a rebuild:

```bash
riscv32-esp-elf-nm -C build/modesp_p4_player.elf | grep -c -i <symbol prefix>
```

`python tools/third_party_licenses.py --check` says whether `THIRD_PARTY_LICENSES.txt` still
matches the dependencies on disk.
