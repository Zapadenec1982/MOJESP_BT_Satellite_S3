**English** · [Українська](C6_UPDATE.uk.md)

# Updating the ESP32-C6 firmware (ESP-Hosted co-processor)

The panel's Wi-Fi is an ESP32-C6, which the ESP32-P4 talks to over SDIO through ESP-Hosted. The host
library (`espressif/esp_hosted` **3.0.7**, `espressif/esp_wifi_remote` 1.6.4, pinned in
`components/modesp_net/idf_component.yml`) and the C6 firmware (co-processor, CP) must come from **the
same release** of esp_hosted; the CP must not be older than the host. The factory CP on the C6 is 2.3.x,
older than the host, so a new panel first goes through sections 2–4. The C6 can be flashed only through
the P4 (the updater, section 3) or through the C6's UART on the JP1 header (section 5).

A version mismatch in the player's log (WARN level and above, visible on the shipping build):

```
E (...) eh_init_evt: major version mismatch — OTA coprocessor from host
W (...) eh_init_evt: minor version mismatch — OTA coprocessor from host
```

A difference only in the patch version gives INFO `patch version differs (compatible)` and works.

## 1. Current CP version

The line `eh_init_evt: esp-hosted fw versions: host=3.0.7 coprocessor=a.b.c` (with ` (match)` when they
agree). It is INFO and is printed when the C6, once started, sends the host its init event:

- in the updater's log (section 3) — always: its build keeps IDF's default level (INFO);
- in the player's log — only on a build with `CONFIG_LOG_DEFAULT_LEVEL_INFO` in the local `sdkconfig`:
  the shipping build is at WARN, and `/api/loglevel` opens the tag only after the line has gone by.

The "Service" row in the panel's settings shows `C6 3.0.7` — this is a constant in
`modules/ui_lvgl/src/screen_set.cpp`, not a version read from the C6.

## 2. Build the CP firmware for the C6

The source is the same version that is pinned in `idf_component.yml`: the `espressif/esp-hosted-mcu`
repository, tag `v3.0.7`. **The clone directory must be named `esp_hosted`**: the examples pull the
component in through `override_path` to the repository root, and the IDF component name = the directory
name (with another name the build fails with `Failed to resolve component 'esp_hosted'`).

```
git clone --depth 1 --recurse-submodules --shallow-submodules -b v3.0.7 https://github.com/espressif/esp-hosted-mcu.git esp_hosted
cd esp_hosted/examples/ota/coprocessor_ota/cp
idf.py set-target esp32c6
idf.py build          # → build/eh_cp_ota_coprocessor_ota.bin, ~1.1 MB
```

- Transport: `menuconfig` → Configure coprocessor → CP transport → "Communication bus" = SDIO
  (the default for the C6). The SDIO pins on the C6 side are fixed in hardware.
- CP partition table. The example builds `partitions_eh_cp_ota_4m.csv` and 4 MB flash. The table is set
  by both `sdkconfig.defaults` and `sdkconfig.defaults.esp32c6`; the second is applied after the first
  and overrides it. For a C6 with 2 MB flash (the size — `esptool --chip esp32c6 -p <port> flash_id` over
  the C6's UART, section 5) set `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_eh_cp_ota_2m.csv"`
  and `CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y` in `sdkconfig.defaults.esp32c6`, delete `sdkconfig` and
  `build/` (defaults seed only a new `sdkconfig`), and after the build check `--flash_size 2MB` in
  `build/flash_args`. The slot of the 2 MB table is 960 KB (0xF0000), while the 3.0.7 image with the
  example's default configuration takes ~1.1 MB; IDF checks the image size against the app partition
  during the build.
- `eh.py` / `install.sh` from the repository are not needed: the example is an ordinary IDF project.

## 3. Flash the C6 through the P4 (updater, the standard path)

`tools/c6_updater/` is a separate IDF project for the P4: the example
`examples/ota/coprocessor_ota/mcu_host` from esp_hosted 3.0.7, method "Partition OTA"
(`CONFIG_OTA_METHOD_PARTITION`). It:

1. reads the CP image from the `slave_fw` partition of the P4's flash (0x630000, 2 MB);
2. verifies the image (section 3a);
3. compares the image's version with the version running on the C6 (the `v` prefix is dropped) and
   skips the OTA if they are the same;
4. sends the image to the C6 over SDIO (`esp_hosted_slave_ota_begin/write/end`), on CP ≥ 2.6.0
   activates it (`eh_host_cp_ota_activate()`) and restarts the P4.

The updater's partition table (`tools/c6_updater/partitions.csv`) repeats `partitions_p4_16mb.csv`
line for line: `slave_fw` sits where the player expects it, and the table the updater writes does not
drop `userdata`. The updater goes into `ota_0` in place of the player; `data`, `userdata` and NVS
are not touched (the project has no images for them).

```
cp esp_hosted/examples/ota/coprocessor_ota/cp/build/eh_cp_ota_coprocessor_ota.bin tools/c6_updater/components/ota_partition/slave_fw_bin/
cd tools/c6_updater
idf.py set-target esp32p4
idf.py build
idf.py -p <panel port> flash monitor
```

- Windows: before the build `$env:IDF_COMPONENT_CACHE_PATH = "<short folder>"` — the esp_hosted 3.0.7
  package does not unpack into the default cache (paths longer than 260 characters).
- `slave_fw_bin/*.bin` is not in git. `slave_fw` gets the `.bin` from this folder with the **newest file
  time**.
- The step that writes `slave_fw` calls esptool itself and takes the port from `ESPPORT` (set by
  `idf.py -p`) or `ESPTOOL_PORT`; without them esptool tries every serial port on the machine.
- If `idf.py flash` fails, flash directly from `build/`:

  ```
  python -m esptool --chip esp32p4 -p <port> -b 460800 write_flash "@flash_args"
  python -m esptool --chip esp32p4 -p <port> write_flash 0x630000 ../components/ota_partition/slave_fw_bin/eh_cp_ota_coprocessor_ota.bin
  ```

The updater's log (example — factory CP 2.3.2):

1. `eh_init_evt: esp-hosted fw versions: host=3.0.7 coprocessor=2.3.2` →
   `eh_init_evt: major version mismatch — OTA coprocessor from host`.
2. `host_performs_slave_ota: Starting OTA via Partition` →
   `ota_partition: image: <N> bytes, <k> segment(s), version v3.0.7, hash appended` →
   `Firmware verified - Size: <N> bytes, Version: v3.0.7` →
   `Version differs - proceeding with OTA from 2.3.2 to v3.0.7`.
   `<N>` is the image length that `tools/c6_image.py verify` prints; for a `.bin` from esptool it equals
   the file length.
3. `Partition OTA completed successfully - Sent <N> bytes`, `OTA completed successfully!`. Then on
   CP < 2.6.0 — `Activate API not supported (requires v2.6.0+)` (the C6 boots the new image itself), on
   CP ≥ 2.6.0 — `New firmware activated - slave will reboot`; then
   `Restarting host to resync with slave...`.
4. After the restart: `esp-hosted fw versions: host=3.0.7 coprocessor=3.0.7 (match)`,
   `RPC version negotiated: V2`,
   `Current slave firmware version (3.0.7) is the same as new version (v3.0.7). Skipping OTA.` →
   `OTA not required - slave firmware is up to date`. The updater answers this way on every start until
   the player is flashed (section 4).

The example's "host ↔ CP" check (`CONFIG_OTA_VERSION_CHECK_HOST_SLAVE`) is disabled: it compares the CP
with the example's compat constants (2.12.x), not with the host version. The gate is the comparison of
the image version with the CP version (`CONFIG_OTA_VERSION_CHECK_SLAVEFW_SLAVE`).

If the updater does not connect to the C6 (`eh_host_connect_to_slave()` returns an error,
`ESP_ERROR_CHECK` restarts the P4) — section 5.

## 3a. Image verification — twice, before it reaches the C6

The panel's Wi-Fi exists only on the C6, and only the P4 can flash the C6 over the air: an activated
corrupt image leaves the panel without a network, and after that only section 5 helps. So the image is
verified twice, with the same three checks: the header magic `0xE9` and a segment table that fits in
the file; the esptool byte checksum (`0xEF` xor all the bytes of segment data); the appended SHA-256
(when the header has `hash_appended`). The image length is computed the way esptool writes it: the
checksum is the last byte of a 16-byte block, there is no padding before the hash
(`tools/tests/test_c6_image.py`, eight alignments).

**On the PC, before copying into `slave_fw_bin/`:**

```
python tools/c6_image.py verify <image.bin>
python tools/c6_image.py record <image.bin> --tag v3.0.7   # record it as known-good
python tools/c6_image.py check  <any copy>                 # is it the same image
```

`record` writes `tools/c6_updater/known_good.json`: sha256, length, version, project, tag and the
esp_hosted version (`--hosted`, default 3.0.7). The image does not go into git; the fingerprint is meant
for git, but the repository does not have it, so `check` answers
`no fingerprint yet — run: python tools/c6_image.py record <image.bin>`.

**On the device, before the transfer:** `tools/c6_updater/components/ota_partition/ota_partition.c`
verifies the image in `slave_fw` **before** `esp_hosted_slave_ota_begin()`. If it does not match, not a
single byte goes to the C6 and activation is not called:

```
E (...) ota_partition: the image's own SHA-256 does not match its contents
E (...) ota_partition: REFUSING to flash the C6: the image in 'slave_fw' did not verify (ESP_ERR_INVALID_CRC)
E (...) ota_partition: Nothing was sent to the co-processor; it is still running what it had.
```

An image without an appended SHA-256 passes with the WARN
`no SHA-256 in this image - only the 1-byte checksum could be verified`.

## 4. Flash the player

The updater occupies `ota_0`, so after it comes a full flash of the player from the repository root:

```
idf.py set-target esp32p4
idf.py build
idf.py -p <panel port> flash
```

A full flash rewrites `data` from `data.bin`; it does not touch `userdata` and `slave_fw` (the player
build makes no images for them). After the start the log must have no `eh_init_evt: … version mismatch`
lines.

## 5. Recovery: flashing the C6 directly over UART (JP1)

When the updater cannot negotiate with the C6 (firmware too old, a corrupt image). The JP1 header
carries the C6's UART0: pins **18 / 20 / 22 / 24 = U0RXD, U0TXD, IO9 (BOOT), CHIP_PU (reset)** (JP1
pinout — [CONCEPT.md](CONCEPT.md), §4). Levels are 3.3 V.

1. **Stop the P4 first**: it drives GPIO54 (the C6 reset) and will reset the C6 in the middle of
   flashing. Put the P4 into download mode (holding BOOT — GPIO35, button SW1 on the board, in parallel
   with the encoder button — reset the P4) or flash onto the P4 an image that does not initialise
   ESP-Hosted. Do not open the P4 monitor during this time.
2. USB-TTL 3.3 V: adapter TX → U0RXD (18), adapter RX → U0TXD (20), GND ↔ GND.
3. C6 download mode: IO9 (22) to GND, a short pulse of CHIP_PU (24) to GND, release IO9.
4. Flash the images from the CP build directory; take the offsets from its `build/flash_args`, do not
   guess them (for both of the example's tables: `0x0 bootloader.bin`, `0x8000 partition-table.bin`,
   `0xd000 ota_data_initial.bin`, `0x10000 eh_cp_ota_coprocessor_ota.bin`; `--flash_size` — per the
   configuration, section 2):

   ```
   cd esp_hosted/examples/ota/coprocessor_ota/cp
   idf.py -p <USB-TTL port> flash      # or: cd build; python -m esptool --chip esp32c6 -p <port> write_flash "@flash_args"
   ```

5. Take IO9 off GND, pulse CHIP_PU — the C6 starts with the new firmware. Restart the P4.

## 6. Host configuration the C6 link depends on

- **C6 reset polarity.** GPIO54 goes straight to CHIP_PU: LOW — reset, HIGH — run. In ESP-Hosted 3.x
  this is `CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_LOW=y`. With `ACTIVE_HIGH` the pin sits LOW, the C6 is
  held in reset, and the host sees only `sdmmc_init_ocr … 0x107`.
- **Chip revision.** The panel has ESP32-P4 **v1.3**. IDF 5.5 builds for v3.1+ by default, and the
  bootloader/esptool refuse (`requires chip revision in range [v3.1 - v3.99]`). So the three
  projects — the player (`boards/p4_player/sdkconfig.board`), `tools/c6_updater/sdkconfig.defaults`,
  `tools/sdio_soak/sdkconfig.defaults` — set `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` and
  `CONFIG_ESP32P4_REV_MIN_100=y`.
- **The SDIO block** is identical in these three files and must stay so: slot 1, 4 bits, 40 MHz,
  CLK 18, CMD 19, D0–D3 14–17, reset GPIO54, `CONFIG_ESP_HOSTED_HOST_SDIO_RX_STREAMING_MODE=y`.

## 7. Changing the esp_hosted version

The version is recorded in several places, and all of them must change together:

| Where | What |
|---|---|
| `components/modesp_net/idf_component.yml` | `espressif/esp_hosted`, `espressif/esp_wifi_remote` — player |
| `tools/c6_updater/main/idf_component.yml` | the same two packages — updater |
| `tools/sdio_soak/main/idf_component.yml` | the same two packages — soak test |
| `modules/ui_lvgl/src/screen_set.cpp` | the text `C6 3.0.7` in the "Service" row |
| `tools/c6_image.py` | the default of `record --hosted` |

`components/hosted_mem` (`CONFIG_HOSTED_MEM_FRAMES_IN_PSRAM`, default `y`) wraps esp_hosted's internal
function `eh_host_port_dma_alloc()` and repeats its prototype. CMake stops the build if
`port/os/include/eh_host_port_dma.h` has disappeared or the prototype in it has changed: the wrapper has
to be checked against the new version.

After the change: build the CP from the same tag (section 2) → put the `.bin` into `slave_fw_bin/` → run
the updater (section 3) → flash the player (section 4).
