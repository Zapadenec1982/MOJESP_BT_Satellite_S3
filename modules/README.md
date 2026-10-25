# modules/

Модулі продукту: один ESP-IDF-компонент на модуль, `manifest.json` (ключі SharedState, UI, MQTT) і
C++-клас на `BaseModule`. Активні модулі — `project.json` → `modules`; `tools/generate_ui.py` робить
із них `generated/modules.cmake`, `module_includes.h`, `module_instances.h`, `module_register.h`, а з
маніфестів — метадані стану, теми MQTT і `data/ui.json`.

| Модуль | Що робить |
|---|---|
| `ui_lvgl` | інтерфейс LVGL на MIPI-DSI панелі: тач, енкодер, підсвітка, режим очікування ([PANEL.md](../docs/PANEL.md), [UI.md](../docs/UI.md)) |
| `audio` | аудіоядро: вибір джерела (`audio.source`), DSP, гучність, лімітери, вихід I2S на ЦАП, вхід Bluetooth; задача на ядрі 1 ([AUDIO.md](../docs/AUDIO.md)) |
| `storage` | microSD: монтування `/sd`, стан і обсяг картки |
| `player` | відтворення файлів із microSD (MP3, FLAC, WAV, AAC, M4A) у PCM-кільце |
| `radio` | інтернет-радіо: HTTP(S)-потік, ICY, список станцій; також грає потоки, передані DLNA ([NETWORK.md](../docs/NETWORK.md)) |
| `dlna` | DLNA/UPnP: рендерер (поверх `components/esp_dlna`) і браузер медіасерверів ([NETWORK.md](../docs/NETWORK.md)) |
| `bt` | Bluetooth через ESP32-сателіт: UART-лінк, стан з'єднання, метадані, спарювання ([LINK_PROTOCOL.md](../docs/LINK_PROTOCOL.md)) |
| `airplay` | приймач AirPlay 1/2 поверх `components/airplay2`, некомерційна ліцензія ([UPSTREAM.md](../components/airplay2/UPSTREAM.md)) |
| `usb_audio` | USB-аудіо: панель як звукова карта комп'ютера, UAC2 на HS-порту ([AUDIO.md](../docs/AUDIO.md)) |
