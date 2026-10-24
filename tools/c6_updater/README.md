# c6_updater

One-shot ESP32-P4 firmware that pushes the ESP-Hosted co-processor image (built from the same
`esp_hosted` release the player pins, currently 3.0.7) from the `slave_fw` flash partition to the
ESP32-C6 over SDIO. Derived from `esp_hosted/examples/ota/coprocessor_ota/mcu_host` (Apache-2.0),
configured for the Guition JC4880P443C pins and the player's 16 MB partition table.

Procedure, recovery over the C6 UART and the version rule: `docs/C6_UPDATE.md`.

Quick path:

1. Build the CP image for esp32c6 and copy `eh_cp_ota_coprocessor_ota.bin` into
   `components/ota_partition/slave_fw_bin/` (git-ignored).
2. `idf.py set-target esp32p4 && idf.py build && idf.py -p COMx flash monitor`
3. Watch the monitor: OTA → activate → host restart → `Versions compatible - OTA not required`.
4. Flash the player firmware from the repo root; `slave_fw` stays in place.
