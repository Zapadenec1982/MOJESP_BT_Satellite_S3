# 2026-09-16 — SDIO soak: HTTPS internet radio through the C6, 8 h 28 min

`soak_log.txt` is the COM12 capture of `tools/sdio_soak/` (`docs/SOAK_TEST.md`, run 2 —
the real HTTPS path), copied byte-exact from `D:\P4_DSP\soak_log.txt` on 2026-09-21
(SHA-256 `2a1beda0e734cc179a14a6c1d0f508797eb9c930b4735cab3b0ea8cd490fd1f2`). Logger
started 2026-09-16 05:22:09 and stopped 13:50:24; one `SOAK` line every 30 s, 1015 of
them, the last at `t=08:28:13`. The boot banner was cut by the logger, so the station URL
is not in the capture; the rate says which run it was.

## Result, read with the "Як читати результат" rules of SOAK_TEST.md

- 465.1 MB received at a steady 127–128 kbit/s — the station's own bitrate, consistent with
  the default 128 kbit/s HTTPS station and `SOAK_DRAIN_KBPS = 128`.
- Final counters: `stall=2 reconn=1 http_err=1 wifi_drop=0 restart=0`.
  - `t=01:42` — `STALL #1: no data for 5 s (ring 18%)`; data resumed 0.6 s later, no reconnect.
  - `t=06:59` — `STALL #2` with the ring at 0 %, then `esp_tls_conn_read error, errno=No more
    processes`, `HTTP_CLIENT: Incomplete chunked data received 0`, `stream broke:
    ESP_ERR_HTTP_INCOMPLETE_DATA, HTTP 200, after 25186695 ms — reconnect #1 in 1000 ms`;
    the certificate validated and data resumed about 2 s after the break.
- Memory flat for the whole run: DMA 406.5 K (min 400.7 K, largest block 296.0 K),
  internal 445.1 K (min 439.3 K), PSRAM 31.5 M.
- `rssi=0` is the esp_hosted 3.0.7 limitation described in SOAK_TEST.md, not a reading.

Verdict by those rules: no silent death of the link. Both stalls recovered on their own,
the single reconnect was the server ending a chunked stream and the back-off healed it
within 2 s, and there was no Wi-Fi drop in 8.5 hours. The tool's `sdkconfig.defaults`
carries a 16 KiB TLS input buffer since the 15.09 finding (SOAK_TEST.md); the player's
4 KiB budget is the open question plan item 7.6 carries.
