**English** · [Українська](README.uk.md)

# c6_updater

One-shot ESP32-P4 firmware that pushes the ESP-Hosted co-processor image from the `slave_fw` flash
partition to the ESP32-C6 over SDIO. The image is built from the same `esp_hosted` release the player
pins (3.0.7). Based on `esp_hosted/examples/ota/coprocessor_ota/mcu_host` (Apache-2.0), configured for
the Guition JC4880P443C pins and the player's 16 MB partition table.

The procedure, the image check, recovery over the C6 UART and the version rule:
[C6_UPDATE.md](../../docs/C6_UPDATE.md).

What changed relative to the example (the files with `SPDX-FileContributor`):

| File | Change |
|---|---|
| `CMakeLists.txt` | project name `c6_updater` |
| `sdkconfig.defaults` | P4 revision v1.3, the panel's SDIO block (as in `boards/p4_player/sdkconfig.board`), Partition OTA from `slave_fw`, the host ↔ CP check by compat constants turned off, 16 MB flash |
| `partitions.csv` | `partitions_p4_16mb.csv`, line for line |
| `main/idf_component.yml` | `esp_hosted` 3.0.7 and `esp_wifi_remote` 1.6.4, as in the player |
| `components/ota_partition/ota_partition.c`, `CMakeLists.txt` | checksum and SHA-256 check of the image before the transfer (hence the dependency on `mbedtls`); the image version is compared without the `v` prefix |
| `components/common_ota_scripts/flash_selected_firmware.cmake` | esptool port from `ESPPORT` / `ESPTOOL_PORT` |

`main/main.c` is unchanged from the example. `components/ota_littlefs` and `components/ota_https`
(together with their READMEs) are unchanged example code, not used here: the table has no `storage`
partition, and the updater is configured for Partition OTA.

In short:

1. Build the CP image for esp32c6 and copy `eh_cp_ota_coprocessor_ota.bin` into
   `components/ota_partition/slave_fw_bin/` (not in git).
2. `idf.py set-target esp32p4 && idf.py build && idf.py -p <port> flash monitor`
3. In the monitor: OTA → activation → P4 restart → `Skipping OTA` →
   `OTA not required - slave firmware is up to date`.
4. Flash the player from the repository root (`idf.py -p <port> flash`); `slave_fw` stays in place.
