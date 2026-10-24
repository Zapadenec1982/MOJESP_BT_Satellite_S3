# 2026-09-20 — Bluetooth cover-art bench: the two working patches and the deployed hashes

Copied on 2026-09-21 from `D:\P4_DSP\bench_logs\` (the capture directories and build/flash
logs referenced by `docs/BT_COVER_2026-09-20.md` stay there; they are not evidence of a
decision, only of a run). Context for every file: `docs/BT_COVER_2026-09-20.md`.

| file | what |
|---|---|
| `cover-firmware-hashes.json` | SHA-256 of the P4 and satellite (ESP32) ELF/BIN pair of the first cover deployment (00:39) — the same four hashes as `BT_COVER_2026-09-20.md` "Deployed images" |
| `cover-screen-index.patch` | 00:34, `modules/ui_lvgl/src/screen_now.cpp`: pick the cover slot by audio source (Bluetooth vs file) so a stale file cover does not show on the BT screen |
| `cover-p4-bench-input.patch` | 01:13, the P4-side working set that went into the bench build: `components/cover/src/cover.cpp` (JPEG MCU-aligned layout instead of a blanket 16-px round-up, grayscale falls back to the software path), `modules/dlna/*` and `modules/ui_lvgl/*` wiring |

Neither patch applies to `stage4/dlna` HEAD in either direction: their substance landed
in reworked form — the JPEG layout in 5e3ec11 (`fix(cover): use native RGB565 byte order
and JPEG MCU stride`), the cover-source selection in d1b06c7 / 86cf394. They are kept as
the exact input of the bench runs whose hashes and captures the journal cites, not as
something to apply.
