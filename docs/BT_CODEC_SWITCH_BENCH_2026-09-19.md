# Bluetooth codec-switch bench capture - 2026-09-19

## Setup and outcome

Customer-authorized 180-second passive capture of P4 COM12 and satellite COM13,
started at local 21:05:29. DTR/RTS were false before opening both ports. No reset,
firmware change, flash, backup, source command or volume command was issued by
Codex. The customer operated the phone. Both serial ports closed at capture end.

Deployed implementation pair: player ce3e3db1a456ac1bdc5388585b425d5bf38a4673;
satellite 843566204ea376dcd3543708632b67bcd02ad706. Exact deployed ELF hashes are
in CODEX_HANDOFF_2026-09-19.md.

**Result: partial pass, reproducible codec-switch failure.** SBC 44.1 kHz / 16-bit
playback and aptX HD 48 kHz / 32-bit playback after reconnect were observed.
Changing to aptX HD during the test caused silence; the customer explicitly
reported it. Full phone reconnect restored sound, also confirmed by the customer.
Do not mark live codec switching complete.

## Event sequence

Times below are each device's own uptime in milliseconds, not a shared clock.

| Device/time | Evidence |
|---|---|
| Satellite 2537555 | Application receives SBC 44100 Hz configuration. |
| Satellite 2537905 | format ready: 44100 Hz / 16-bit, generation 3. |
| P4 2574449 | Nonzero SBC input/output; short 0, dropped 0, ring 49%. |
| Satellite 2553915 | aptX HD decoder configuration coincides with A2DP disconnected; IDLE handler ignores BTC_AV_CONFIG_EVT. |
| Satellite 2553975 / 2554835 | IDLE handler ignores BTA_AV_OPEN_EVT / BTA_AV_START_EVT. |
| Satellite 2568265 | SBC readiness succeeds again, generation 4. |
| Satellite 2574585 | Second aptX HD attempt: A2DP disconnected, IDLE ignores BTC_AV_CONFIG_EVT. |
| Satellite 2574635 / 2575525 | IDLE again ignores OPEN / START. |
| P4 thereafter | Announced rate 0, silence; UART liveness continues. |
| Satellite 2651225 | After phone reconnect, application receives aptX HD 48000 Hz configuration. |
| Satellite 2651555 | format ready: 48000 Hz / 32-bit, generation 5. |
| Satellite 2656815 | Stream starts. |
| P4 2689449 onward | Nonzero input/output, 48000 Hz / 32-bit path; short 0, dropped 0, ring about 49%. |

Full logs also contain OPENING handler warnings for STOP/CLOSE/SUSPEND. The
aptX decoder's configure message alone does not prove the application received
ESP_A2D_AUDIO_CFG_EVT. In the failing attempts it did not log an aptX format
transaction, whereas reconnect did. The first place to investigate is the
Bluedroid BTC/BTA event/state sequence during codec reconfiguration. This is not
yet an established root cause or a basis to accept CONFIG/OPEN blindly in IDLE.

## Other measurements and limits

- P4 CRC 3 and COBS 268 stayed unchanged across sampled link reports. Satellite
  bad/gaps/unknown/overrun and TX drops stayed zero. UART loss was not observed
  in this window; this is not a long-duration SDIO/radio coexistence test.
- Satellite PCM dropped count stayed 6912 in complete current reports; it is
  cumulative since boot, not a new loss of 6912 bytes during this capture. Initial
  COM12 bytes include an old truncated buffered log fragment; exclude that from
  current baselines.
- Satellite application-ring underrun counter increased 271 -> 529. Its code
  increments on an empty application queue at a throttled cadence, not a measured
  TX DMA underrun. Investigate it separately; do not label steady audio loss or
  declare it harmless solely from this counter.
- P4 recorded an output-write interval of 124617 us on reconnect. As already
  reviewed, this includes explicit I2S reconfiguration and is not proof of an
  audible dropout. P4 also shows RX 32 -> 16 -> 32 around that reconnect; review
  transaction clearing/replay alongside the timestamped sequence.
- No panic/assert was observed. User confirmation of recovery does not establish
  artifact-free switching, because the codec-change attempt itself went silent.

## Next implementation review

Compare the repository's generated components/bt/host/bluedroid/btc/profile/std/
a2dp/btc_av.c and corresponding BTA transitions against pinned official IDF and
the tracked patch series. Follow docs/BASE.md; fixes belong in reproducible local
patches, never in shared IDF. Model the failing close/config/open/start sequence
and test event ownership before adding recovery behavior. Preserve the 1.3
format barrier: do not release PCM on timeout to conceal the upstream failure.

Raw logs remain local (may contain track metadata and device addresses):
D:/P4_DSP/bench_logs/bt-codec-switch-20260919-210529/COM12.log
D:/P4_DSP/bench_logs/bt-codec-switch-20260919-210529/COM13.log

COM12 log SHA256: 45d87c2629d4ce297dc94f5363af96946c64cc7fb3369e99a899cef8ac7f9efe

COM13 log SHA256: b3383fcce17dbbbb9b627fc71caa03faf2e2be6252429d324dcb545b77293c03
