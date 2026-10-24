# Queued codec reopen correction - 2026-09-19

## Reproduction and diagnosis

The customer confirmed that BOTH SBC -> aptX HD and aptX HD -> SBC can go silent
and recover only after disconnect/reconnect. The first captured failing sequence
is in BT_CODEC_SWITCH_BENCH_2026-09-19.md. SBC/aptX containers themselves work once
the stream is established; do not bypass the protocol-1.3 readiness barrier.

The pinned upstream code has two moments at which CONFIG sees stream state:
1. `bta_av_proc_stream_evt` sees AVDT CONFIG arrival and queues it. It only sets
   `force_incoming` if the SCB is already INIT and the signalling link exists.
2. `bta_av_ssm_execute` later dispatches queued BTA events. A CLOSE queued before
   CONFIG can have moved the SCB from OPEN to INIT in the meantime.

In that ordering the INIT table legitimately handles CONFIG and enters INCOMING,
but no flag was set at arrival. `bta_av_config_ind` then omits
BTA_AV_INCOMING_CFG_EVT. BTC processed the preceding CLOSE and is IDLE, so it
ignores the later CONFIG/OPEN/START, matching the captured failure.

Patch 0002 sets the existing `force_incoming` flag when the dispatcher actually
handles STR_CONFIG_IND from INIT. The existing config action and BTC IDLE handler
then restore peer identity and move BTC to OPENING in FIFO order, without calling
BTA_AvOpen or restarting Bluetooth. The earlier notification path remains valid.
No new public event, UART message, codec-specific workaround or timeout fallback.

The production delta is one assignment plus its explanation in bta_av_ssm.c.
OPEN/CLOSING configuration rejection and arbitrary OPEN/START in IDLE are unchanged.
The patch covers the SCB-swap path too, since it tests state before that swap.
This proves a code-level ordering defect matching the observed events; a fresh
phone test is still needed to establish that this is the only runtime cause.

## Verification and official sources

- ESP32 soc_caps.h confirms Classic BT; local IDF HEAD equals idf.pin:
  b774170ff46c393eeb5e495ea37936038d3f4f4f (v5.5.5).
- Read the target/version-specific official A2DP API documentation:
  https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32/api-reference/bluetooth/esp_a2dp.html
- Inspected pinned BTA tables/dispatcher, AVDT callback, config action and BTC
  IDLE/OPENING handlers, plus btc_transfer_context call sites. Official sources:
  https://github.com/espressif/esp-idf/blob/v5.5.5/components/bt/host/bluedroid/bta/av/bta_av_ssm.c
  https://github.com/espressif/esp-idf/blob/v5.5.5/components/bt/host/bluedroid/bta/av/bta_av_aact.c
  https://github.com/espressif/esp-idf/blob/v5.5.5/components/bt/host/bluedroid/btc/profile/std/a2dp/btc_av.c
- Recreated components/bt with setup.ps1 -Clean after checking the resolved local
  deletion target. Both numbered patches applied successfully; overlay copied.
- Shared IDF components/bt has no tracked diff after setup/build preparation.
- Host replay extracts actual BTA state/event enums, all state tables, dispatcher,
  config notification and BTC incoming-config case from the selected source tree.
  Hardware, codec validation, and unrelated action handlers are stubs.
- Same final test on stock IDF: 78 checks, 35 failures (expected regression).
  On patched local component: 78 checks, 0 failures. Includes both codec identities,
  repeated switches, early-notification path, ordinary opening, SCB swap, rejection
  while OPEN/CLOSING and no acceptance of bare OPEN/START in IDLE.
- Full satellite host suite: 2981 link/format checks plus 78 BTA checks passed.
  Optional UBSan runtime is unavailable. The standard host script now runs both.
- P4 application code and the shared wire protocol are unchanged in this correction.
  Firmware build/deployment identities and remaining bench result are recorded in
  CODEX_HANDOFF_2026-09-19.md when available.

## Required phone test after deployment

Play audio; switch SBC <-> aptX HD repeatedly without manual disconnect, waiting
15-20 seconds per codec. Confirm application codec notification, format readiness,
nonzero P4 input and audible playback in both directions. Test explicit reconnect
as a separate control. Preserve logs if IDLE still ignores configuration/open/start.
The earlier 124617 us P4 transition interval, duplicate RX adoption and application
queue underrun accounting remain separate follow-up items, not fixed by this patch.
