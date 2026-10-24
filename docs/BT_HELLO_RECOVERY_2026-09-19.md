# Satellite startup HELLO recovery - 2026-09-19

## Observed failure

After deployment of the queued BTA reopen fix, the customer reported no sound.
Passive capture D:/P4_DSP/bench_logs/bt-no-audio-20260919-223602/ showed:
- ESP32 decoded aptX HD at 44100 Hz into 16-bit slots, with nonzero PCM peaks.
- Satellite hello_sent remained 1; no round-trip report (printed only when a host
  HELLO has been accepted). PING/PONG continued without unanswered pings.
- P4 announced rate remained 0, input -120 dBFS and ring 0%.
- A phone disconnect/reconnect was captured: satellite generation 2 again resumed
  44100/16 in legacy mode, while P4 still had no announced format.

Source review explains the mismatch: the satellite sent HELLO once at startup.
If it or its reply was lost/rate-limited, PONGs did not retry capabilities. P4
could retain modern_format_ from an earlier protocol-1.3 stream and reject legacy
STREAM_FMT from the restarted satellite. Exact loss/suppression of the previous
boot's greeting was not captured; do not claim a physical UART cause is proved.
The earlier idle-link-only deployment check missed this failure. PING/PONG alone
is insufficient validation; require host HELLO/caps and matching format readiness.

## Correction

Satellite commit 3b4443f retries HELLO on the existing one-second timer until a
valid compatible or incompatible host greeting arrives. Reply/startup/retry use
one rate limiter with its timestamp reserved under a short portMUX critical
section. Encoding, logging and queueing happen outside that section. Host flags
are atomic because the RX and timer tasks access them. Existing wire layouts,
legacy compatibility and the protocol-1.3 readiness barrier are unchanged.
P4 application and shared IDF were not modified. BTA patch 0002 remains applied.

## Verification

- Host test compiles actual send_hello/answer_hello/handle_hello/ping_tick plus real
  message encoders/decoders. UART, clock, logging and locks are stubbed: it is not
  a scheduling test. Covers dropped greeting/reply despite successful PONGs,
  delayed/malformed host reply, legacy host, incompatible host and rate limiting.
- Old source: 41 checks / 7 failures. Fixed source: 53 checks / 0 failures.
  Count differs because retries execute extra encoder assertions.
- Full suite: 2981 link/format + 78 BTA + 53 HELLO checks passed (3112 total).
  Optional UBSan runtime unavailable. IDF v5.5.5 build passed, app 0x10ae40 bytes.
- Espressif procedure: ESP32 soc_caps.h, pinned esp_timer.h, official timer example,
  Xtensa portmacro.h and target/version-correct timer/FreeRTOS documentation.
  https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32/api-reference/system/esp_timer.html
  https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32/api-reference/system/freertos_idf.html
- COM13 app-only flash at 0x10000 verified by esptool. No backups or data writes.
  Held in bootloader, opened both console ports with DTR/RTS false, then used
  esptool HardReset only on ESP32 to capture its startup. P4 was not reset/flashed.
- ELF SHA256 c39a5574b382a9e3df769ecc5f71767f192e7018db9d04d1d23bb2de23f93375.
  Boot confirmed hash prefix; app version 3cabec3-dirty because build preceded commit.
- Capture D:/P4_DSP/bench_logs/bt-hello-recovery-20260919-224249/:
  first HELLO at 605 ms did not establish capabilities; retry at 1605 ms received
  host 1.3/caps 0x19 at 1615 ms. Endpoint initialization then admitted 44.1/48 kHz.
  Thus the actual bench exercised the new retry, not only the normal success path.

Playback and codec-switch results are recorded in CODEX_HANDOFF_2026-09-19.md.
Do not infer audible recovery solely from HELLO or from successful host tests.
