**English** · [Українська](README.uk.md)

# sat_bridge

A UART bridge for the ESP32-S3 board whose Bluetooth satellite is a bare ESP32-WROOM soldered on
(`boards/s3_player`, [S3_PORT.md](../../docs/S3_PORT.md), "Wiring"). It runs on the S3 in the
player's place, passes every byte between the board's COM connector (UART0) and the satellite's
UART0 (GPIO11/12) at 115200, and drives the satellite's EN and IO0 (GPIO13, GPIO2) on command, so
that esptool on the PC reaches the satellite's ROM loader. Until a command it leaves both pins
alone.

| Command (`sat_cmd.py`) | The satellite |
|---|---|
| `#SAT-DL#` | resets into its ROM loader (IO0 low through the reset) |
| `#SAT-RUN#` | resets into its application |

## Flashing the satellite

From ESP-IDF v5.5.5's environment; `<port>` is the COM connector.

1. Here in `tools/sat_bridge`: `idf.py set-target esp32s3 && idf.py -p <port> flash`. The bridge
   is built with the player's own partition table (`partitions_s3_16mb.csv`) and flash settings,
   so it lands in ota_0 like the player and leaves the player's settings, files and user data in
   place.
2. `python sat_cmd.py <port> '#SAT-DL#'`: the answer ends in `waiting for download`.
3. From the satellite's build directory (modesp-bt-satellite, the bare-module build with
   `sdkconfig.bare`), at 115200 and without the reset lines: the bridge does not change rates, and
   the COM connector's lines reset the S3, not the satellite.
   `python -m esptool --chip esp32 -p <port> -b 115200 --before no_reset --after no_reset write_flash @flash_args`
4. `python sat_cmd.py <port> '#SAT-RUN#'`
5. The player back, from the repository root:
   `idf.py -B build_s3 -D SDKCONFIG=build_s3/sdkconfig -p <port> flash`. Its console then says
   `Bt: link up: sat ...`.

A satellite with Secure Boot V2 and flash encryption burned into its eFuses takes only images
signed with its own key, written encrypted: the build's `encrypted_app-flash_args` (`--encrypt`),
never the bootloader, never `--force`. esptool refuses to erase such a chip; write 0xFF instead.
