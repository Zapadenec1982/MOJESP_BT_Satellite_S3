# 2026-10-15 — DSP load before and after the filter chain's speed-ups

The customer's stage of 15.10.2026 ("один прохід замість кількох", docs/PLAN_SOUND_UX_2026-10-14.md,
S3): the DSP must keep the rule of docs/DSP_V2_DESIGN_2026-09-30.md §5 — on average ≤ 40 % of a
core, the heaviest block ≤ 80 % of its own time — at 192 kHz with the loudness on, and the sound
must not change. Before: main 4b8d2b2 (the panel's image from 09:33). After: main 61af2af = 4b8d2b2
+ the five `perf(dsp)` commits of feat/dsp-one-pass (flashed 10:25:30, app only), whose output is
bit-exact with 4b8d2b2's (tests/host/test_dsp_passes.cpp).

## Method

`bench.py` (here, Python 3, no packages): a mono 1 kHz tone at −20 dBFS as a 16-bit WAV, served
from the PC and pushed to the panel's DLNA renderer, which needs no credentials. For each rate:
DLNA Stop, `audio.source` 0, then the push — a new listening session every time, because the
output clock is chosen by a session's first track and then stays (`AudioModule::adopt_clock`;
the first attempt pushed 48, 96 and 192 kHz into one session and the panel played all three at
48 kHz). Then volume 4 %, each step's settings through `POST /api/settings` (the web login was
off that day), 4 s for fades and the loudness glide, and 60 s of WebSocket snapshots every
0.5 s. `audio.dsp_pct` is the DSP's cycles over a half-second window as a share of one core;
`audio.dsp_max_pct` is the window's worst block as a share of a block's own time (240 frames:
1.25 ms at 192 kHz). Both are published without notify, so each reading is a fresh connection's
snapshot; a window read twice is counted once. Afterwards everything was put back: volume 20,
Бас/Високі 0, the EQ and the stages as found, DLNA stopped, `audio.source` 3 (Bluetooth).

The tables give the mean of `dsp_pct` over the windows and the median, the 90th percentile and
the maximum of `dsp_max_pct` — "heaviest block" in the plan has been the median of the windows'
worst blocks. `windows-<image>.csv` holds every snapshot; `settings-<image>.json` the panel's
audio state when each run started; `to_csv.py` made both from bench.py's JSONL logs.

Panel settings (as found): EQ on, «Мій звук», all ten bands; loudness 60 % (+16.8 dB of bass
shelf at volume 4 %); subsonic 40 Hz; mono bass 80 Hz; bass limiter 250 Hz; drive 5 dB; width
100 %; balance 0. `audio.spk_type` was 2 (closed box — the customer trying the setting), so the
port lift (62 Hz +4 dB) was off and the subsonic two sections; at 10:33 it went back to 0 at
the customer's word, and the last table is the panel as the customer uses it. Bluetooth
discoverable, nothing connected; the glass in standby.

## Result

Same settings before and after (`spk_type` 2: port lift off).

| rate | Бас/Високі | DSP before | after | heaviest block before (median / p90 / max) | after |
|---|---|---|---|---|---|
| 48 kHz | 0 | 12.2 % | 9.9 % | 28.7 / 31.3 / 34.4 % | 18.7 / 22.7 / 24.5 % |
| 48 kHz | +6 | 13.1 % | 10.6 % | 28.7 / 32.9 / 37.3 % | 19.2 / 22.0 / 25.6 % |
| 96 kHz | 0 | 23.0 % | 18.2 % | 55.4 / 60.7 / 63.1 % | 33.7 / 37.3 / 42.5 % |
| 96 kHz | +6 | 24.8 % | 19.8 % | 56.1 / 60.4 / 64.0 % | 37.3 / 43.0 / 46.9 % |
| 192 kHz | 0 | 44.2 % | 37.8 % | 104.7 / 110.1 / 119.4 % | 75.2 / 81.2 / 87.0 % |
| 192 kHz | +6 | 47.8 % | 40.9 % | 110.7 / 117.3 / 125.8 % | 78.2 / 84.2 / 88.1 % |

No underruns in any run after. Before, the first 96 kHz run stalled for 14 s (the stream, not
the DSP: 5248 underruns with the DSP idle) and was taken again (`base-4b8d2b2-96k-redo`).

192 kHz as the customer uses it (port lift on, 61af2af):

| Бас/Високі | DSP | heaviest block (median / p90 / max) |
|---|---|---|
| 0 | 39.5 % | 79.0 / 83.6 / 89.7 % |
| +6 | 42.4 % | 81.7 / 86.1 / 96.5 % |

What each stage costs at 192 kHz, from switching it off for a run (Бас/Високі 0):

| stage | before (4b8d2b2) | after (61af2af) |
|---|---|---|
| the EQ's ten sections | 16.8 % = 31.5 cycles a section a frame | 15.0 % = 28.1 |
| the bass limiter | 11.6 % = 218 cycles a frame | 10.1 % = 190 |
| mono bass (the M/S stage) | 1.9 % = 36 cycles a frame | 1.7 % = 31 |
| the heaviest block without the EQ / the bass limiter | 75.9 / 73.7 % (from 105.0) | 61.2 / 56.0 % (from 79.0) |

## Reading it

- The average fell by 19–21 % at 48 and 96 kHz and by 14.5 % at 192 kHz: 6.4–6.9 points of a
  core there.
- The heaviest block fell much more: about 30 points at 192 kHz (104.7 → 75.2 %). Before, it
  sat at about the average plus 270–300 thousand cycles (~0.8 ms) at every rate, but switching a
  stage off took it down by more than the stage's own share. The reading: blocks run slower than
  the average at times, and a longer block is hit harder. The bass limiter weighs most there
  (−23 points of the heaviest block for −10 of the average, after).
- The bass limiter still costs 190 cycles a frame; its two loops are about 100 instructions a
  frame with nothing spilled. Its delay line (7.9 KB at 192 kHz) lives in PSRAM: one new cache
  line every 8 frames to read and one to write back — presumably waiting on PSRAM behind the
  display and the decoder. The experiment below moves it.

## Experiment: the bass limiter's delay line in SPM

exp/bass-line-spm (3b48664) = 61af2af + the 7920-byte delay line in the P4's 8 KB SPM instead of
PSRAM. SPM is the internal heap's region of last resort; with the line there the 160 bytes left
are refused by TLSF cleanly, and `system.heap_internal` read 81 thousand instead of 88–89. The
main-checkout owner flashed it for a measurement only (10:44:13) and put main back at 10:53:49 —
0c05ab7, whose DSP is 61af2af's (plus the glass's Settings rows). 192 kHz, port lift on:

| image | Бас/Високі | DSP | heaviest block (median / p90 / max) | underruns |
|---|---|---|---|---|
| exp 3b48664 | 0 | 35.7 % | 63.1 / 68.4 / 70.7 % | 120 (first 20 s) |
| exp 3b48664 | +6 | 38.7 % | 66.8 / 71.2 / 74.1 % | 0 |
| exp 3b48664, no bass limiter | 0 | 27.8 % | 49.6 / 54.1 / 57.2 % | 0 |
| exp 3b48664, 3 min | 0 | 37.2 % | 70.9 / 75.7 / 81.3 % | **8111** |
| main 0c05ab7, 3 min (control) | 0 | 37.4 % | 69.2 / 79.9 / 86.4 % | 0 |

The DSP got cheaper, but in the 3-minute run the PCM ring ran dry in two waves (27–40 s and
57–65 s into it; `main_loop_max_ms` up to 56, 93 overruns in a window) while the same run on main
right after had none (`main_loop_max_ms` ≤ 26). One run each is no proof, and not something to
recommend either. The control also shows how much the same DSP code moves between sessions:
37.4 % and 69.2 % here against 39.5 % and 79.0 % at 10:34 — so part of the first comparison is
that, not SPM. Not merged; a longer A/B (15 minutes each) would be a stage of its own.

## Model

Instruction counts are from the build's own compiler (riscv32-esp-elf esp-14.2.0_20260121, the
firmware's flags), cycles from an in-order, single-issue model with a 4-cycle FPU latency and a
1-cycle taken-branch penalty, calibrated on the 25.09 bench of the biquad kernels (5545 and 5068
cycles for 240 samples; the model gives 23.0 and 20.0 a sample against 23.1 and 21.1). It puts
one SVF section at 28 cycles a frame; the panel measured 29–34.

| loop | before | after |
|---|---|---|
| one SVF section (svf_stereo) | 26 instructions, 28 cycles a frame | — |
| two sections (svf_stereo2) | 2 × 26, 56 | 46, 48 |
| brickwall | 52 instructions, 5 data-dependent branches, ~55 | 34, ~35 |
| bass limiter (detector + applied) | 119 with ~12 spills, ~122 | 48 + 51, ~101 |
| mono bass (M/S + side SVF) | 24, 41 (one chain end to end) | 23, 29 (skewed by a frame) |
| steady volume | 5 a sample, ~16 a frame (waits) | 14 per two frames, ~8 |

## Files

| file | SHA-256 |
|---|---|
| `windows-4b8d2b2.csv` | `ba7b3317a3c0fd133754aa213240ce3265634aca122ffc3c27f42155285eea1f` |
| `settings-4b8d2b2.json` | `857dda0eeea9b26cd2316b19fcc4f5c3d4e702b615896ae99f83b2af54ecca1b` |
| `windows-61af2af.csv` | `3bfc5fb6ba0ac6f8bae95eb647c8044ec893a0794af514cdbe86c5590c327146` |
| `settings-61af2af.json` | `467c895260e04aef813b35dba34e2ecad491366192c25ea8da89acaf34db6947` |
| `windows-exp-bass-spm-3b48664.csv` | `56074aeed77fcb298f84e81bfa508d7fbfb7952f0745edf076bd13449757a098` |
| `settings-exp-bass-spm-3b48664.json` | `24989c208e9641fb4deb5f4cc49258de96f5dd2b5a28423200afa3fe3853b361` |
| `windows-0c05ab7.csv` | `a73fdbff2bc7f4e1d601133aebdfd877ed7db1c76eaea953501560ad508fb647` |
| `settings-0c05ab7.json` | `00764d320d9e21afaedb03a63974a838115ed36bcd1642b24560778a83c2a6cc` |
| `bench.py` | `b95a1b9214cf95778767ecde2af3a4c45ab3336c99fe12e29ce742a5d86c98f1` |
| `to_csv.py` | `a235eb05f261318603ae170fa28744618590c0b4f38ccaa6651504b3ea4a48d7` |
