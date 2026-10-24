# VERDICT: is modesp-player failing to use what Espressif already provides?

Ten research agents swept the ecosystem, a skeptic checked each one and refuted a significant fraction. What follows is what survived.

---

## 1. THE HONEST ANSWER

No, hand-writing this was not a mistake, and in four places it was the only correct answer: nothing in Espressif's stack de-interleaves ICY metadata, nothing reads Vorbis comments or RIFF INFO tags, nothing provides a lock-free float-frame SPSC ring, and no BSP exists for the Guition JC4880P443C. Those four were verified by reading Espressif's own source, not by trusting a search that came back empty. The I2S driver wrapper is also correct, because ESP32-P4 below chip revision v3.0 has no 160 MHz PLL clock source for I2S at all, which forces APLL and makes the clocking bespoke by construction.

What was a mistake is the procedure, not the code. Three of the four pain points were answerable from a primary source before a line was written, and two of them from files already on this machine. The esp_audio_codec revision gate is stated in plain English in the component's own changelog, reachable in one HTTP GET. The APLL reference-count freeze is a commented 30-line function in the local IDF checkout, and ESP-IDF 5.5.5 already ships the release-then-acquire escape the project tried to hand-roll. Half the stated reason for abandoning esp_http_client is factually wrong: IDF's bundled http_parser has dedicated ICY status-line states and parses "ICY 200 OK" correctly. Separately, the registry contained espressif/esp_playlist, espressif/esp_extractor and espressif/jsmn, which together overlap roughly 1,100 lines that were written by hand. So: the judgement was sound and the research was absent. Fixing the research habit is worth more than any single adoption below.

---

## 2. WHAT WE SHOULD SWAP IN

Only items where P4 rev v1.3 compatibility was established by reading the package, the ISA attributes of its prebuilt archive, or IDF source. Ranked by value for effort.

| # | Component / change | Exact name and version | Replaces | Effort | Risk |
|---|---|---|---|---|---|
| 1 | P4 revision pin | `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y`, `CONFIG_ESP32P4_REV_MIN_100=y` moved into `sdkconfig.defaults.esp32p4` | nothing; makes an existing setting durable | 2 lines | none. IDF 5.5.5 defaults to `ESP32P4_REV_MIN_301`, so a deleted sdkconfig or a CI checkout silently builds an image this board rejects |
| 2 | Dependency floor | `espressif/esp_audio_codec: ">=2.5.0,<2.6.0"` in `modules/radio/idf_component.yml` | current `>=2.0.0` floor | 1 line | none. The 2.4.1 package contains no OGG parser at all, so `ESP_AUDIO_SIMPLE_DEC_TYPE_OGG` at `radio_module.cpp:243` cannot compile against it |
| 3 | FATFS stdio buffer | `CONFIG_FATFS_VFS_FSTAT_BLKSIZE=4096`, plus `setvbuf` at `player_module.cpp:365` and `:1039` | 128-byte newlib buffers on tag, cover and state reads | 3 lines | none. Espressif's own combined SD+SDIO example marks this "do not remove" |
| 4 | JSON tokeniser | `espressif/jsmn: "~1.1"` (1.1.0, MIT) | `components/jsmn` (471 lines) | delete a directory, 2 manifest lines | very low. Package file is 477 lines, reformatted, not byte-identical. Diff before deleting. Also fix the CMakeLists comment: jsmn is MIT, not public domain |
| 5 | Real-input FFT | `espressif/esp-dsp` 1.8.2 (already a dependency): half-length `dsps_fft2r_fc32` + `dsps_cplx2reC_fc32` | the full complex transform over zero-padded reals in `spectrum.cpp:104-107` | benchmark, then part of one file | low. Halves analyser FFT work with no new dependency. `dsps_cplx2real_fc32` is the radix-4 equivalent |
| 6 | Stereo biquad | `esp-dsp` `dsps_biquad_sf32` (has `dsps_biquad_sf32_arp4.S`, dispatches on P4) | de-interleave loop, re-interleave loop and per-band `memcpy` in `DspChain::run_eq` | one function plus `w[2]`→`w[4]` | low functionally. Preconditions verified: L and R already share coefficients, ANSI reference is in-place safe. Shares the `arp4` exposure in open issue #119 (see section 6) |
| 7 | Compiler flags | `target_compile_options` `-O2` on the esp-dsp managed component | its current global `-Os` build | one build flag | low, flash only. Every esp-dsp module without P4 assembly (all of math, windows, conv/corr, `dsps_firmr_f32`, both resamplers, and `dsps_bit_rev_fc32` on the analyser path) is currently built for size. Espressif's own figures: 1.90x on `dsps_fir_f32`, 1.33x on the 1024-point FFT |
| 8 | SD multi-block reads | `sdmmc_host_t::unaligned_multi_block_rw_max_chunk_size` = 8 or 16, plus a 64-byte-aligned player read buffer of a length that is a multiple of 64 | single-block CMD17 fallback on unaligned PSRAM destinations | ~5 lines in `storage_module.cpp` plus one allocation | low. Documented under "Unaligned Buffer Performance". `dma_aligned_buffer` is auto-allocated at 512 bytes, so pre-allocating only matters once chunk size exceeds 1 |
| 9 | SDIO RX depth | `CONFIG_ESP_HOSTED_HOST_SDIO_RX_STAGING_SLOTS` 2 → 4 | nothing; untouched default | 1 line, 1 bench run | low. Default 2 gives effective queue depth 1. Its own Kconfig names 4 and says "Tune on hardware". Cost lands in PSRAM given `EH_HOST_PORT_DMA_PREFER_SPIRAM=y` |
| 10 | Rate converter (bench only) | `espressif/esp_audio_effects: "==1.3.0~1"` (`rate_cvt` standalone, no gmf_core) | candidate for `components/audio_resampler` | swap behind a compile switch, one measurement | medium. See caveats below |
| 11 | ID3 parser (conditional) | `espressif/esp_extractor: "~1.0.5"` | the ID3v2 half of `components/audio_tags` (of 388 lines) | one file, if an extractor is already open | medium. See caveats below |
| 12 | Playlist and catalog (spike) | `espressif/esp_playlist: "0.5.1"` + `espressif/cjson` | `components/playlist`, `components/stations`, indexing half of `modules/storage` (~480 lines) | days, behind a thin adapter | medium. See caveats below |

**Caveats on 10, 11, 12.**

Row 10: `esp_audio_effects` 1.3.0~1 was downloaded and verified to have no revision check in its CMakeLists and to ship `lib/esp32p4/libesp_audio_effects.a`. Three things it is not: the 4x P4 assembly for `rate_cvt` landed in 1.4.0, in the same release as the `require ESP32-P4 chip version >= 3.0` CMake gate, so this board gets the plain C path; it accepts s16/s24/s32 only while `audio_pipe.h:26,59` is interleaved float, so the benchmark must include the conversions on every block; and it pulls `espressif/gmf_fft ~1.0` transitively, which is another prebuilt component needing its own gate and licence check. Use exactly `1.3.0~1`, not `1.3.0`, which has the `esp_gmf_fft_init` link errors. Set the bar honestly: if it is not clearly faster including conversions, revert and close the question, because the fast version is unreachable on this silicon.

Row 11: no gate in the CMakeLists and the P4 archive reports only `xespv2p1`, so it will build. The parser ships as compilable source, handles v2.2 through v2.4, normalises to UTF-8 and extracts embedded APIC art into `cover`/`cover_mime`/`cover_size`. Two blockers: `esp_extractor_id3_parser_open()` takes an `esp_extractor_handle_t`, so without an extractor in the pipeline you open one purely to read tags; and `include/esp_extractor_id3_parser.h` carries `SPDX LicenseRef-Espressif-Proprietary` while the package LICENSE is Espressif Modified MIT. Resolve that before adopting. The control symbols are `ESP_EXTRACTOR_CTRL_TYPE_SET_ID3_PARSER` (0x47) and `ESP_EXTRACTOR_CTRL_TYPE_GET_ID3_BASIC` (0x82).

Row 12: pure C source with no prebuilt archive, so it cannot carry a revision gate. Scans a tree with extension filters and a callback, persists JSON, does repeat and shuffle, multiple instances. Downgraded from "adopt" to "spike behind an adapter" because it is 0.5.x with 0 registry downloads, its catalog schema is `name` plus `url` only (its README: public scan and add APIs do not write album or artist), and its docs state it does not parse M3U or PLS, so `components/playlist` does not disappear.

### Defects found in passing, worth fixing regardless

| File | Defect |
|---|---|
| `drivers/i2s_std/src/i2s_std_driver.cpp` | `I2sStdDriver::start(uint32_t)` builds `I2S_STD_CLK_DEFAULT_CONFIG` (whose `clk_src` is `I2S_CLK_SRC_DEFAULT`, which is XTAL on this silicon), reconfigures **both** channels, discards the return value and leaves `rx_cfg_apll_`/`rx_apll_ref_` untouched. It releases both APLL references behind the bookkeeping and drops the DAC onto the crystal. The next `set_rate()` park underflows `assert(s_apll_ref_cnt > 0)`. Same reboot, second route, never looked at |
| `modules/radio/src/radio_module.cpp:243-244` | `"ogg"` is tested before `"flac"`, so an Ogg FLAC station is routed to the OGG parser, which handles Vorbis and Opus only. Reordering alone is insufficient: bare `audio/ogg` contains no `flac` substring |
| `modules/player/src/player_module.cpp:101-104` | `type_from_name()` maps only `.mp3`, `.flac`, `.wav` while `modules/player/idf_component.yml` advertises M4A. Close the gap or drop the claim |
| `modules/radio/src/radio_module.cpp` AAC path | esp_audio_codec 2.6.2 fixed an AAC-LC mono out-of-bounds write when packing interleaved mono output. There is no 2.5.x backport and no component swap escapes it. Size the decode buffer from `esp_audio_simple_dec_out_t.needed_size` on `ESP_AUDIO_ERR_BUFF_NOT_ENOUGH` and over-allocate for AAC. Label it hardening, not a fix: the bug is in a prebuilt binary nobody outside Espressif has seen |
| `components/jsmn/CMakeLists.txt` | Calls jsmn "public domain". The MIT header sits in the same directory |
| `modules/radio/src/radio_module.cpp:364-369` | Comment dated "bench 18.09.2026", which is tomorrow. Settle whether the `esp_http_client` assert was ever observed on this board or inferred from source |

### Explicitly rejected

ESP-GMF and ESP-ADF as the project's pipeline. They would replace `modules/player`, `modules/audio` and `drivers/i2s_std` (2,545 lines, recounted) and cannot replace `modules/radio` (1,246) or `components/audio_tags` (446). The one concrete win, the 4x `rate_cvt`, is gated behind rev 3.0. `gmf_io`'s `esp_gmf_io_http.c` would reintroduce the `esp_http_client` assert path with no ICY. `esp_audio_render`, sold as the lightweight writer-callback option, depends on `gmf_audio ~1.0` and therefore drags `esp_audio_codec`, `esp_audio_effects`, `esp_asrc`, `esp_muxer` and `gmf_core`, including the muxer this project has no use for.

`espressif/esp_asrc`. P4 has no hardware ASRC (its SoC table says "Software only"; `asrc_reg.h` exists only for esp32h4 in IDF 5.5.5), it cannot change ratio while the handle is open, and 1.1.0 depends on `esp_audio_effects ~1.4` and warns of a Guru Meditation on chip version < 3.0. If you want Espressif rate conversion, call `esp_audio_effects` directly.

`espressif/gmf_fft` for the spectrum analyser. The v1.0 blog describes it as fixed-point Q15 while `audio_dsp` and `audio_pipe` are float end to end, and esp-dsp is already a dependency under a more permissive licence. (Note the argument that gmf_fft is peculiar to the 1.4.x branch is wrong: 1.3.0~1 declares the same dependency.)

`CONFIG_FATFS_SECTOR_4096` and disabling LFN. `FF_SS_SDCARD` is the literal `512` at `ffconf.h:240`, independent of `CONFIG_FATFS_SECTOR_*`. LFN's Kconfig makes only memory claims, and 255-char UTF-8 names are needed for real music files.

The full official LWIP/Wi-Fi profile. Already measured on this bench and it made things worse monotonically: `docs/CONCEPT.md:357-362` records 33% loss on the small Espressif profile and 75% on the large one, with 126 KB of internal RAM consumed and 15 KB DMA-capable remaining against a documented 6.7 KB failure floor.

`CONFIG_FATFS_ALLOC_PREFER_EXTRAM=n` and `EH_HOST_PORT_DMA_PREFER_SPIRAM=n`. Both were recommended on the reasoning that PSRAM is abundant. That is backwards here: internal DMA-capable RAM is the scarce resource, and `PREFER_SPIRAM=y` was set deliberately, taking free DMA RAM from 15 KB to 27 KB.

`esp_cache_get_alignment()`. It is declared only in `esp_private/esp_cache_private.h`. Use `heap_caps_malloc` with `MALLOC_CAP_CACHE_ALIGNED` (public, `esp_heap_caps.h:49`), which aligns internally and needs no queried constant.

Replacing `stb_image` for progressive JPEG. `stb_image.h:23` says it decodes baseline and progressive, and `cover.cpp:181` logs that path succeeding. `esp_new_jpeg` and `esp_jpeg` are baseline-only and would lose capability. libjpeg-turbo allocates the same full coefficient array that is the complaint (and its registry licence field reads "Custom", not BSD).

---

## 3. WHAT WE SHOULD KEEP

**`modules/radio`'s hand-written ICY client (1,246 lines).** Two independent sweeps of all 2,141 registry components found zero ICY, Icecast, SHOUTcast or station-directory clients. `esp_gmf_io_http.c` contains no `icy`, `metaint` or `StreamTitle` and sets only a `Range` header. ESP-ADF v2.8's `http_stream.c` returns zero matches for the same terms. Espressif's own maintainer conceded on issue #380 that "the intricate details" are "not implemented yet". Record this as an architectural decision, not debt. **But correct the stated reason.** IDF 5.5.5's `http_parser.c` has explicit ICY states (`s_res_or_resp_I`, `s_res_I`, `s_res_IC` at lines 283-287, driven at 748 and 781-805), so "ICY 200 OK" parses to status 200 and `esp_http_check_response()` accepts it. Writing down a false reason is worse than writing nothing.

**The Vorbis comment and RIFF INFO halves of `components/audio_tags`.** `esp_extractor`'s OGG extractor states "Comment header is skipped for not used by decoder", and its only metadata API is the ID3 parser. A registry-wide sweep for RIFF/LIST metadata returned nothing. Category (c), confirmed.

**`components/audio_dsp/pcm_ring`.** No lock-free SPSC ring exists in the registry; the only candidate names one and implements it over the FreeRTOS byte `ringbuf` with critical sections. Beyond locking, the semantics have no library equivalent: interleaved float frames, a generation counter that bumps on every reset, consumer-side pause (because stopping the decoder pauses nothing), a fade-then-drop handshake, and producer ownership via `claim()`.

**`drivers/i2s_std`.** `i2s_ll.h:45-52` gates `I2S_CLK_SRC_PLL_160M` on `HAL_CONFIG(CHIP_SUPPORT_MIN_REV) >= 300`, and this build sets 100. The mux offers XTAL 40 MHz, APLL and external, and `I2S_CLK_SRC_DEFAULT` silently resolves to XTAL, which caps 32-bit stereo at 48 kHz at 256x MCLK. With the PCM5102A's BCLK-locked PLL and a slave I2S1 for the QCC5125, this cannot be delegated to a framework.

**`components/cover`'s three-tier architecture.** Hardware JPEG engine, PPA hardware scale, software fallback is exactly what the platform offers. `SOC_JPEG_DECODE_SUPPORTED` and `SOC_PPA_SUPPORTED` are both 1; no Espressif decoder handles progressive at any revision.

**The M3U/PLS parsing in `components/playlist`.** `esp_playlist`'s own `docs/playlist_usage_scenarios.md:321` says it does not parse them and tells you to parse externally into name/url.

**The esp-dsp biquad chain over `esp_audio_effects` EQ.** esp-dsp is Apache-2.0 with real P4 assembly (`dsps_biquad_f32_arp4.S`, scalar `fmadd.s` inside `esp.lp.setup`, no PIE). `esp_audio_effects` 1.3.x is a closed prebuilt blob under an Espressif-silicon-only licence with no P4 assembly at all. Swapping would be a downgrade in licence, observability and probably speed.

**Direct use of `esp_lcd_st7701` 2.0.2~2 and `esp_lcd_touch_gt911` 1.2.1.** No BSP exists for the Guition JC4880P443C anywhere in the registry.

**The `host.init`/`host.deinit` no-op in `storage_module.cpp:99-112`.** esp_hosted's CHANGELOG 2.12.11 confirms "fixed SDIO deinit tearing down the shared SDMMC host (broke a co-existing SD card on the other slot)". The official `mcu_hosted_sdio_sdmmc_combined` example does the same thing.

**ESP-IDF v5.5.5.** Issues #16233 and #17889 report that the newer `sd_host` driver hardcodes `SDMMC_LL_HOST_CTLR_NUMS` to 1 and breaks SDMMC plus ESP-Hosted coexistence with "no available sd host controller". Both closed as internally Done with no public fix version. Verified locally: `esp_driver_sdmmc/src` contains only `sdmmc_host.c`, `sdmmc_internal.h`, `sdmmc_transaction.c`, and that symbol appears nowhere in the tree. The current pin is load-bearing.

**`components/audio_resampler`, but fix it rather than replace it.** This is the largest single finding in the audit and it is not an adoption question. Disassembling the project's own build artefact, `build/esp-idf/audio_resampler/.../resampler.c.obj`, shows `subsample_no_interpolate` calling `floor`, `__subdf3`, `__floatsidf`, `__muldf3`, `__adddf3`, `floor`, `__fixdfsi`, `__fixdfsi` around the 48-tap loop and `__extendsfdf2` on the way out. Whole-object relocation counts match to the unit: 31x `__adddf3`, 28x `__floatsidf`, 26x `__muldf3`, 20x `__subdf3`, 14x `__divdf3`. The CPU is built `-march=rv32imafc -mabi=ilp32f` (from `build/toolchain/cflags`), single precision only, so every `double` is a soft-float library call.

| Metric | Value |
|---|---|
| Configured taps / filter phases | 48 / 320, fixed ratio, stereo |
| MAC rate at 48 kHz stereo | 4.61 M MAC/s |
| Measured cost | 45% of one 360 MHz core |
| Implied cost per MAC | ~35 cycles |
| esp-dsp benchmarked float FIR on P4 (`dsps_fird_f32`, O2) | 5.19 cycles/MAC |
| Same kernel, ANSI RISC-V, O2 | 6.34 cycles/MAC |

Delete `double` from the per-sample path (`resampler.h:47`, `resampler.c:1018,1033`) and re-measure before concluding anything about 96 or 192 kHz. Budget ~5.2 cycles per tap-MAC on this chip however the kernel is written.

---

## 4. WHAT WOULD HAVE SAVED US TIME

| Pain point | The source that would have answered it |
|---|---|
| 1. `esp_http_client` assert on Icecast, and "ICY 200 OK" | The ICY half is answered by `C:/esp/v5.5.5/esp-idf/components/http_parser/http_parser.c` lines 283-287 (state declarations commented "for ICY URIs"), 781-782 (`case 'I': /* ICY URI */`) and 796-805. The assert half is answered by `components/esp_http_client/esp_http_client.c:315-318`: the guard is `client->state < HTTP_STATE_RES_ON_DATA_START && client->cache_data_in_fetch_hdr`, and `esp_http_client_fetch_headers()` sets that state at :1579, so the branch is dead after the first fetch. It fires on a **reused** handle whose previous response was partially drained, because `esp_http_client_read()` advances `raw_data` at :1338 but only frees the cache when `raw_len` reaches exactly zero. The fix is one call, `esp_http_client_clear_response_buffer()`, declared at `esp_http_client.h:712-724` with a doc comment describing this exact case. It is absent from the published API page, which is why reading the header beat reading the docs. `https://api.github.com/search/issues?q=repo:espressif/esp-idf+orig_raw_data+in:title,body` returns one hit, PR #18359, closed and unmerged |
| 2. I2S APLL reference count | `components/esp_hw_support/clk_ctrl_os.c`: `s_apll_ref_cnt` at :30, `assert(s_apll_ref_cnt > 0)` at :94 (the underflow that asserted), and the freeze at :118-135 with its own comment, "when more than one peripheral refers APLL, its frequency is not allowed to change once it is set". `i2s_std.c:329-333` shows the acquire is per channel handle with no full-duplex awareness, so a duplex pair always holds two. The root cause is one level below: `hal/esp32p4/include/hal/i2s_ll.h:45-52` gates `I2S_CLK_SRC_PLL_160M` on rev >= 300, leaving APLL as the only option. **And the escape was already in the tree**: `i2s_common.c:1485-1490`, inside the public `i2s_channel_tune_rate()`, does `periph_rtc_apll_release()` then `i2s_set_get_apll_freq()` then `periph_rtc_apll_acquire()`, dropping the count to 1 so the retune is permitted. One correction for the record: the diagnostic signature is not `ESP_ERR_INVALID_STATE`. That code is swallowed into an `ESP_LOGW` ("APLL is occupied already, it is working at N Hz") and the frozen frequency is returned as success. The `ESP_ERR_INVALID_ARG` comes from `i2s_std_calculate_clock`'s `sclk > mclk * min_mclk_div` check failing with the message "sample rate is too large" |
| 3. Resampler at 45% of a core | `riscv32-esp-elf-objdump -dr` on the project's own `resampler.c.obj`, plus `build/toolchain/cflags` showing `-mabi=ilp32f`. Both were on this machine before the code was profiled. For the target budget: `https://docs.espressif.com/projects/esp-dsp/en/latest/esp-dsp-benchmarks.html`, which has an ESP32P4 column (and proves `dsps_fir_f32` has no P4 assembly, since its P4 and ANSI RISC-V numbers are 1,606,563 and 1,606,552) |
| 4. Cover art stealing CPU and failing with out-of-memory | `components/esp_driver_jpeg/jpeg_decode.c:317-322` shows `jpeg_decoder_process()` blocking on `xQueueReceive` with a timeout, so the hardware path consumes no CPU. Lines 805-818 show progressive rejected cleanly with `ESP_ERR_NOT_SUPPORTED` and "Only baseline-DCT is supported.", not an OOM. The project's own `components/cover/src/stb_image.h:23` states "JPEG baseline & progressive", and `cover.cpp:181` logs "(software, progressive)". So both symptoms belong to the fallback, and the OOM is a largest-free-block problem stated in `cover_soft.c:32`. Route progressive art off the audio-critical path or reject it at ingest; do not chase a decoder that already works |

The esp_audio_codec gate is not in the list of four, but it is the cleanest example: `GET https://components.espressif.com/api/components/espressif/esp_audio_codec` returns `versions[0].docs.changelog`, and fetching that URL yields, verbatim, "Requires `ESP32-P4` chip version >= 3.0 for assembly optimization support" (v2.6.0) and "Added CMake check to require `ESP32-P4` chip version >= 3.0" (v2.6.2). One GET.

---

## 5. THE SOURCE-CONSULTATION PROCEDURE

Espressif's database is good, and it is machine-queryable, not just readable. Put this in `CLAUDE.md`.

**Step 0. Does the silicon do it at all.**
`C:/esp/v5.5.5/esp-idf/components/soc/esp32p4/include/soc/soc_caps.h`. Faster than any doc page. A 404 on a peripheral doc page is also a capability oracle: `.../v5.5.5/esp32p4/api-reference/peripherals/dac.html` is 404 while `.../esp32/.../dac.html` is 200, and `jpeg.html` is the reverse.

**Step 1. The right doc page, version- and target-correct.**
`idf.py docs --no-browser --starting-page api-reference/peripherals/i2s.html` builds the URL from the project's own version and target, HEAD-checks it and falls back on 404. Pattern: `https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/<page>`. Note the bare domain 302-redirects, so there is no browsable root.

**Step 2. The API index, for agents.**
`https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/objects.inv`. 448,462 bytes; skip 4 header lines, zlib-inflate. Contains exactly 27,877 entries mapping every documented symbol to its URL and anchor, scoped to this version and this chip. Cache it. Absence from the inventory means undocumented for this target, not nonexistent, so fall back to grepping the tree.

**Step 3. Local IDF source, for the exact failure mode.**
A return code and its guarding comment beat any prose, and it is the code actually being linked. This is what settled pain points 1, 2 and 4.

**Step 4. The registry, filtered.**
`https://components.espressif.com/api/components?q=<term>&target=esp32p4&idf=5.5.5&sort_by=total&sort_dir=desc`. Full OpenAPI 3.0.2 spec at `https://components.espressif.com/apidocs/apidocs/swagger.json`. Registry holds 2,141 components. Three traps:
- `targets: []` means "all targets", not a P4 claim. Every audio component returns empty.
- Nothing in the schema encodes a chip-revision requirement.
- Namespace lookalikes exist: `q=asrc` returns both `espressif/esp_asrc` and `majingjing123/esp_asrc`. Always pin namespace plus name.
- There is no `idf.py search` and no `compote search`. Discovery is the website or this API.

**Step 5. The changelog of the candidate AND every transitive dependency.**
`GET /api/components/<ns>/<name>` gives `versions[].docs.changelog`. Grep it for `P4`, `chip version`, `revision`, `Break`. Recurse into `dependencies`. This step is where the audit itself failed: one agent recommended `esp_asrc` as the resampler answer without reading its own changelog, which says "Requires `ESP32-P4` chip version >= 3.0", or its dependency `esp_audio_effects ~1.4`, which fails at build time. Write the 40-line script.

**Step 6. The ISA check, for any prebuilt archive.**
```
riscv32-esp-elf-readelf -A <component>/lib/esp32p4/*.a | grep Tag_RISCV_arch | sort -u
```
Any line containing `xespv2p2` means the archive carries instructions this silicon does not implement, whether or not the CMakeLists says so. Verified end to end: esp_audio_codec 2.6.2 shows both ISAs, the installed 2.5.0 shows only `xespv2p1`, esp_audio_effects 1.4.2 shows both, `esp_extractor` 1.0.5 and `esp_new_jpeg` 1.0.2 are clean. Binary is already installed at `C:/Espressif/tools/riscv32-esp-elf/esp-14.2.0_20260121/riscv32-esp-elf/bin/`. Treat a hit as "investigate", not "reject".

**Step 7. GitHub, for known bugs.**
`https://api.github.com/search/issues?q=repo:espressif/esp-idf+<exact assert string>+in:title,body`. Works unauthenticated, rate-limited to about 10 requests per minute. Use positive hits as evidence; do not treat a miss as proof, since issue search does not substring-match identifiers and misses Jira-mirrored IDFGH threads. Cite the issue number next to any workaround so it can be retired. `esp32.com` is unusable: it serves `<title>Bot Challenge</title>` to programmatic clients.

**Step 8. Errata, v1.3 column.**
`https://docs.espressif.com/projects/esp-chip-errata/en/latest/esp32p4/02-errata-summary/index.html`. Columns are v0.0 | v1.0 | v1.3 | v3.0 | v3.1 | v3.2.

| Applies to v1.3 | Does not (v3.0-only) |
|---|---|
| RMT-176, I2C-308, APM-560, ECDSA_DS-836, ECDSA_DS-837 | MSPI-749/750/751, ROM-764/770/816, Analog-765, DMA-767 |

Nothing touching I2S, DMA, cache, PSRAM, PPA or JPEG applies to v1.3. APM-560 (unauthorized AHB access may block subsequent PSRAM or flash transactions) is the one worth attention on a board with audio buffers in PSRAM.

**Step 9. The local build's own JSON.** `build/config/kconfig_menus.json`, `build/config/sdkconfig.json`, `build/project_description.json`, `build/compile_commands.json`. Query these instead of grepping the checkout.

**Two standing rules.** Read `COMPATIBILITY.md` and `SUPPORT_POLICY.md` from `master` on GitHub, not from the local checkout: the v5.5.5 copy has no ESP32-P4 section at all while master says "v1.0, v1.3, Supported since ESP-IDF v5.3", and the file itself warns that release branches may be stale. Conversely, read local source for behaviour. And before shipping anything from the esp-gmf or esp-adf-libs family, get a licence read: all of them report `license.name = "Custom"` and their LICENSE files read "Permission is hereby granted for use EXCLUSIVELY with Espressif Systems products", while `espressif/esp-dsp` is Apache-2.0.

---

## 6. OPEN QUESTIONS AND CONTRADICTIONS

### Claims the skeptic refused to confirm (unverified)

- **"esp_audio_codec 2.6.0 and 2.6.1 would build and then execute illegal instructions."** The build-and-link half is sound. "Illegal instructions" is an inference. Espressif's wording, "Updated PIE for saturation and rounding", is equally consistent with the same encodings producing different results, that is, silently wrong audio rather than a trap. Nobody has run it. Do not write this into a source comment as fact.
- **The rev-3 PIE errata causal story.** No Espressif source documents a PIE defect on pre-3.0 silicon. The P4 SoC errata list contains no such item, and the v3.x page describes "optimized PIE acceleration units" as a v3.x improvement. The defensible statement is that 1.4+ assembly targets v3.x PIE semantics.
- **The resampler cost model (20% three-stage, 27% single-stage, 180% for the current code).** Built on an invented 8-cycles-per-tap figure for `dsps_firmr_f32`, for which Espressif publishes no benchmark on any chip. The cross-check compounds three unmeasured scaling steps and lands at roughly half the primary estimate. The 180% figure additionally assumes the current code stays on the interpolation-free branch at 192 kHz, which it would not (required factor 640 exceeds `ASRC_FILTERS` 320). Measure before designing.
- **The duplex RX `sig_loopback` argument.** Marked PLAUSIBLE only. The mechanism is real (`i2s_std.c:142-146` calls `i2s_ll_share_bck_ws` when `controller->full_duplex`), but the documentation sentence cited in support sits inside a `SOC_I2S_HW_VERSION_1` block, and ESP32-P4 is `HW_VERSION_2`, whose adjacent text says the opposite: separate RX and TX channels able to work under different clocks. No TRM line was produced. The bench gate on that recommendation is mandatory, not optional.
- **`gmf_core` "~7 KB RAM for the core"** and **`esp_player` 1.0.6 requiring IDF >= 5.3**: neither confirmable from registry metadata.
- **`defensiblelogic/lowzag` 2.2.1 memory figures** (K = 64/25/5/1, 36 MB to 3.6 MB at 1/8). Vendor README only, 14 downloads, single vendor. The component exists; the numbers are unchecked.
- **`espressif/libjpeg-turbo` 3.1.1~2**: 0 downloads as an ESP port, registry licence field reads "Custom" (the "BSD-style" attribution was asserted, not extracted), and its progressive path allocates the same full coefficient array that is the original complaint.
- **`esphome/esp-audio-libs` 4.0.1**: 0 downloads, no targets, no dependencies, licence "Custom" in an otherwise Apache-2.0 namespace. Pattern suggests a superseded package.
- **"No ESP-IDF issue discusses ICY in esp_http_client."** A negative two-term GitHub search. The conclusion may hold on the API's merits; this evidence does not establish it.
- **P4 v3.x hardware differences** (pin 54 NC to VDD_HP_1, DCDC passives, "not recommended for new designs"). All true, but sourced from the ESP Hardware Design Guidelines, not the v3.x news page that was cited. Also, "v1.1" is not a documented revision.
- **`espressif/esp_codec_dev`, `espressif/esp_audio_render`, `espressif/gmf_io`, `espressif/gmf_fft`**: none were checked for a P4 revision gate or an `xespv2p2` archive. "Supports all targets" on a registry summary page is not evidence for rev v1.3.
- **The coprocessor save-area sizes** (FPU 132, HWLP 24, PIE 216 bytes). The mechanism, the 16-byte PIE alignment and the panic string are confirmed; the three numbers are approximate.
- **`CONFIG_FATFS_USE_FASTSEEK`**: proposed, not measured. It allocates a cluster link map per open read-mode file and silently falls back for write mode.

### Where two agents disagreed

**A. What actually causes the SDIO packet loss.** This is the sharpest disagreement and it is unresolved. The storage agent concluded that the SDMMC controller-wide mutex (`sdmmc_transaction.c:54,105,189`, verified real and not per-slot) serialises the card on slot 0 against the Wi-Fi link on slot 1, explaining 33-75% loss while streaming and 0% idle. Its own skeptic refuted the attribution: `docs/CONCEPT.md:357-362` measures **internet radio**, with the microSD untouched, and "idle" there means radio off. The link fails at roughly 0.05 Mbit/s against a published 30 Mbit/s with zero slot alternation. Worse, the 33% and 75% figures **are** the small and large Espressif tuning profiles, so the recommended remedy is the thing that already made it worse. The closest-fitting external evidence is esp-hosted-mcu issue #184, open, P4 plus C6, inbound TCP stalling after roughly 100 KB, "particularly affecting the streaming RX optimization mode configuration", and this project sets `CONFIG_ESP_HOSTED_HOST_SDIO_RX_STREAMING_MODE=y` at `boards/p4_player/sdkconfig.board:55`. Issue #167 is also open on exactly ESP32-P4 v1.3 silicon with C6, with four independent reporters, and reducing the clock to 20 MHz did not help. **Next step is a measurement, not a knob**: run `eh_host_raw_tp_start(ESP_TEST_RAW_TP__BIDIRECTIONAL)` with radio off and card unmounted, which sits below TCP and Wi-Fi and which `tools/sdio_soak` cannot reach. Then repeat with the card mounted to test the mutex hypothesis separately.

**B. The resampler replacement.** Three agents proposed three different answers. The sources agent recommended `esp_asrc` 1.0.1 plus `esp_audio_effects <1.4`. The resample agent recommended esp-dsp's `dsps_resampler_mr_*`. The esp-dsp agent recommended neither and said to fix the vendored code. The esp-dsp agent is best supported: `dsps_resampler_mr_init` binds to `dsps_firmr_f32`, which `dsps_fir.h:347-358` defines as `dsps_firmr_f32_ansi` in all four dispatch branches with no `arp4` variant anywhere, so it is plain C at `-Os`; both esp-dsp wrappers reject `samplerate_factor < 1` and therefore cannot downsample at all (96k, 88.2k, 176.4k or 192k sources to a 48 kHz output); `dsps_resampler_mr_init` takes an `int16_t length`, capping the coefficient bank at 32,767 floats; and `dsps_fird_f32` is the subject of open esp-dsp issue #117 on this exact chip revision. `esp_asrc` is ruled out separately (section 2). Resolution: fix the `double` first, then benchmark `esp_audio_effects` 1.3.0~1 `rate_cvt` end to end including conversions.

**C. Is esp-dsp's P4 assembly safe on this silicon?** The registry-sweep agent stated the project is "already on the fast path" with `CONFIG_DSP_OPTIMIZED=y` and no caveat. The esp-dsp agent found three open defect reports against exactly that code path: issue #119, `dsps_fft2r_fc32` intermittently faulting at `dsps_fft2r_fc32_arp4.S:52` on a P4 rev **v1.3**, roughly one boot in four, cured by `CONFIG_DSP_ANSI=y`; issue #117, `dsps_fird_f32` silently corrupting 48 kHz audio on a rev 1.3 P4 after minutes; issue #102, misaligned `esp.lp.setup` causing PMP faults (rev 1.0). `spectrum.cpp:110` calls `dsps_fft2r_fc32` on the audio path. **Unresolved, and it should be the first thing measured**: build once with `CONFIG_DSP_ANSI=y` and soak against the current build across at least a dozen cold boots with the analyser running. Cost of the safe setting is about 1.6% of one core (not the 0.3% first estimated, because esp-dsp builds at `-Os` here). Cost of being wrong is an intermittent Guru Meditation that will read as an audio bug for days. Note that recommendation 6 in section 2, `dsps_biquad_sf32`, is gated on the same `dsps_biquad_f32_arp4_enabled` macro and inherits whatever this soak finds.

**D. Are IDF's HWLOOP erratum workarounds compiled into this firmware?** The esp-dsp agent said `CONFIG_ESP32P4_REV_MIN_FULL=100` compiles them out, since `portasm.S:216` guards on `ESP32P4_REV_MIN_FULL <= 1`. Refuted by disassembling this project's own `portasm.S.obj`: the guard tests the **bare** symbol, which nothing in ESP-IDF ever defines (only the `CONFIG_`-prefixed form exists), so it evaluates to 0, the condition is always true, and the workaround is present. Do not change `REV_MIN_FULL`; it would achieve nothing. The genuine gap is the one esp-idf issue #19025 actually reports: the coprocessor **save** path at `portasm.S:546-547` has no workaround at all, and no sdkconfig setting closes it.

**E. Is `components/audio_tags` category (c)?** The GMF agent said Espressif provides no tag reader and told the team not to look. The codec agent found `espressif/esp_extractor` with an open-source ID3 parser. Both are partly right: ID3v2 has a replacement, Vorbis comment and RIFF INFO do not. The blanket "do not go looking" would have frozen a wrong conclusion into the project docs.

**F. Is ESP-ADF stale?** The GMF agent argued v2.8 dates from March 2024, "two and a half years stale", and cited three open issues as evidence of neglect. All refuted: v2.8 was published 2026-03-17, v2.7 was 2024-09-14, and all three cited issues are closed as completed (#380 in February 2021 after two Espressif replies, #1335 in August 2025, #1523 six days after opening). The recommendation against ADF survives on other grounds (its `audio_pipeline` is being replaced by GMF, and it has no ICY), but the staleness argument must be withdrawn. An audit that cites closed tickets as open evidence damages its own credibility.

**G. Do the four documented esp_hosted SDIO Kconfig symbols exist?** One agent said no, based on a grep. Refuted: `CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ` and friends exist as back-compat aliases in `managed_components/espressif__esp_hosted/host/sdkconfig.rename`. The conclusion survives by a different and sharper mechanism: `C:/esp/v5.5.5/esp-idf/tools/cmake/kconfig.cmake:42` globs only `${component_dir}/sdkconfig.rename`, the file sits in `host/`, and nothing registers it, so the aliases are inert. That is an upstream packaging bug in esp_hosted 3.0.7, fixable by moving one file, and worth reporting.

**H. Smaller refutations worth recording so they are not re-derived.** `esp_audio_codec` does contain resampler code (SILK, 60 string hits in the P4 archive); the operative claim, that it exposes no public resampler API, holds. `esp_audio_effects` 1.3.0~1 depends on `gmf_fft ~1.0` exactly as 1.4.2 does, so the argument that gmf_fft is peculiar to the branch being avoided is void. Radix-4 FFT does link on P4: `dsps_bit_rev4r_fc32_ae32` is defined, because the aliasing macro at `dsps_fft4r.h:172` renames the ANSI definition along with the call sites (confirmed with `nm` on this project's built `libespressif__esp-dsp.a`); only the int16 alias is genuinely undefined. `C:/esp/v5.5.5/esp-idf/version.txt` does not exist; the version comes from `components/esp_common/include/esp_idf_version.h`. `espressif/jsmn` is not byte-identical to the vendored copy. And one blog quotation used in the GMF report, "All 14 official components have been promoted to v1.0.x status with stable APIs", is not in the source; the post says "Promoted all components to 1.0.x" with no count. The number was invented inside quotation marks.

### Two upstream issues worth filing

1. **ESP-IDF, v5.5.x backport request**, citing the master commits that already fix both: `i2s_channel_reconfig_std_clock` releases the old APLL reference at `i2s_std.c:393-397` before `i2s_std_set_clock` at :398 can fail, and never restores it (master acquires the new source first and releases the old only after success); and `i2s_channel_tune_rate` returns bare on the `mclk_div.integer < 2` path without `xSemaphoreGive` (master uses `goto err`). GitHub search for `s_apll_ref_cnt` and `reconfig_std_clock` both return `total_count: 0`, so nobody has asked. Ask separately that `i2s_channel_init_std_mode` skip the APLL acquisition when `controller->full_duplex` is already set; master has not addressed that. Note the mutex-leak half does not reproduce on an APLL channel (the assert fires first), so use an XTAL-sourced simplex channel for that reproducer.
2. **esp-adf-libs**, asking for the pattern Espressif already ships elsewhere: `espressif/esp_image_effects` 1.2.0 carries both `lib/esp32p4` and `lib/esp32p4_less_v3` and selects between them on `CONFIG_ESP32P4_SELECTS_REV_LESS_V3`, which this project already sets. Both `esp_audio_codec` and `esp_audio_effects` already ship a no-assembly variant for esp32s31 with an opt-out Kconfig symbol. Doing the same for P4 would unblock the entire 2.6+/1.4+ line on pre-3.0 silicon. That is worth more than any local pin.