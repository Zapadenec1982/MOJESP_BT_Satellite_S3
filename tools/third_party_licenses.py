#!/usr/bin/env python3
"""
third_party_licenses.py — write THIRD_PARTY_LICENSES.txt: the licence text of every body of
third-party code in the built images, as the files that carry it put it.

THIRD_PARTY_NOTICES.md is the inventory: what is in the firmware image and in data.bin, where it
comes from, under what terms, and what those terms ask of a release. This file is the other half,
the texts themselves, which the permissive licences want handed out with a binary (Apache-2.0
§4(a), BSD clause 2, MIT, ISC, OFL §2). A binary release ships it beside the images.

Every text is read from the tree the build uses, so a bumped dependency is picked up by running
this again:

    managed_components/        idf.py reconfigure
    $IDF_PATH (or --idf)       the ESP-IDF the image is built with (v5.5.5)
    webui/node_modules/        npm ci in webui/
    tools/fonts/node_modules/  npm ci in tools/fonts/
    LICENSES/                  standard texts for the notices that name a licence without carrying
                               its text (an SPDX identifier, "see LICENSE.txt"), and libFLAC's
                               COPYING.Xiph, which esp_audio_codec ships no copy of

A missing file is an error, not a skipped entry: a silently shorter list is the failure this
exists to prevent.

    python tools/third_party_licenses.py            # rewrite THIRD_PARTY_LICENSES.txt
    python tools/third_party_licenses.py --check    # exit 1 if it is out of date
"""

import argparse
import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "THIRD_PARTY_LICENSES.txt"
MC = "managed_components"

RULE = "=" * 80
THIN = "-" * 80


class Missing(Exception):
    pass


def read(path):
    if not path.is_file():
        raise Missing(str(path))
    return path.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")


def leading_comment(text):
    """The comment block a source file opens with: /* ... */ or a run of // lines."""
    if text.startswith("/*"):
        end = text.index("*/") + 2
        return text[:end]
    lines = []
    for line in text.split("\n"):
        if not line.startswith("//"):
            break
        lines.append(line)
    if not lines:
        raise ValueError("no leading comment")
    return "\n".join(lines)


def between(text, start):
    """From the line holding `start` to the end of the comment it sits in."""
    i = text.index(start)
    i = text.rfind("\n", 0, i) + 1
    j = text.index("*/", i)
    return text[i:j]


def template(name, copyright_line):
    """A standard text from LICENSES/ with its placeholder copyright line filled in."""
    text = read(ROOT / "LICENSES" / name)
    out, done = [], False
    for line in text.split("\n"):
        if not done and re.match(r"Copyright \(c\) <", line):
            out.append(copyright_line)
            done = True
        else:
            out.append(line)
    if not done:
        raise ValueError(f"LICENSES/{name}: no placeholder copyright line")
    return "\n".join(out)


def mc_version(name):
    text = read(ROOT / MC / name / "idf_component.yml")
    m = re.search(r"^version:\s*['\"]?([^'\"\n]+)", text, re.M)
    return m.group(1).strip() if m else "?"


def npm_version(pkg_dir):
    return json.loads(read(pkg_dir / "package.json"))["version"]


def entries(idf):
    """(section, title, used in, source shown, text) for every entry, in output order."""
    c = idf / "components"
    lv_fonts = ROOT / MC / "lvgl__lvgl/scripts/built_in_font/font_license"
    roboto = ROOT / "tools/fonts/node_modules/@fontsource/roboto"
    svelte = ROOT / "webui/node_modules/svelte"
    fw = "Firmware image (modesp_p4_player.bin)"
    web = "Web UI (data.bin, data/www/)"
    repo = "This repository only; not in an image"

    def p(rel):
        return ROOT / rel

    yield (fw, "AirPlay 2 receiver core — rbouteiller/airplay-esp32 at 811d5f8",
           "components/airplay2", "components/airplay2/LICENSE",
           read(p("components/airplay2/LICENSE")))
    yield (fw, "esp_dlna — espressif/esp-adf release/v2.x, examples/advanced_examples/dlna",
           "components/esp_dlna", "components/esp_dlna/esp_dlna.c (file header)",
           leading_comment(read(p("components/esp_dlna/esp_dlna.c"))))
    yield (fw, "audio-resampler — dbry/audio-resampler",
           "components/audio_resampler", "components/audio_resampler/license.txt",
           read(p("components/audio_resampler/license.txt")))
    yield (fw, "stb_image 2.30 — nothings/stb",
           "components/cover", "components/cover/src/stb_image.h (end of file)",
           between(read(p("components/cover/src/stb_image.h")),
                   "This software is available under 2 licenses"))
    yield (fw, "jsmn — zserge/jsmn",
           "components/jsmn", "components/jsmn/jsmn.h (file header)",
           leading_comment(read(p("components/jsmn/jsmn.h"))))
    yield (fw, "Embedded Template Library 20.40.1 — ETLCPP/etl (git submodule)",
           "components/etl", "components/etl/etl/LICENSE; holder from include/etl/*.h: "
           "\"Copyright(c) 2014 John Wellbelove\"",
           read(p("components/etl/etl/LICENSE")))
    yield (fw, f"Roboto — @fontsource/roboto {npm_version(roboto)}",
           "modules/ui_lvgl/src/fonts/ui_font_{300,400,500,600}_*.c (rasterised glyphs)",
           "tools/fonts/node_modules/@fontsource/roboto/LICENSE",
           read(roboto / "LICENSE"))
    yield (fw, f"LVGL {mc_version('lvgl__lvgl')} — lvgl/lvgl",
           "managed component lvgl__lvgl", f"{MC}/lvgl__lvgl/LICENCE.txt",
           read(ROOT / MC / "lvgl__lvgl/LICENCE.txt"))
    yield (fw, "Montserrat — LVGL's built-in lv_font_montserrat_14",
           "managed component lvgl__lvgl",
           f"{MC}/lvgl__lvgl/scripts/built_in_font/font_license/Montserrat/OFL.txt",
           read(lv_fonts / "Montserrat/OFL.txt"))
    yield (fw, "Font Awesome 5 Free — the symbols in LVGL's lv_font_montserrat_14",
           "managed component lvgl__lvgl",
           f"{MC}/lvgl__lvgl/scripts/built_in_font/font_license/FontAwesome5/LICENSE.txt",
           read(lv_fonts / "FontAwesome5/LICENSE.txt"))
    yield (fw, f"TinyUSB {mc_version('espressif__tinyusb')} — hathach/tinyusb",
           "managed component espressif__tinyusb", f"{MC}/espressif__tinyusb/LICENSE",
           read(ROOT / MC / "espressif__tinyusb/LICENSE"))
    yield (fw, f"libsodium {mc_version('espressif__libsodium')} — jedisct1/libsodium",
           "managed component espressif__libsodium", f"{MC}/espressif__libsodium/LICENSE",
           read(ROOT / MC / "espressif__libsodium/LICENSE"))
    yield (fw, f"esp_littlefs {mc_version('joltwallet__littlefs')} — joltwallet/esp_littlefs",
           "managed component joltwallet__littlefs", f"{MC}/joltwallet__littlefs/LICENSE",
           read(ROOT / MC / "joltwallet__littlefs/LICENSE"))
    yield (fw, "littlefs — littlefs-project/littlefs (inside esp_littlefs)",
           "managed component joltwallet__littlefs",
           f"{MC}/joltwallet__littlefs/src/littlefs/LICENSE.md",
           read(ROOT / MC / "joltwallet__littlefs/src/littlefs/LICENSE.md"))
    for name in ("espressif__esp_audio_codec", "espressif__media_lib_sal",
                 "espressif__esp_media_protocols"):
        short = name.split("__", 1)[1]
        yield (fw, f"{short} {mc_version(name)} — Espressif", f"managed component {name}",
               f"{MC}/{name}/LICENSE", read(ROOT / MC / name / "LICENSE"))
    yield (fw, "libFLAC — Xiph.Org (prebuilt inside esp_audio_codec)",
           "managed component espressif__esp_audio_codec (libesp_audio_codec.a)",
           "LICENSES/LicenseRef-FLAC-Xiph.txt (xiph/flac 1.3.4 COPYING.Xiph; holder as on "
           "ESP-ADF's Copyrights and Licenses page)",
           read(ROOT / "LICENSES/LicenseRef-FLAC-Xiph.txt"))
    yield (fw, "protobuf-c — protobuf-c/protobuf-c",
           "managed component espressif__esp_hosted (msg_codec), ESP-IDF protobuf-c",
           f"{MC}/espressif__esp_hosted/common/serializers/third_party/msg_codec/protobuf-c/LICENSE",
           read(ROOT / MC / "espressif__esp_hosted/common/serializers/third_party/msg_codec/"
                "protobuf-c/LICENSE"))
    yield (fw, "ESP-MQTT message encoder (lib/mqtt_msg.c)",
           "managed component espressif__mqtt",
           f"{MC}/espressif__mqtt/lib/mqtt_msg.c: SPDX BSD-3-Clause, "
           "\"SPDX-FileCopyrightText: 2014 Stephen Robinson\"; text LICENSES/BSD-3-Clause.txt",
           template("BSD-3-Clause.txt", "Copyright (c) 2014 Stephen Robinson"))
    yield (fw, "newlib — the C library (toolchain and ESP-IDF newlib component)",
           "ESP-IDF", "$IDF_PATH/components/newlib/COPYING.NEWLIB",
           read(c / "newlib/COPYING.NEWLIB"))
    yield (fw, "FreeRTOS kernel", "ESP-IDF",
           "$IDF_PATH/components/freertos/FreeRTOS-Kernel/LICENSE.md",
           read(c / "freertos/FreeRTOS-Kernel/LICENSE.md"))
    yield (fw, "lwIP", "ESP-IDF", "$IDF_PATH/components/lwip/lwip/COPYING",
           read(c / "lwip/lwip/COPYING"))
    yield (fw, "FatFs — ChaN", "ESP-IDF", "$IDF_PATH/components/fatfs/src/ff.h (file header)",
           leading_comment(read(c / "fatfs/src/ff.h")))
    yield (fw, "SD/MMC protocol layer — derived from OpenBSD", "ESP-IDF",
           "$IDF_PATH/components/sdmmc/sdmmc_common.c (file header)",
           leading_comment(read(c / "sdmmc/sdmmc_common.c")))
    yield (fw, "TLSF allocator — Matthew Conte", "ESP-IDF",
           "$IDF_PATH/components/heap/tlsf/tlsf.c: SPDX BSD-3-Clause, "
           "\"SPDX-FileCopyrightText: 2006-2016 Matthew Conte\"; text LICENSES/BSD-3-Clause.txt",
           template("BSD-3-Clause.txt", "Copyright (c) 2006-2016 Matthew Conte"))
    yield (fw, "UBSAN runtime — Linaro", "ESP-IDF",
           "$IDF_PATH/components/esp_system/ubsan.c (file header)",
           leading_comment(read(c / "esp_system/ubsan.c")))
    yield (fw, "http_parser — NGINX / Joyent", "ESP-IDF",
           "$IDF_PATH/components/http_parser/LICENSE.txt",
           read(c / "http_parser/LICENSE.txt"))
    yield (web, f"Svelte {npm_version(svelte)} — the runtime compiled into the bundle",
           "data/www/*.js.gz", "webui/node_modules/svelte/LICENSE.md",
           read(svelte / "LICENSE.md"))
    yield (repo, "doctest — doctest/doctest", "tests/host/doctest.h",
           "tests/host/doctest.h: \"Copyright (c) 2016-2023 Viktor Kirilov ... Distributed under "
           "the MIT Software License\"; text LICENSES/MIT.txt",
           template("MIT.txt", "Copyright (c) 2016-2023 Viktor Kirilov"))


APACHE_USERS = """\
Firmware image:
  ESP-IDF v5.5.5 (Espressif Systems), including Mbed TLS (dual Apache-2.0 OR GPL-2.0-or-later,
    taken here under Apache-2.0)
  espressif/esp-dsp, esp_codec_dev, esp_hosted (host side), esp_lcd_st7701, esp_lcd_touch,
    esp_lcd_touch_gt911, esp_lvgl_port, esp_wifi_remote, mdns, mqtt (except lib/mqtt_msg.c)
  Inside esp_audio_codec: the MP3 and AAC decoders (The Android Open Source Project) and the
    ALAC decoder (Apple), as ESP-ADF's Copyrights and Licenses page lists them
  Material Symbols Rounded (Google, google/material-design-icons): glyphs in
    modules/ui_lvgl/src/fonts/ui_font_icons_*.c
  The ST7701S init sequence in drivers/dsi_st7701 (csvke/esp32_p4_jc4880p433c_bsp,
    SPDX-FileCopyrightText: 2024 Espressif Systems)
Web UI (data.bin):
  Material Symbols Rounded: the woff2 subset inline in webui/src/styles/icons.css
This repository only:
  tools/c6_updater (from esp_hosted examples/ota/coprocessor_ota/mcu_host, Espressif Systems)"""


def render(idf):
    out = [
        "Third-party licence texts — ModESP Streamer",
        "",
        "Generated by tools/third_party_licenses.py from the files named in each entry; do not",
        "edit by hand. THIRD_PARTY_NOTICES.md is the inventory and says what each licence asks",
        "of a release. This project's own code is under LICENSE (PolyForm Noncommercial 1.0.0);",
        "nothing below is.",
    ]
    section = None
    for sec, title, used, src, text in entries(idf):
        if sec != section:
            out += ["", "", RULE, sec.upper(), RULE]
            section = sec
        out += ["", THIN, title, f"In:   {used}", f"From: {src}", THIN, ""]
        out += [line.rstrip() for line in text.strip("\n").split("\n")]
    out += ["", "", RULE, "APACHE LICENSE 2.0", RULE, "", "Used by:", APACHE_USERS, "",
            "From: $IDF_PATH/LICENSE", THIN, ""]
    out += [line.rstrip() for line in read(idf / "LICENSE").strip("\n").split("\n")]
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--idf", help="ESP-IDF root (default: $IDF_PATH)")
    ap.add_argument("--check", action="store_true",
                    help="exit 1 if THIRD_PARTY_LICENSES.txt differs from what would be written")
    args = ap.parse_args()
    idf = args.idf or os.environ.get("IDF_PATH")
    if not idf:
        sys.exit("ESP-IDF not found: activate it or pass --idf")
    try:
        text = render(Path(idf))
    except Missing as e:
        sys.exit(f"missing: {e}\n(idf.py reconfigure, npm ci in webui/ and tools/fonts/)")
    if args.check:
        old = OUT.read_text(encoding="utf-8") if OUT.is_file() else ""
        if old != text:
            sys.exit(f"{OUT.name} is out of date: run tools/third_party_licenses.py")
        print(f"{OUT.name} is up to date")
        return
    OUT.write_text(text, encoding="utf-8", newline="\n")
    print(f"{OUT.name}: {text.count(THIN) // 2} entries, {len(text)} bytes")


if __name__ == "__main__":
    main()
