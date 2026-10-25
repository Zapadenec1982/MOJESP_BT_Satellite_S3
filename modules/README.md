**English** · [Українська](README.uk.md)

# modules/

Product modules: one ESP-IDF component per module, a `manifest.json` (SharedState keys, UI, MQTT) and
a C++ class derived from `BaseModule`. The active modules are `project.json` → `modules`; `tools/generate_ui.py`
turns them into `generated/modules.cmake`, `module_includes.h`, `module_instances.h`,
`module_register.h`, and the manifests into state metadata, MQTT topics and `data/ui.json`.

| Module | What it does |
|---|---|
| `ui_lvgl` | LVGL UI on the MIPI-DSI panel: touch, encoder, backlight, standby ([PANEL.md](../docs/PANEL.md), [UI.md](../docs/UI.md)) |
| `audio` | audio core: source selection (`audio.source`), DSP, volume, limiters, I2S output to the DAC, Bluetooth input; a task on core 1 ([AUDIO.md](../docs/AUDIO.md)) |
| `storage` | microSD: mounting `/sd`, card state and capacity |
| `player` | playback of files from microSD (MP3, FLAC, WAV, AAC, M4A) into the PCM ring |
| `radio` | internet radio: HTTP(S) stream, ICY, station list; also plays streams handed over by DLNA ([NETWORK.md](../docs/NETWORK.md)) |
| `dlna` | DLNA/UPnP: renderer (on top of `components/esp_dlna`) and media server browser ([NETWORK.md](../docs/NETWORK.md)) |
| `bt` | Bluetooth through the ESP32 satellite: UART link, connection state, metadata, pairing ([LINK_PROTOCOL.md](../docs/LINK_PROTOCOL.md)) |
| `airplay` | AirPlay 1/2 receiver on top of `components/airplay2`, non-commercial licence ([UPSTREAM.md](../components/airplay2/UPSTREAM.md)) |
| `usb_audio` | USB audio: the panel as a computer's sound card, UAC2 on the HS port ([AUDIO.md](../docs/AUDIO.md)) |
