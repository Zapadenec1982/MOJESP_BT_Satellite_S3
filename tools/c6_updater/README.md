# c6_updater

Одноразова прошивка ESP32-P4, яка передає образ ESP-Hosted co-processor із розділу flash `slave_fw`
на ESP32-C6 по SDIO. Образ збирається з того самого релізу `esp_hosted`, що прибитий у плеєрі
(3.0.7). Основа — `esp_hosted/examples/ota/coprocessor_ota/mcu_host` (Apache-2.0), налаштована під
піни Guition JC4880P443C і 16-МБ таблицю розділів плеєра.

Процедура, перевірка образу, відновлення через UART C6 і правило зміни версії —
[C6_UPDATE.md](../../docs/C6_UPDATE.md).

Що змінено відносно прикладу (файли з `SPDX-FileContributor`):

| Файл | Зміна |
|---|---|
| `CMakeLists.txt` | ім'я проєкту `c6_updater` |
| `sdkconfig.defaults` | ревізія P4 v1.3, блок SDIO панелі (як у `boards/p4_player/sdkconfig.board`), Partition OTA з `slave_fw`, вимкнена перевірка host ↔ CP за compat-константами, flash 16 МБ |
| `partitions.csv` | рядок у рядок — `partitions_p4_16mb.csv` |
| `main/idf_component.yml` | `esp_hosted` 3.0.7 і `esp_wifi_remote` 1.6.4, як у плеєрі |
| `components/ota_partition/ota_partition.c`, `CMakeLists.txt` | перевірка checksum і SHA-256 образу до передачі (звідси залежність від `mbedtls`); версія образу порівнюється без префікса `v` |
| `components/common_ota_scripts/flash_selected_firmware.cmake` | порт esptool з `ESPPORT` / `ESPTOOL_PORT` |

`main/main.c` — без змін із прикладу. `components/ota_littlefs` і `components/ota_https` (разом з їхніми
README) — незмінний код прикладу, тут не використовується: у таблиці немає розділу `storage`, а
updater налаштований на Partition OTA.

Коротко:

1. Зібрати CP-образ для esp32c6 і скопіювати `eh_cp_ota_coprocessor_ota.bin` у
   `components/ota_partition/slave_fw_bin/` (не в git).
2. `idf.py set-target esp32p4 && idf.py build && idf.py -p <порт> flash monitor`
3. У моніторі: OTA → активація → перезапуск P4 → `Skipping OTA` →
   `OTA not required - slave firmware is up to date`.
4. Прошити плеєр із кореня репозиторію (`idf.py -p <порт> flash`); `slave_fw` лишається на місці.
