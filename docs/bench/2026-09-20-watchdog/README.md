# 2026-09-20 — task watchdog reset during pushed (DLNA) MP3 playback

Evidence copied on 2026-09-21 from `D:\P4_DSP\dlna-next\build-host-dlna\crash-20260920\`
(an untracked build directory). The investigation itself is
`docs/DLNA_WATCHDOG_2026-09-20.md`; the scheduler rework it calls for is plan stage 6
(`docs/PLAN_HARDENING_2026-09-21.md`).

## Panic summary

At uptime 53.009 s the task watchdog fired: `main (CPU 0)` had not fed it inside the
5 s window (`HW watchdog: 5000 ms timeout` in `uart.log`). CPU 0 was running `radio`,
CPU 1 was `IDLE1`. A pushed track (48 kHz stereo MP3, 320 kbit/s) had started at
48.099 s. The panel rebooted; the next boot reported `Boot reason: task_wdt (6)`.

From the coredump (`decoded.txt`, `scheduler.txt`):

- `Panic reason: Task watchdog got triggered … - main (CPU 0)`.
- Task current on CPU 0 at the dump: `radio`, in `vPortYield` ← `sleep_ms` ←
  `radio_task` (`modules/radio/src/radio_module.cpp:1234`).
- `main` (TCB `0x4ff30ed8`) sits in the priority-1 ready list — not blocked on a lock or
  an RPC — with its saved frame at the loop delay, `main/main.cpp:409` (`vTaskDelayUntil`).
  Its last wake tick is 4629; `radio`'s is 5128: 499 ticks at 100 Hz, the whole watchdog
  window without `main` getting the core.
- Stacks are healthy (`main` 496 used / 8072 free, `radio` 448 / 5548). This is CPU
  starvation of the priority-1 main loop by the priority-4/5 producer tasks on core 0,
  not a hang and not an overflow.

The same night the producers were dropped to `ESP_TASK_MAIN_PRIO` (86cf394) and a
corrected image was flashed (see the journal); a pushed-track reproduction under the
corrected image is still pending.

## Files

| file | bytes | SHA-256 | what |
|---|---|---|---|
| `uart.log` | 23709 | `0cb456699935c2be18155ae7e5a1d520429053251e185b61f22aae2e4e22ac85` | passive COM12 capture: the watchdog abort, the reboot, the boot that follows |
| `coredump.bin` | 65536 | `203eb06d57765e3fa100532e900aa7fd62f59422253b0c29da3491088b2e08c6` | raw `coredump` partition (`0x1c000`, 64 KiB) read with esptool |
| `decoded.txt` | 41000 | `f43baef1e2f0c618eaa15ac0ea62fb66a29102eb4fe2ed564f296d1c7120a390` | `espcoredump.py v1.17.0` output against `player.elf` |
| `scheduler.txt` | 12868 | `817ce6785f905347e4bb3cd5ff38f3e8296c5b5896376eb8c983040773c65968` | gdb walk of the ready/delayed task lists |
| `core.elf` | 29012 | `217291d89b82c56ae0a784f9a5a52451fe1ad439c56cae06c5c4bb4c4a5e218d` | **not committed** — the ELF core espcoredump wrote; lives next to the originals |
| `player.elf` | 27723956 | `b4cf008ddbbf8a5ce775e0a52f931ea9b52e6289c023d6207f72453cac4ac674` | **not committed** — the image that wrote the dump |

## The ELFs, and why they matter

From `CLAUDE.md`: a coredump is only readable against the ELF of the image that wrote
it — `esp-coredump` refuses on an app SHA mismatch, and a mismatch is exactly what
several reflashes produce. Keep a copy of the ELF whenever a crash is being chased.
ELF binaries are not committed (27 MB each); they currently live here:

- `player.elf`, SHA-256 `b4cf008d…4ac674` — the deployed Bluetooth/artwork build made in
  `player-cover-bench` (the backtrace's source paths are `D:/P4_DSP/player-cover-bench/…`).
  Two identical copies: `D:\P4_DSP\dlna-next\build-host-dlna\crash-20260920\player.elf`
  and `D:\P4_DSP\modesp-player\build\watchdog-deploy-20260920\previous.elf`.
  `player-cover-bench\build\` itself no longer holds the ELF.
- `core.elf`, SHA-256 `217291d8…5e218d` — only in
  `D:\P4_DSP\dlna-next\build-host-dlna\crash-20260920\`.
- The corrected image flashed afterwards (HEAD 34fe433 + working changes): ELF
  `c45271a05a0f5360cb5abaadf3b7ecf1be08f58e76e33073ba466e27d285bbcf`, BIN
  `51300563e75ca1165d7e7c97ac10b7c0ac27ab53fea0a27b4b9875fa2d4d9e3e`, at
  `D:\P4_DSP\modesp-player\build\watchdog-deploy-20260920\player.{elf,bin}`.

Both directories are build outputs outside git. If either tree is cleaned before the
ELFs are copied somewhere durable, `coredump.bin` becomes unreadable.
