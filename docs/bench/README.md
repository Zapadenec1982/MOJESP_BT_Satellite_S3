# Bench evidence

Captures, coredumps and working patches that a decision or a journal entry rests on, kept
in git so they survive a cleaned build directory. One folder per `<date>-<topic>`, each
with a README naming the source, the hashes and the context. ELF binaries are not
committed — their SHA-256 and current location are.

| folder | what |
|---|---|
| `2026-09-16-sdio-soak/` | 8.5 h HTTPS soak through the C6 (`tools/sdio_soak`); `docs/SOAK_TEST.md` |
| `2026-09-20-bt-cover/` | the two working patches and the image hashes of the Bluetooth cover-art bench; `docs/BT_COVER_2026-09-20.md` |
| `2026-09-20-watchdog/` | task-watchdog coredump during pushed MP3 playback; `docs/DLNA_WATCHDOG_2026-09-20.md` |
| `2026-10-05-cover-stack/` | stack the software cover decoder needs (stb_image under QEMU, and `-fstack-usage`), per caller; why it has its own task (`SOFT_STACK` in `components/cover/src/cover.cpp`) |
| `2026-10-15-dsp-one-pass/` | DSP load at 48/96/192 kHz before and after the filter chain's speed-ups (main 4b8d2b2 → 61af2af), what each stage costs, the bass limiter's delay line in SPM tried and not kept; `docs/PLAN_SOUND_UX_2026-10-14.md` S3 |
| `2026-10-15-rate-ceiling-96/` | 176.4/192 kHz played at 88.2/96: the first attempt starved the stream (the resampler on soft doubles), the second plays it with 0 underruns at 19–21 % DSP; `docs/PLAN_SOUND_UX_2026-10-14.md` S3 |

Captures are stored byte-exact (no line-ending conversion: `docs/bench/** -text` in
`.gitattributes`), so the SHA-256 in each README is that of the file as captured.
