# DLNA playback watchdog reset, 2026-09-20

## Observed failure

The user reported a panel restart during pushed MP3 playback. Passive COM12 capture
recorded the task watchdog firing at uptime 53.009 seconds, naming `main (CPU 0)`.
CPU 0 was running `radio`; CPU 1 was idle. The stream had started at 48.099 seconds:
48 kHz stereo, 320 kbit/s, Content-Length 9,759,631, estimated duration 243 seconds.
Boot subsequently reported `task_wdt (6)`.

The exact deployed ELF SHA256 is
`b4cf008ddbbf8a5ce775e0a52f931ea9b52e6289c023d6207f72453cac4ac674`.
The matching ELF was saved from `D:/P4_DSP/player-cover-bench/build/` before decoding.
This is the deployed Bluetooth/artwork build, not the unfinished DLNA worktree build.

Evidence is retained in `D:/P4_DSP/dlna-next/build-host-dlna/crash-20260920/`:
`uart.log`, `player.elf`, `coredump.bin`, `core.elf`, `decoded.txt`, `scheduler.txt`.
These ignored build artifacts are local evidence, not committed fixtures.

## Core findings

- Main TCB `0x4ff30ed8` is in the priority-1 ready list, not blocked on a lock or RPC.
  Its last wake tick is 4629. Radio's recorded wake tick is 5128: a gap of 499 ticks
  at 100 Hz, consistent with the five-second watchdog window.
- Main's saved frame is at the loop delay/yield. Used/free stack is 496/8072 bytes;
  radio is 448/5548. This is not evidence of stack exhaustion.
- Decoder and reader run on core 0 at priorities 5 and 4, above main at priority 1.
- `sleep_ms(5)` already clamps to one real tick. Deployed disassembly confirms
  `vTaskDelay(1)`. The cause must not be reported as rounding the delay to zero.
- CPU starvation is confirmed. The dump is a snapshot, not a scheduling trace;
  alternating ready producer tasks are a plausible mechanism, not a traced fact.

## Candidate correction and validation

In `D:/P4_DSP/dlna-next/modules/radio/src/radio_module.cpp`, both producers now use
`ESP_TASK_MAIN_PRIO`. A compile-time assertion requires FreeRTOS time slicing.
Equal-priority ready tasks share ticks even while a decode call is executing;
one producer no longer outranks main while the other sleeps. The existing bounded
decode batches and blocking waits remain. Audio output priority/core are unchanged.

The radio translation unit compiled successfully with the project's ESP32-P4 IDF
5.5.5 flags and generated headers. `git diff --check` passed. This is not a full
firmware link or hardware validation. Playback throughput/underruns and main-loop
responsiveness need checking on the eventual integrated firmware. Do not flash the
older isolated baseline over newer Bluetooth/artwork/resampler work.

## Hardware actions and outstanding work

UART was opened with DTR/RTS disabled before opening. Reading the saved flash dump
at `0x1c000`, length `0x10000`, through esptool caused one announced service reset.
No firmware, NVS, or filesystem writes were performed. COM13 was not used.

The correction has not been flashed. No soak test was started; the user's hold
still applies. Renderer EOF/automatic-next, control point and remaining DLNA work
remain tracked in `DLNA_JOURNAL_2026-09-20.md`.

## Watchdog correction deployed with user authorization (2026-09-20)

- User explicitly authorized flashing. Copied the isolated radio scheduling correction into the main checkout and built its current source state at HEAD 34fe433 plus working changes. Verified Bluetooth/artwork/UI/DLNA sources and sdkconfig match the last deployed player-cover-bench versions. Preserved the pending resampler float-return optimization; the unfinished dlna-next queue/control-point work was NOT included.
- Full ESP32-P4 IDF 5.5.5 build passed, image size 0x201890 with 33% app-partition headroom. Existing resampler calloc-argument-order warning remains unrelated to this correction.
- Host suite including native C++ runner: 169 passed. Fixed one compile error in the pending resampler test: this doctest requires separate CAPTURE(rate) and CAPTURE(ratio). Initial sandbox subprocess failures required an elevated build/test run; no failed run is counted as validation.
- Flashed only the app at 0x30000 on COM12, esptool hash verification passed. NVS, LittleFS, bootloader, partition table and COM13 were untouched. Normal post-flash reset occurred.
- Deployed BIN SHA256: 51300563e75ca1165d7e7c97ac10b7c0ac27ab53fea0a27b4b9875fa2d4d9e3e.
- Deployed ELF SHA256: c45271a05a0f5360cb5abaadf3b7ecf1be08f58e76e33073ba466e27d285bbcf.
- Exact new/previous BIN and ELF, source hashes, working patches, build/test/flash logs and passive UART capture are in D:/P4_DSP/modesp-player/build/watchdog-deploy-20260920/.
- Passive UART after flashing shows running main-loop Bluetooth link/pong reports, local CRC/COBS/oversize zero, and no observed watchdog/panic. Boot output is partial because attachment followed the flash. This is startup observation, NOT a pushed-track reproduction or a proof of watchdog resolution under playback.
- Soak remains on hold. Repeat the formerly crashing DLNA track and check progress, responsiveness, underruns and watchdog before declaring the incident resolved. No commit or push was made during deployment.
