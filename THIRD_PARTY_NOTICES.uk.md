[English](THIRD_PARTY_NOTICES.md) · **Українська**

# Сторонній код у ModESP Streamer

Що містить цей репозиторій і що потрапляє в образ прошивки (`modesp_p4_player.bin`) і
вебінтерфейс (`data.bin`), звідки воно походить і під якою ліцензією — **так, як це записано у
файлах**, станом на 2026-10-25, до відкриття репозиторію. Це перелік фактів для того, хто має
вирішити, що вони означають; це не юридична консультація.

Власний код цього проєкту — під [PolyForm Noncommercial License 1.0.0](LICENSE). **Ні на що з
переліченого нижче вона не поширюється**: кожен масив стороннього коду зберігає власну ліцензію, а файл із
власним ліцензійним повідомленням зберігає його, разом зі змінами, зробленими тут. Самі тексти —
у [THIRD_PARTY_LICENSES.txt](THIRD_PARTY_LICENSES.txt), який `tools/third_party_licenses.py`
генерує з названих там файлів.

«В образі» нижче — перевірене, а не припущене: за символами ELF, зібраного з
`dependencies.lock`, а не лише за тим, що компілює збирання.

## У цьому репозиторії

| Код | Де в цьому дереві | Ліцензія, як у файлах | Примітки |
|---|---|---|---|
| Ядро приймача AirPlay 2, [rbouteiller/airplay-esp32](https://github.com/rbouteiller/airplay-esp32) на `811d5f8` | `components/airplay2/upstream/` побайтово як в upstream; `components/airplay2/port/` — адаптовані копії й сполучний код цього проєкту | `components/airplay2/LICENSE`: "Non-Commercial License, Copyright (c) 2026 Remi Bouteiller" — використання, копіювання, зміна й поширення "for non-commercial purposes only"; комерційне використання потребує письмового дозволу правовласника | `components/airplay2/UPSTREAM.uk.md` перелічує, що взято, і кожну зміну. `modules/airplay` — його єдиний користувач; без обох прошивка збирається, а AirPlay не пропонується. Upstream містить RSA-ключ AirPort Express (`rtsp/rtsp_rsa.c`) і таблиці FairPlay setup (`rtsp/rtsp_fairplay.c`), які публікують і інші відкриті приймачі |
| esp_dlna, з [espressif/esp-adf](https://github.com/espressif/esp-adf) `release/v2.x`, `examples/advanced_examples/dlna` | `components/esp_dlna/` | Заголовки файлів: "ESPRESSIF MIT License, Copyright (c) 2019 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>", "Permission is hereby granted for use on all ESPRESSIF SYSTEMS products" | Взято 2026-09-18. Чотири зміни, кожна позначена `p4-player:`, перелічені в `components/esp_dlna/README.uk.md` |
| audio-resampler, [dbry/audio-resampler](https://github.com/dbry/audio-resampler) | `components/audio_resampler/` | `license.txt`: BSD-3-Clause, "Copyright (c) 2006 - 2025 David Bryant" | master станом на 2026-03. Локальні зміни позначені `MODESP:` у `resampler.c` і `resampler.h` |
| stb_image 2.30, [nothings/stb](https://github.com/nothings/stb) | `components/cover/src/stb_image.h` | Кінець файлу: MIT, "Copyright (c) 2017 Sean Barrett", або суспільне надбання (Unlicense), на вибір користувача | Без змін |
| jsmn, [zserge/jsmn](https://github.com/zserge/jsmn) | `components/jsmn/jsmn.h` | Заголовок файлу: MIT, "Copyright (c) 2010 Serge Zaitsev" | Без змін |
| Embedded Template Library 20.40.1, [ETLCPP/etl](https://github.com/ETLCPP/etl) | `components/etl/etl`, git-субмодуль | `LICENSE`: MIT; заголовки: "Copyright(c) 2014 John Wellbelove" | Прив'язана до коміту, завантажується `git submodule update --init` |
| Послідовність ініціалізації ST7701S | `drivers/dsi_st7701/src/dsi_st7701_driver.cpp` | Apache-2.0, "SPDX-FileCopyrightText: 2024 Espressif Systems", як у [csvke/esp32_p4_jc4880p433c_bsp](https://github.com/csvke/esp32_p4_jc4880p433c_bsp) `src/bsp_display.c` | Значення регістрів скопійовано без змін; файл каже про це там, де вони стоять |
| Roboto, гліфи | `modules/ui_lvgl/src/fonts/ui_font_{300,400,500,600}_*.c` | SIL OFL 1.1, "Copyright 2011 The Roboto Project Authors" (`@fontsource/roboto` 5.x) | Растеризовано `tools/fonts/build_fonts.py` за допомогою `lv_font_conv`; вихідних шрифтів у дереві немає |
| Material Symbols Rounded, гліфи | `modules/ui_lvgl/src/fonts/ui_font_icons_*.c`; підмножина woff2, вбудована в `webui/src/styles/icons.css` | Apache-2.0, [google/material-design-icons](https://github.com/google/material-design-icons) на `27e9ef1` (`tools/fonts/symbols.lock`) | woff2 зберігає таблицю name шрифту, зокрема записи copyright і licence |
| Рантайм Svelte 4.2.20 | `data/www/*.js.gz`, зібраний вебінтерфейс | `LICENSE.md`: MIT, "Copyright (c) 2016-23 [these people](https://github.com/sveltejs/svelte/graphs/contributors)" | Rollup вкомпільовує його в бандл; мініфікація викидає коментар, тож текст їде в `THIRD_PARTY_LICENSES.txt` |
| Оновлювач співпроцесора C6 | `tools/c6_updater/` | Apache-2.0, "SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD" | З esp_hosted 3.0.7 `examples/ota/coprocessor_ota/mcu_host`. Файли, змінені тут, несуть `SPDX-FileContributor: 2026 Zapadenec1982` — повідомлення, якого Apache-2.0 §4(b) вимагає від змінених файлів. Окремий одноразовий образ, не прошивка плеєра |
| doctest | `tests/host/doctest.h` | Заголовок файлу: "Copyright (c) 2016-2023 Viktor Kirilov ... Distributed under the MIT Software License"; `LICENSE.txt`, на який він посилається, у дереві немає, текст MIT — у `THIRD_PARTY_LICENSES.txt` | Лише тести на хості |
| Криві пресетів EQ | `components/audio_dsp/src/eq_presets.cpp`, з `tools/eq/fit_presets.py` | — | Числові налаштування: пресети Winamp у тому вигляді, як їх постачає Audacious, частоти смуг Winamp, як їх перелічує `equalizer_presets.h` VLC, наново підігнані тут під десять пікових смуг. Коду не взято |
| Відповідь Deezer API | `tests/host/fixtures/radio_catalog/deezer_*.json` | — | Записана відповідь публічного пошукового API Deezer, збережена як фікстура парсера |

## Завантажується під час збирання

З [ESP Component Registry](https://components.espressif.com), у версіях із
`dependencies.lock`, у `managed_components/` (не в цьому репозиторії).

| Компонент | Версія | Ліцензія (`LICENSE` компонента) | В образі |
|---|---|---|---|
| espressif/esp_audio_codec | 2.5.0 | Espressif Modified MIT: "Permission is hereby granted for use EXCLUSIVELY with Espressif Systems products" | Так, готові `libesp_audio_codec.a`, `libesp_audio_simple_dec.a`. Злінковані декодери — MP3 і AAC ("The Android Open Source Project", Apache-2.0), FLAC (Xiph.Org, BSD-подібна `COPYING.Xiph`) і ALAC (Apple, Apache-2.0), як їх перелічує [сторінка Copyrights and Licenses ESP-ADF](https://docs.espressif.com/projects/esp-adf/en/latest/COPYRIGHT.html); копій цих повідомлень компонент не постачає |
| espressif/media_lib_sal | 1.0.2 | Espressif Modified MIT | Так |
| espressif/esp_media_protocols | 1.0.1 | ESPRESSIF MIT, "for use on all ESPRESSIF SYSTEMS products" | Так, готова `libesp_media_protocols.a` (SSDP, UPnP, SOAP і GENA для DLNA) |
| lvgl/lvgl | 9.3.0 | MIT, "Copyright (c) 2025 LVGL Kft" | Так. З її вбудованих шрифтів злінковано лише `lv_font_montserrat_14`: Montserrat (OFL 1.1, "The Montserrat Project Authors") із символами Font Awesome 5 Free (OFL 1.1, Fonticons, Inc.). Її вбудовані бібліотеки (ThorVG, lodepng, її TLSF і printf) вимкнені в `sdkconfig` і відсутні в ELF |
| espressif/tinyusb | 0.21.0~2 | MIT, "Copyright (c) 2012-2026, hathach (tinyusb.org)" | Так |
| espressif/libsodium | 1.0.21 | ISC, "Copyright (c) 2013-2026 Frank Denis" | Так |
| joltwallet/littlefs | 1.20.4 | MIT, "Copyright 2020 Brian Pugh"; сам littlefs — BSD-3-Clause, "Copyright (c) 2022, The littlefs authors. Copyright (c) 2017, Arm Limited" | Так |
| espressif/esp_hosted | 3.0.7 | Apache-2.0 для боку хоста; protobuf-c — BSD-2-Clause, "Copyright (c) 2008-2022, Dave Benson and the protobuf-c authors". Модуль ядра Linux у `host/linux/` — GPL-2.0-only | Так; модуль ядра під GPL-2.0 не компілюється (відсутній у `compile_commands.json`) |
| espressif/mqtt | 1.1.0 | Apache-2.0; `lib/mqtt_msg.c` — BSD-3-Clause, "SPDX-FileCopyrightText: 2014 Stephen Robinson" | Так |
| espressif/esp-dsp 1.8.2, esp_codec_dev 1.4.0, esp_lcd_st7701 2.0.2~2, esp_lcd_touch 1.2.1, esp_lcd_touch_gt911 1.2.1, esp_lvgl_port 2.9.0, esp_wifi_remote 1.6.4, mdns 1.12.0 | | Apache-2.0, Espressif Systems | Так |
| espressif/esp_libsrtp 1.0.0 (BSD-3-Clause, Cisco Systems), eppp_link 1.1.6, wifi_remote_over_eppp 0.3.3, esp_serial_slave_link 1.1.2, cmake_utilities 0.5.3 | | Apache-2.0, якщо не зазначено інше | Ні: розв'язуються або компілюються як залежності, у ELF нічого з них немає |

## ESP-IDF v5.5.5 і тулчейн

ESP-IDF — Apache-2.0, "Copyright (C) 2015-2023 Espressif Systems"; його власний перелік
стороннього коду — `docs/en/COPYRIGHT.rst` у дереві IDF. У цьому образі:

| Код | Ліцензія, як записано | Джерело тексту |
|---|---|---|
| newlib (бібліотека C) | BSD-подібна, різні правовласники | `components/newlib/COPYING.NEWLIB` |
| Рантайм GCC (libgcc, libstdc++) | GPL-3.0 з GCC Runtime Library Exception 3.1 | Тулчейн. Виняток дозволяє поширювати образ на умовах, які обере той, хто поширює; повідомлення не потрібне |
| Ядро FreeRTOS | MIT, Amazon.com, Inc. | `components/freertos/FreeRTOS-Kernel/LICENSE.md` |
| lwIP | BSD-3-Clause, Swedish Institute of Computer Science, і правовласники окремих файлів | `components/lwip/lwip/COPYING` |
| Mbed TLS | Apache-2.0 OR GPL-2.0-or-later | Взято за Apache-2.0 |
| FatFs | BSD-подібна, "Copyright (C) 2022, ChaN" | заголовок `components/fatfs/src/ff.h` |
| Протокольний рівень SD/MMC | ISC-подібна, "Copyright (c) 2006 Uwe Stuehler" (OpenBSD) | заголовок `components/sdmmc/sdmmc_common.c` |
| Алокатор TLSF | BSD-3-Clause, "2006-2016 Matthew Conte" | SPDX у `components/heap/tlsf/tlsf.c` |
| Рантайм UBSAN | BSD-2-Clause, "Copyright (c) 2016, Linaro Limited" | заголовок `components/esp_system/ubsan.c` |
| http_parser | MIT, Igor Sysoev / Joyent, Inc. | `components/http_parser/LICENSE.txt` |
| protobuf-c | BSD-2-Clause | `components/protobuf-c/protobuf-c/LICENSE` |

Маскова ROM ESP32-P4 теж містить код під сторонніми ліцензіями (newlib, TJpgDec та інші,
перелічені в тому самому `COPYRIGHT.rst`); він у кристалі, а не в чомусь, що збирає цей
репозиторій.

## Не в цьому репозиторії

- **Прошивка співпроцесора ESP32-C6.** Збирається з боку slave esp_hosted (Apache-2.0) з ESP-IDF
  для C6, разом із бінарними Wi-Fi-бібліотеками Espressif. Цей репозиторій нічого з неї не
  збирає й не постачає (`docs/C6_UPDATE.uk.md`).
- **Bluetooth-супутник.** Окремий репозиторій `modesp-bt-satellite`, з власною ліцензією і
  власним `THIRD_PARTY_NOTICES.md`.

## Бінарний реліз

Що наведені вище умови вимагають від того, хто роздає зібраний образ, а не ці вихідні коди.
Записано тут, не розв'язано. CI зберігає звіт про розміри, а не образ.

- **Постачати `LICENSE` і `THIRD_PARTY_LICENSES.txt` разом з образом.** Apache-2.0 §4(a), пункт 2
  кожної BSD-ліцензії вище (littlefs, lwIP, TLSF, libFLAC, protobuf-c, `mqtt_msg.c`,
  audio-resampler, UBSAN), повідомлення MIT та ISC і OFL §2 (Roboto, Montserrat, Font
  Awesome) — кожне вимагає, щоб його текст роздавали разом із бінарником. Спершу перегенеруйте
  файл: він читається із залежностей, з якими зібрано образ.
- **Espressif Modified MIT, ESPRESSIF MIT** (esp_audio_codec, media_lib_sal,
  esp_media_protocols, esp_dlna): використання лише з продуктами Espressif. Образ працює на
  ESP32-P4.
- **Ядро приймача AirPlay 2**: лише некомерційне використання, як і для власного коду цього
  проєкту. Комерційний реліз потребує письмового дозволу Remi Bouteiller або видалення
  `components/airplay2` і `modules/airplay` (`components/airplay2/UPSTREAM.uk.md`).
- **Патенти** — окреме від авторського права питання: AAC, який декодує esp_audio_codec, для
  комерційних пристроїв досі під патентним ліцензуванням.

## Повторна перевірка

Стовпці ліцензій цитують файли, тож `grep -rn -i "copyright\|SPDX\|licen" <path>` на шляху з
рядка таблиці відтворює їх. Що злінковано в образ, після перезбирання:

```bash
riscv32-esp-elf-nm -C build/modesp_p4_player.elf | grep -c -i <symbol prefix>
```

`python tools/third_party_licenses.py --check` каже, чи `THIRD_PARTY_LICENSES.txt` досі
відповідає залежностям на диску.
