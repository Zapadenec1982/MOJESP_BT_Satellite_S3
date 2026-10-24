# 2026-10-15 — the 96 kHz ceiling: the first attempt broke 192 kHz streams, the second did not

The customer's decision of 15.10.2026: the output clock stops at 96 kHz, and a 176.4 or 192 kHz
track plays at 88.2 or 96 kHz, halved by its producer (`AudioModule::PLAY_MAX_HZ`). Why: what
192 kHz adds over 96 lies above 48 kHz, where the TPA3255's output filter and the speakers stop,
and the DSP at 192 kHz cost 39.5 % of a core with the loudness on — over the 40/80 rule with
Бас/Високі +6 (`../2026-10-15-dsp-one-pass/`).

## Method

`../2026-10-15-dsp-one-pass/bench.py`, as there: a mono 1 kHz tone pushed over DLNA in a new
listening session, volume 4 %, loudness 60 %, the port lift on (62 Hz +4 dB) — the panel as the
customer uses it; 60 s runs and one of 3 minutes; settings put back afterwards. `underruns` is
the audio task's count of output blocks the PCM ring could not fill.

## Result

| image | source | Бас/Високі | DSP | heaviest block (median / p90 / max) | underruns |
|---|---|---|---|---|---|
| 4dfb414 (first) | 192 kHz | 0 | 19.5 % | 33.1 / 39.3 / 45.3 % | **7503** in 60 s |
| 4dfb414 (first) | 192 kHz | +6 | 20.9 % | 35.5 / 41.7 / 46.7 % | **7313** in 60 s |
| 4dfb414 (first) | 192 kHz, 3 min | 0 | 20.1 % | 36.1 / 43.2 / 50.5 % | **28354** |
| 53e8b13 (second) | 192 kHz | 0 | 19.6 % | 37.3 / 42.7 / 45.9 % | 0 |
| 53e8b13 (second) | 192 kHz | +6 | 21.0 % | 38.5 / 43.6 / 47.1 % | 0 |
| 53e8b13 (second) | 192 kHz, 3 min | 0 | 19.8 % | 38.0 / 42.7 / 48.5 % | 0 |
| 53e8b13 (second) | 176.4 kHz | 0 | 19.4 % | 38.8 / 42.2 / 46.2 % | 0 |

Before the ceiling (61af2af, the same settings, 192 kHz played as 192): 39.5 / 79.0 % with
Бас/Високі 0, 42.4 / 81.7 % with +6.

## Why the first attempt broke

The DSP did what was expected, but 30–50 % of the output blocks came out short, steadily, for as
long as the stream played: the producer (the radio task on core 0) did not keep up. It converts
through `RateConverter` with `resampleFixedRatioInit()`, and `resampleProcessInterleaved()` kept
every fixed-ratio converter on its double phase: on the P4, which has no double hardware, several
soft-float calls for each of 192 thousand input frames and 96 thousand output frames a second.
The Q32 phase the ASRC path already used was switched off for fixed ratios. The first attempt was
reverted (d1d94cc) nine minutes after it was flashed.

The second attempt puts an exact fixed ratio — a step of 2 or 4 either way — on the Q32 phase
(daf2168): every position is then the same number on both paths, and a host test holds the
interleaved converter (Q32) to the planar one (double) bit for bit. 44.1 ↔ 48 keeps the double
path. The 48-tap filter itself is unchanged; by the cycle model it is the converter's cost now,
about 18–20 % of core 0 at 192 → 96, and the stream kept up through 3 minutes.

## Files

| file | SHA-256 |
|---|---|
| `windows-4dfb414-first.csv` | `aa4929194b5b85dbb00f5dbc78b4593b900b99bb0b73197593cb2763136eea43` |
| `settings-4dfb414-first.json` | `041f9b5d62ba6be147129a97680884303ba04611b243457b92f1f3610de9fce8` |
| `windows-53e8b13-second.csv` | `a2a0d52d5c37a58d10ffc0ba505666b4888b648bf801bcf2e5859b8df6851ed2` |
| `settings-53e8b13-second.json` | `28fb0271682a3609767d012cae35eee8fd5792b71ce384ec043731511e87a579` |
