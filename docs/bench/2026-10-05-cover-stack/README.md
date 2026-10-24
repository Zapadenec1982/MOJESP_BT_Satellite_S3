# 2026-10-05 — stack the software cover decoder needs, and which tasks it overflowed

The radio art task (8 KB) rebooted the panel on a PNG station logo the same morning
(2c771d4). The question here: how much stack does `stb_image` actually take through
`cover_set_jpeg()` → software path, and which of the five tasks that call it can hold it.
Measured at 9ad3ce6 (main) off the panel, then confirmed on it with the fix (last section).

## Method

1. **Measured.** The firmware's own `cover_soft.c.obj` from the esp32p4 build (SHA-256
   `ac377191c58e98d9c7eb0ee1eab80de38f3a23c8fe2a0412f880182193cb34cc`, unchanged by the fix)
   linked unmodified into a bare-metal probe (`probe/`) and run on QEMU's RV32 `virt` machine.
   The probe paints the stack, calls `cover_soft_info()` and `cover_soft_decode()` for each test
   image, and scans for the deepest word touched (two patterns, so a frame that stores the
   pattern cannot hide a word). The object is integer-only — no FPU or PIE instructions — so
   a generic RV32 core runs exactly the code the panel runs. 24 images (`gen_images.py`): every
   PNG colour type and bit depth stb distinguishes, Adam7, the three deflate block kinds, and
   the JPEGs the hardware engine refuses (progressive 4:2:0/4:2:2/4:4:4, grayscale, CMYK,
   restart markers, odd sizes). Output: `qemu_probe.txt`.
2. **Static.** The same sources recompiled with the build's exact flags plus
   `-fstack-usage -fcallgraph-info=su` (`probe/su_compile.py`), and the call graph walked from
   each task's entry through `cover_set_jpeg()` (`probe/ci_walk.py`). Output:
   `static_chains.txt`. The static worst case under `cover_soft_decode()` is 8752 B; the
   measured one is 8736 B.

Tools: riscv32-esp-elf esp-14.2.0_20260121 (the build's), qemu-system-riscv32
esp_develop_9.2.2_20260417, Pillow 12.3.0 from the IDF v6.1 venv for the JPEGs.

## Result

| under the call | PNG | progressive JPEG |
|---|---|---|
| `cover_soft_info()` | 1608 B | 456 B |
| `cover_soft_decode()` | **8736 B** for every image with a dynamic-Huffman block (6192 B fixed-code only, 6036 B stored only) | 868 B |

The PNG chain: `stbi__parse_png_file` 1296 (a 1 KB palette) → `stbi_zlib_decode_malloc_guesssize_headerflag`
4112 (the inflate state, both Huffman tables inside it) → `stbi__parse_zlib` 64 →
`stbi__compute_huffman_codes` 2544 (the code-length table) → `stbi__zbuild_huffman` 208,
under 528 B of `stb_image` entry frames.

Fixed costs on every P4 task, on top of the call chain: TLS 32 B and the coprocessor
pointer block 32 B at the top, the task wrapper's 16 B, the canary's 16 B, and the
interrupt frame, 160 B, if an interrupt lands at the deepest point: 256 B. Up to 387 B more
for the FPU/HWLP/PIE save areas, carved from the bottom the first time a task uses one.

| task (`cover_set_jpeg` caller) | stack | PNG through the software path, before | after |
|---|---|---|---|
| `bt_cover` (internal RAM) | 6 KB | 9232 B + fixed: **short by 3.3–3.6 KB** | 736 B + logging |
| `ap_cover` (PSRAM) | 6 KB | 9216 B: **short by 3.3–3.6 KB** | 720 B + logging |
| `player` (internal RAM) | 8 KB | 10208 B: **short by 2.2–2.6 KB** | 1712 B + logging |
| `dlna_client` (PSRAM) | 12 KB | 11888 B: **between 144 B to spare and 243 B short** | 3392 B + logging |
| `radio_art` (PSRAM) | 16 KB since 2c771d4 (8 KB before) | 10624 B: 5.0–5.4 KB free (at 8 KB short by 2.6–3.0 KB: the reboot) | 2128 B + logging |
| `cover_soft`, the fix | 16 KB (PSRAM) | — | **8880 B + fixed: 6.7–7.1 KB free** |

"Logging" is an `ESP_LOGx` at the deepest point of the hardware path: about 1.9 KB more,
1152 B of it newlib's `_vfprintf_r` frame. `bt_cover` is created with `xTaskCreate()`, so
its stack is internal RAM, not PSRAM.

Every PNG cover from Bluetooth, AirPlay or a file named `cover.jpg` that is really a PNG
was a stack overflow; DLNA artwork, which is why PNG support exists, had nothing to spare.
Progressive JPEG fitted everywhere.

## The fix

The software path runs on one task owned by `components/cover` (`SOFT_STACK`, 16 KB, PSRAM,
core 0, the caller's priority per job); callers wait for it under the existing decoder lock.
Its INFO line reports the task's lifetime high-water mark — on a panel,
`curl -X POST http://<panel>/api/loglevel -d '{"tag":"Cover","level":"info"}'` and play a PNG
cover: `artwork WxH -> 280 px (software; decoder stack never below N B free)`. These
figures predict N = 7440 B, down to 6893 B if an interrupt lands at the deepest point of a
task that has used every coprocessor.

## On the panel (05.10.2026, 22:48–22:51)

Image: 14eeaf7 built with the board's sdkconfig except `CONFIG_LOG_DEFAULT_LEVEL_INFO`
(`_local/build/cover-soft-bench-14eeaf7.elf`, SHA-256
`fbbd3d548548791e7684cbf8c7bcb67aa6cb15e1f6b7b3448cc7b39d782c65f0`), app-flashed to `ota_0`;
the slot was read back first and restored byte-exact afterwards. Covers went in through the
DLNA renderer — the 12 KB `dlna_client` path that had nothing to spare before — with
`panel/dlna_art.py`: `SetAVTransportURI` on a stopped transport selects the track without
playing it and hands its `albumArtURI` to the worker, which fetched each image itself.
Console read with `panel/capture.py` (DTR/RTS low, no reset): `panel_console.txt`.

| cover | kind | path | decoder stack never below |
|---|---|---|---|
| Hit FM's logo, the PNG that rebooted the 8 KB art task | PNG 120×120 | software | 7404 B |
| python.org logo | PNG 580×164 | software | 7404 B |
| Wikimedia `PNG_transparency_demonstration_1.png` | PNG 800×600 RGBA | software | **7260 B** |
| picsum.photos/600 | progressive JPEG 600×600 | software | 7260 B |
| Wikimedia `JPEG_example_flower.jpg` | progressive JPEG 500×477 | software | 7260 B |
| Hit FM's logo again | PNG 120×120 | software | 7260 B |
| gstatic `webp/gallery/1.jpg` | baseline JPEG 550×368 | hardware | — |
| a Bluetooth cover from the phone, mid-test | baseline JPEG 200×200 | hardware (`bt_cover`) | — |

7260 B free is 9124 B used of 16384, inside the 6893–7440 B predicted above. No reset and no
panic; the only E/W lines in the capture are the three known GENA "Error parse url" and the
two "SOS encountered before SOF0" with which the engine refused the progressive JPEGs.
The radio, AirPlay and card paths were not fed a PNG here; they hand over through the same
`set_jpeg_soft()`.

## Re-running

```
python probe/gen_images.py            # needs Pillow; writes probe/images/ and probe/images.c
sh probe/run.sh <build>/esp-idf/cover/CMakeFiles/__idf_cover.dir/src/cover_soft.c.obj
python probe/su_compile.py <build>/compile_commands.json <outdir> components/cover/src/cover_soft.c ...
python probe/ci_walk.py <outdir> <task entry> --stop cover_soft_decode=8752 --via _ZN11player_core14cover_set_jpeg
```

| file | SHA-256 |
|---|---|
| `qemu_probe.txt` | `cdb97dc4e1453c3bd04e9bd6f8ea9afd8d49ef1f76e461bd1eaa58871427ebab` |
| `static_chains.txt` | `d95a1a28bbb66b224982d50a04095d12c4db904e63029d5d3c84bf6445082263` |
| `panel_console.txt` | `596fb3612ceb43a465598649e38b56acf3286c4695763df930dbfcf0802c54af` |

`panel/` holds the two helpers exactly as they ran; the addresses in them are the bench LAN's.
