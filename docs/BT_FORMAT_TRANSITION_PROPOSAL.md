# Bluetooth format transition - approved implementation

Status: approved by the customer and implemented in both source trees as protocol 1.3.
Software verification is recorded in the handoff; firmware has NOT been flashed.
Date: 2026-09-19. Scope: Bluetooth only; AUX, pinout, amplifier control, ASRC
algorithm and buffer sizing are unchanged.

## Problem

Protocol 1.2 provides a fixed 50 ms interval, but no evidence that the receiver
has finished reconfiguration. Moving P4 publication before the telemetry gate
removes a known 500 ms delay; it cannot make an arbitrarily delayed host ready.
Satellite audio_out_flush also races with the producer, a ring item already held
by the writer, and the carry buffer. An enqueued UART message is not a message
already transmitted. Native per-decoder widths would exercise these paths more.

## Negotiated optional extension (1.3)

Keep all existing message layouts and IDs. Negotiate a new host capability bit 4,
FORMAT_READY, only with a peer whose HELLO minor is at least 3. Both protocol
copies, version constants and firmware sides must change in one coordinated set
of repository commits. There is no atomic commit across two Git repositories;
record the matching pair of SHAs in the handoff.

- 0x16 STREAM_PREPARE, satellite -> player: u32 boot_nonce, u32 generation,
  u32 rate_hz, u8 bits, u8 channels (14 bytes, little endian).
- 0x23 STREAM_READY, player -> satellite: u32 boot_nonce, u32 generation,
  u8 status (9 bytes). Status 0 = prepared successfully; 1 = refused.
- generation starts at 1 and changes on every transition, including a reconnect
  with an identical format. boot_nonce distinguishes satellite restarts.
- Duplicate PREPARE for the same ID and tuple is idempotent. The same ID with a
  different tuple is rejected. Old acknowledgements cannot release a new stream.
- Rate must be supported by the actual port; bits must be 16 or 32 and channels
  must be 2. The receiver reports refusal instead of guessing or reading mono as
  stereo. No newly invented sample-rate or GPIO capabilities.

## Transition sequence

1. Satellite closes the PCM producer gate BEFORE accepting new decoder-format
   samples. The Bluetooth callback never waits on a task, a mutex, or UART.
2. The writer acknowledges that it owns no old-format ring item. Drain queued
   PCM and clear its carry buffer. Stop/reconfigure I2S under the writer's own
   control and clear ALL DMA descriptors while the channel is READY. Never
   re-enable a channel after failed allocation/reconfiguration.
3. Announce PREPARE before changing the wire format. Keep old-format or new-format
   I2S strictly silent throughout the transition. Start the minimum 50 ms silence
   interval from confirmed UART transmission, not from queue insertion.
4. P4 publishes the entire request immediately as one coherent record. Fade the
   old buffered audio, pause RX and wait for its in-flight read/publication to
   finish. Discard old DMA/ring data and reset ASRC history for this stream.
5. P4 applies the existing clock-source/reconfiguration ordering, checks every
   result, and starts the receiver. Only THEN send READY success for that ID.
   Failed preparation stays silent and sends refusal. When Bluetooth is not the
   selected source, defer preparation/READY without changing the active output.
6. Satellite releases PCM only after BOTH matching success and the minimum
   transmitted-silence interval. A timeout never counts as success. Retry the
   same request at a bounded rate (250 ms); no growing queue of retries.

Selecting BT later triggers preparation of the saved latest request. Leaving BT
parks/releases its input clock as today. A missing host does not interrupt an
already established standalone stream, but an ACK-negotiated transition cannot
silently fall back to the unsafe timer-only path. Disconnect invalidates the
transaction. Reboot/reconnect scenarios need explicit test coverage on both ends.

## Implemented ownership rules

- Satellite BT control task owns current decoder format and transaction state.
  UART HELLO/READY handlers enqueue notifications; they do not retune I2S or edit
  the format concurrently with the BT task. Queue overflow must leave PCM gated.
- Writer owns ring items and carry state. Producer gating must also account for a
  callback already in flight; volatile alone is not synchronization.
- P4 audio task owns clock adoption and ACK success. UART publication is separate
  from slow SharedState telemetry. Receiver pause must not block normal playback
  for a 200 ms read timeout; use an explicit pause/completion handshake.
- No logging, allocation or waiting inside an ISR or a per-sample conversion loop.

## Native widths behind the barrier

For an ACK-capable wide host: SBC/AAC decoder output -> 16-bit slots; the current
aptX and aptX HD decoder's 24 significant bits -> 32-bit slots. WIDE means
capability, not an instruction to widen 16-bit PCM. Do not confuse slot width
with meaningful sample precision. Keep narrowing with dither for a narrow host.
Legacy hosts retain their existing width-selection policy until separately
validated; do not rely on unknown firmware completing a new transition on time.

## Required verification before deployment

Host tests on BOTH implementations: golden wire vectors; malformed sizes/status;
repeated and stale messages; same-format reconnect with a new ID; refusal;
missing/lost ACK; delayed host beyond 500 ms; inactive BT source; timeout wrap;
16/32 width selection with/without capabilities; no premature producer release.

Build player for esp32p4 and satellite for esp32 using pinned local IDF 5.5.5.
Verify both protocol copies and wire vectors agree. Do not change shared IDF.

Bench, separately authorized: switching 44.1/48 kHz and 16/32 bits; phone and host
restarts; link interruption; source switching while satellite waits; codec
changes under load. Check DMA/underrun counters and the I2S data line, not only
listening. No claim of click-free operation before these checks.

## Official source checks already made

Local IDF: components/soc/esp32{,p4}/include/soc/soc_caps.h;
components/esp_driver_i2s/i2s_common.c (enable/disable/preload/read/write) and
i2s_std.c (READY-state reconfiguration, clock/slot ordering, descriptor allocation).
i2s_channel_disable waits for the current transfer and resets its cursor;
i2s_channel_preload_data fills descriptors in READY state. Neither justifies
flushing application-owned buffers concurrently with their producer/consumer.
Versioned docs: https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32/api-reference/peripherals/i2s.html
and https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/api-reference/peripherals/i2s.html

## Implementation and review limits

The customer explicitly approved this coordinated implementation after reviewing
this proposal. The earlier rejected edit was not applied; it is superseded by
this reviewed pair of implementations and matching protocol documents.

The BT control task performs channel reconfiguration under producer/writer
mutexes; the writer returns its ring item before releasing the channel mutex.
An atomic gate token plus decoder-event epoch prevents stale control work from
releasing PCM for a newer queued codec event. P4 uses a nonspinning atomic request
snapshot and an explicit RX pause flag, with a 10 ms read timeout.

Satellite driver failure keeps the gate closed and requires reboot: IDF can free
old descriptors before allocation of replacements fails. Do not re-enable such
a channel. Refusal is terminal for the current transaction; a new negotiation
may retry host refusal, but cannot bypass a satellite driver fault.

Host tests cover framing, pure barrier decisions and mailbox concurrency. They
do not execute FreeRTOS task scheduling, physical UART/I2S or the audible fade.
Follow the bench checks above before claiming click-free transitions.
