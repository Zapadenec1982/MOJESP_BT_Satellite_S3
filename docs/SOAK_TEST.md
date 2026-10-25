**English** · [Українська](SOAK_TEST.uk.md)

# SDIO soak test (ESP-Hosted P4 ↔ C6)

`tools/sdio_soak/` is a separate IDF project for the P4 without ModESP. It connects to Wi-Fi through the
C6, pulls a single HTTP(S) stream for hours into a PSRAM ring that a "decoder" drains at a set rate, and
counts breaks, stalls and free memory. The ESP-Hosted block in `tools/sdio_soak/sdkconfig.defaults`
(SDIO pins, 40 MHz, reset, P4 revision) is the same as in `boards/p4_player/sdkconfig.board` and must
stay so. The rest of the network stack differs: here lwIP's TCP window is the default, there is no
`components/hosted_mem` (copies of esp_hosted frames go into internal DMA RAM), and mbedTLS allocates
from internal RAM.

## What it prints

Every `SOAK_REPORT_SEC` (30 s) — one line for `grep SOAK`:

```
SOAK t=01:23:45 rx=123.4MB kbps=318 ring=61% reconn=2 restart=0 stall=0 http_err=2 wifi_drop=0 rssi=0 dma=380.1K dma_min=372.9K dma_big=335.9K int=401.2K int_min=398.0K psram=31.5M
```

| Field | Meaning |
|---|---|
| `t` | time since start |
| `rx`, `kbps` | total received; average rate over the last period |
| `ring` | fill of the PSRAM ring (`SOAK_RING_KB`, 512 KB), which is drained at `SOAK_DRAIN_KBPS`; draining starts at 25 % fill |
| `reconn` | reopenings of the stream after an error, a timeout or a non-2xx response |
| `restart` | the stream ended without an error with a 2xx response (a finite file reached its end, a LAN run) and was opened again — not an error |
| `stall` | `SOAK_STALL_SEC` (5 s) without a single byte — the main "link hung" indicator |
| `http_err` | those of `reconn` where `esp_http_client_perform()` returned an error (connection, TLS, socket timeout `SOAK_HTTP_TIMEOUT_MS`); a non-2xx response is not counted here |
| `wifi_drop` | `STA_DISCONNECTED` events |
| `rssi` | with esp_hosted 3.0.7 always 0: the `sta_get_ap_info` response contains no rssi, and the host cannot compose `esp_wifi_sta_get_rssi()` (`pack_req_payload failed msg_id=341`); likewise `wifi.rssi` in the player |
| `dma`, `dma_min`, `dma_big` | free DMA-SRAM now / minimum / largest free block |
| `int`, `int_min`, `psram` | free internal SRAM now / minimum; free PSRAM |

`dma_min` and `int_min` are the minimum since start, and in a phased run — since the start of the current phase.

A stream break is logged separately: `stream broke: <error>, HTTP <code>, after <T> ms — reconnect #N in
M ms`. Reconnection with a backoff of 1 → 30 s, with no limit on attempts; the end of a finite file is reopened
immediately. `STALL #N: no data for 5 s (ring P%)` — at the start of a stall, `data resumed` — when data
flows again. After `SOAK_DURATION_MIN` (default 120 min; 0 — no summary) a summary is printed
once:

```
SOAK DONE after 120 min: rx=… reconn=… restart=… stall=… http_err=… wifi_drop=… ring_underruns=… ring_overflow=…B dma_min=… int_min=…
```

`ring_underruns` — how many times the ring emptied and the "decoder" waited for a new fill to 25 %,
`ring_overflow` — bytes that did not fit into the ring (draining slower than reception). After the summary the test
keeps running.

## Settings (`menuconfig` → "SDIO soak test")

| Option | Default | What it sets |
|---|---|---|
| `SOAK_URL` | `https://icecast.radiofrance.fr/fip-midfi.mp3` | the stream, `http://` or `https://` |
| `SOAK_DURATION_MIN` | 120 | when to print `SOAK DONE`; 0 — never |
| `SOAK_REPORT_SEC` | 30 | period of the `SOAK` line |
| `SOAK_RING_KB` | 512 | size of the PSRAM ring |
| `SOAK_DRAIN_KBPS` | 128 | "decoder" rate, kbit/s; 0 — take everything at once (pure throughput) |
| `SOAK_STALL_SEC` | 5 | `stall` threshold |
| `SOAK_HTTP_TIMEOUT_MS` | 10000 | HTTP socket timeout |
| `SOAK_PHASE_MIN` | 0 | phase length, min; 0 — no phases |
| `SOAK_BALLAST_TARGETS` | `0` | free DMA-SRAM targets per phase, KB, comma-separated |
| `SOAK_WIFI_SSID`, `SOAK_WIFI_PASS` | empty | the network; empty — from the player's NVS |

## Runs

1. **Control, without internet and TLS:** a large file (FLAC) on an HTTP server on the local network,
   for example `python -m http.server 8000` in the folder with the file →
   `SOAK_URL = http://<PC ip>:8000/track.flac`, `SOAK_DRAIN_KBPS = 1000` or 0. The file is reopened
   on EOF (`restart`).
2. **Real path, HTTPS:** a public MP3 station, the default `SOAK_URL` above (128 kbit/s),
   `SOAK_DRAIN_KBPS` = the station's bitrate. A larger drain empties the ring (`ring_underruns`) — that
   is not a link error.
3. **Controlled bitrate:** `tools/sdio_soak/rate_server.py` on the PC serves an endless HTTP body
   (chunked, a pseudo-random incompressible block) at a rate that can be changed without breaking
   the connection:

   ```
   python tools/sdio_soak/rate_server.py --port 8000 --rate-file rate.txt   # --rate <kbit/s> — initial, if the file does not exist yet
   echo 320 > rate.txt                                                       # re-read every second; 0 = ceiling
   ```

   `SOAK_URL = http://<PC ip>:8000/`. The content is not checked on the panel — the test counts bytes;
   packet loss is measured separately by ICMP-pinging the panel from the same PC, in parallel with the stream.

Each run: at least 2 hours, preferably overnight.

## Phased run: pressure on DMA-SRAM

On the player, the internal DMA-SRAM from which ESP-Hosted takes its buffers is shared with the I2S DMA
and the rest of the firmware, so a link measured with hundreds of free kilobytes says nothing about the
player. With `SOAK_PHASE_MIN > 0` the test cycles through `SOAK_BALLAST_TARGETS` (up to 8 targets): before each phase
it takes DMA-SRAM in 4 KB blocks (up to 2 MB) until no more than the target is left free (0 — no ballast),
and holds it like that for `SOAK_PHASE_MIN` minutes. List the targets from the largest free amount to the
smallest: tight phases can cut the run short. The player's actual free DMA-SRAM is visible in its line
`main: Heap …: … DMA <N> (largest <M>) …`.

At the start and end of each phase:

```
SOAK PHASE <i>/<n> target=<target>K ballast=<taken>K dma_free=<…>K dma_big=<…>K
SOAK PHASE END <i>/<n> target=<target>K ballast=<taken>K kbps_avg=<…> rx=<…>MB reconn=<…> stall=<…> http_err=<…> wifi_drop=<…> dma_min=<…>K int_min=<…>K
```

`ballast` — how much is taken; `kbps_avg`, `rx`, the counters and minima — for this phase only. The bitrate
is changed from the PC side (`rate_server.py`), so the "memory × bitrate" grid can be covered in one run
without reflashing.

## TLS: 16 KB input buffer

`CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384` (OUT — 4096). The `SOAK_URL` station ignores
`max_fragment_length` and sends 16 KB records; a record that does not fit into the input buffer is not decrypted,
and with 4096 every read fails with `esp-tls-mbedtls: read error :-0x7200` about a second in. The player
has the same value (`boards/p4_player/sdkconfig.board`). In the player mbedTLS allocates from PSRAM
(`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`); the soak test does not have this option, so the TLS session takes internal
RAM and this is visible in `int` / `dma`.

## Running it

Prerequisites: the C6 is already on CP 3.0.7 ([C6_UPDATE.md](C6_UPDATE.md)); the player has connected to Wi-Fi at least once — the test
reads the network from the same NVS (namespace `wifi`, keys `ssid`, `pass`). If the NVS requires erasing, the test
refuses to run, so as not to lose the player's credentials.

```
cd tools/sdio_soak
idf.py set-target esp32p4
idf.py menuconfig          # "SDIO soak test"
idf.py build
idf.py -p <panel port> flash
idf.py -p <panel port> monitor | tee soak.log
```

- Windows: before the build `$env:IDF_COMPONENT_CACHE_PATH = "<short folder>"` (as for the player);
  log — `| Tee-Object -FilePath soak.log`. Without `idf.py`: `python -m esp_idf_monitor -p <port>
  build/sdio_soak.elf`.
- `idf.py monitor` resets the P4 when it attaches (DTR is the USB-Serial/JTAG reset line), and the test starts
  over. Attach at the start of the run and do not reattach.
- The partition table (`tools/sdio_soak/partitions.csv`) is `partitions_p4_16mb.csv` line for line. The test
  goes into `ota_0` in place of the player; NVS, `data` and `userdata` are not touched.
- To return the player: from the repository root, `idf.py -p <panel port> flash`.

## Reading the result

- `stall` > 0 after which `reconn` does not help and `rx` does not grow — the link is hung. This is a negative result.
- `stall` > 0 that ends with `data resumed` or a reconnect within seconds — a pause, not a
  hang; look at how often.
- A few `reconn` per night with instant recovery and `stall = 0` — normal for internet radio (servers
  drop connections themselves); then look at `wifi_drop` and `http_err`.
- `ring_underruns` with `stall = 0` — the drain is higher than the source's real bitrate, not a link problem.
- `dma_min` — the lowest free DMA-SRAM alongside the stream; in a phased run — compare across the phases'
  `SOAK PHASE END` lines: the phase where `stall` / `reconn` grow or `kbps_avg` falls shows the memory threshold
  below which the link degrades.
