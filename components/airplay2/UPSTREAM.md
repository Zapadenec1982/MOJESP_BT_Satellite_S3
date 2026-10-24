# airplay2 — where this code comes from

The AirPlay 2 receiver in this component is **not ours**. It is the core of
[rbouteiller/airplay-esp32](https://github.com/rbouteiller/airplay-esp32), taken at

| | |
|---|---|
| commit | `811d5f8af750096683dcec7c95d1cbc08c2f7601` (v0.2.1, "Merge pull request #163 … release-v0.2.1") |
| fetched | 2026-09-23 |
| licence | Non-Commercial License, Copyright (c) 2026 Remi Bouteiller — [`LICENSE`](LICENSE), verbatim |

## The licence, and what it means here

The licence allows use, copying, modification and distribution **for non-commercial purposes
only**, and requires its notice in every copy. This firmware runs on one unit, for its
owner's personal use (customer decision, 23.09.2026 — `docs/PLAN_HARDENING_2026-09-21.md`,
stage 17). That is what makes it usable at all.

- Keep `LICENSE` in this directory and the upstream files as they are, notices included.
- Do not relicense any of it, and do not present it as this project's code.
- **Anyone turning this firmware into a product** must either remove this component (and
  `modules/airplay`, which is the only thing that uses it) or obtain a commercial licence
  from the copyright holder first. The rest of the firmware does not depend on it: without
  the module, `audio.source = 6` is simply not offered.

## Layout

| | |
|---|---|
| `upstream/` | Upstream files, **byte-identical** to the commit above, in upstream's own directory layout (`main/` dropped). `upstream.sha256` lists every one of them with its hash, and `tools/tests/test_airplay2_upstream.py` fails if any differs or is missing from the list. |
| `port/` | Ours: what binds the core to this player instead of to upstream's boards. |

A change to upstream behaviour goes in `port/`. Where that is impossible and an upstream
file has to change, it moves out of `upstream/` into `port/` whole, and the change is listed
below — so `upstream/` always means exactly "as published".

## What is taken

The receiver itself: RTSP server and handlers, HAP pairing (SRP-6a, Ed25519, via
libsodium), FairPlay setup, plist/bplist, PTP and NTP clocks, the realtime (ALAC over UDP)
and buffered (AAC over TCP) receivers, the timing engine, the decoder glue (over
`esp_audio_codec`), the DACP client, the ALAC magic cookie.

## What is not taken, and what replaces it

| upstream | why not | here instead |
|---|---|---|
| `main.c`, `network/wifi.c`, `ethernet.c`, `dns_server.c`, `web_server.c`, `ota.c`, `log_stream.c` | the player has its own network stack, web UI and OTA | `modesp_net`, `modules/airplay` |
| `audio/audio_output.c` (+ `_spdif`, `_usb`) | they own an I2S port; the player's audio core owns the only one | `port/` — writes into `pcm_ring` |
| `audio/audio_resample.c`, `components/audio-resampler` | the output clock follows the source here, and where it cannot, the player's own converter already does this | — |
| `settings.c` | upstream keeps its settings in NVS; the player keeps them in SharedState | `port/settings.h` |
| `playback_control.c`, `buttons.c`, `led.c`, `display`, `boards`, `dac*`, `a2dp_sink.c`, `bt_coex.c`, `audio/eq_events.c` | board features of upstream's hardware; Bluetooth is the satellite's job here | — |
| `network/mdns_airplay.c` | calls `mdns_init()` and sets the hostname, both of which `modesp_net` already owns | `port/mdns_airplay.c` — adds the services only |
| `settings.h`, `components/dac/include/dac.h` | the headers of files not taken, still included by taken ones | `port/settings.h` (the four calls the core makes), `port/dac.h` (declared, never called here) |

## Material inside the upstream files that upstream did not write

- `rtsp/rtsp_fairplay.c` — the FairPlay setup replies are fixed byte tables. Upstream does
  not say where they come from; the same tables appear in other open-source AirPlay
  receivers. They are data answering Apple's handshake, and are carried as upstream ships
  them.
- Comments in several files describe behaviour "like shairport-sync"; the file headers claim
  no code from it.

## Dependencies

As upstream's `dependencies.lock` at that commit, except where the player already pins its
own:

| | upstream lock | here |
|---|---|---|
| `espressif/libsodium` | 1.0.21 | 1.0.21 (source-built, no prebuilt archive to check for `xespv2p2`) |
| `espressif/esp_audio_codec` | 2.5.0 | `>=2.0.0,<2.6.0`, as `modules/player` and `modules/radio` — 2.6 needs chip revision 3.0 and this board is v1.3 |
| `espressif/mdns` | 1.11.1 | 1.12.0, already in the build |

## Local changes

No file under `upstream/` is edited. What differs from upstream's behaviour:

1. **`esp_read_mac` is redirected, for upstream files only** (`port/redirect.h`,
   force-included by `CMakeLists.txt`). Upstream derives the device id from the Wi-Fi
   station MAC in `rtsp_get_device_id()`, the AirPlay 1 Apple-Challenge response and the
   pair-verify accessory info. The ESP32-P4 has no Wi-Fi of its own (`SOC_WIFI_SUPPORTED`
   is not defined for it), so `esp_read_mac(…, ESP_MAC_WIFI_STA)` returns
   `ESP_ERR_NOT_SUPPORTED` and leaves the buffer untouched — the id would be uninitialised
   stack, and pair-verify signs it. Here that one type answers with the MAC the module passes
   to `airplay2_start()` (the base MAC, `modesp::device_mac()`); every other type goes to
   the real function.
2. **`network/mdns_airplay.c` is replaced** by `port/mdns_airplay.c`, derived from it: the
   same TXT records, without `mdns_init()` / `mdns_hostname_set()`, callable again after the
   network stack restarts mDNS, and with one MAC for `deviceid` and the RAOP name.
3. **The settings the core reads come from the module**, not from NVS: the name from
   `airplay2_start()`, the volume from what the phone last set (it becomes `audio.volume`,
   which the player persists). Upstream's NVS namespace `airplay` is still used, by `hap.c`
   alone, for the pairing keys.
4. **The audio output is the player's ring** (`port/`, plan item 17.3), not an I2S port.
5. **Cover art is taken, not dropped.** Upstream's `handle_set_parameter` sees an image body
   and at most flags `has_artwork`; the bytes are gone after that, and nothing in between
   is a hook. `rtsp_dispatch()` — the one function that sees every request whole, called
   from `rtsp_server.c` — is wrapped at link time (`-Wl,--wrap=rtsp_dispatch`,
   `port/artwork.c`): an `image/*` body is handed to the module on the way past, and every
   request then goes to upstream unchanged. Whether senders are asked for covers (`md` in
   the RAOP record) is `airplay2_config_t::artwork`, decided at start; upstream's
   `CONFIG_ENABLE_AIRPLAY_ARTWORK` stays off, since all it adds is that flag.

   `md` is what an AirPlay 1 sender reads. An AirPlay 2 sender is asked by the features
   instead — bit 15 artwork, 16 progress, 17 text, as shairport-sync sets them — and
   upstream advertises none of the three, so on the AirPlay 2 profile an iPhone sent the
   text and the progress but never a cover (measured 13.10.2026, `airplay_trace`). The
   AirPlay 2 features now carry 16 and 17, and 15 when covers are asked for
   (`port/mode_policy.h`, `airplay_mode_features()`); the classic profile's stay upstream's.

6. **Opt-in negotiation trace** (`port/diagnostics.c`): the dispatch wrapper logs a
   bounded request line and allowlisted headers. Linker wrappers record response
   status, body length, encryption state and send result, including `rtsp_send_ok`
   (its internal call to `rtsp_send_response` bypasses linker wrapping). No bodies,
   authentication headers or key values are logged. Enable `airplay_trace` at INFO
   through `/api/loglevel`, plus `rtsp_server` for connection boundaries. Defaults
   stay at WARN. This diagnoses Windows Apple Music's early disconnect without
   changing replies or the upstream files.

7. **Runtime protocol mode** (`port/mode_policy.h`): persisted `airplay.mode` selects
   AirPlay 2 (0, default) or classic RAOP (1, port 5000, AirTunes/105.1). The module
   removes mDNS, runs `airplay2_set_legacy_mode()` on an internal-stack worker, and
   advertises only after the listener is ready. Changing mode disconnects AirPlay
   sessions without restarting P4. HAP identity, allocated audio resources and the
   shared output worker stay initialized. UI exposes switching, active profile,
   status and errors; rapid changes are serialized and the latest selection wins.
   Failure attempts rollback, and leaves a clear unavailable state if rollback fails.

   Four adapted copies replace upstream sources: `rtsp_handlers.c` changes /info
   features/version/model, `rtsp_message.c` changes Server headers, `bplist_builder.c`
   changes /info model/source version, and `rtsp_server.c` changes the listener port
   and lifecycle. Checked stop joins both client tasks and the server, retaining stop
   flags on timeout. Start waits for bind/listen success; sockets close in their owner
   task, cleanup is serialized, and connection access is guarded. Profile changes
   happen only after the old tasks finish. All originals and MAC redirection remain.

   `modesp_net::MdnsGuard` serializes Wi-Fi init/free with AirPlay registration/removal
   across ESP's event task and the module loop. No mDNS lock is held while joining
   RTSP tasks. Tests cover profile responses, lifecycle failures/rollback and queued
   module selections (`tools/tests/test_airplay*.py`).

8. **ANNOUNCE is held to the buffers' shape** (`port/rtsp_handlers.c`, `parse_sdp`).
   Upstream takes the channel count, frame length and bit depth of an SDP as sent, but
   the decode buffer and every PCM ring slot are sized for `AUDIO_MAX_CHANNELS` 16-bit
   channels and `MAX_SAMPLES_PER_FRAME` frames (`audio/audio_buffer.h`); three or more
   channels overran both. An rtpmap outside 1..2 channels, or an ALAC fmtp outside
   1..2 channels, 1..4096 frames or 16 bits, is answered 400 and changes nothing.
   Standard RAOP SDPs (stereo, 16-bit, 352) are unaffected
   (`tools/tests/test_airplay_sdp.py`).

9. **The binary-plist parser is bounds-checked** (`port/bplist_parser.c`, an adapted
   copy that replaces `plist/bplist_parser.c` in the build). Upstream trusts the
   sender's trailer, object indices, offsets and counts: SETUP, TEARDOWN and the other
   bplist handlers, all reachable before pairing, could be made to read far outside
   the request buffer or to spin. The copy validates the trailer (object count, top
   object, offset table between header and trailer), refuses an index past the table
   or an offset that is not an object, checks every offset before it is dereferenced,
   does its range arithmetic in 64 bits with lengths and counts bounded by the plist's
   size, bounds the work of the recursive search, and refuses more than eight streams.
   Well-formed plists parse exactly as upstream's (`tools/tests/test_airplay_bplist.py`,
   under ASan/UBSan and in a 32-bit build where the host has them).

10. **RTSP receive buffers have room for the terminator** (`port/rtsp_server.c`).
    Each message is NUL-terminated at `buffer[total_len]` while it is dispatched; a
    message exactly as long as the buffer (4096 B, or 256 KB grown) wrote that byte
    past the allocation. Buffers are now allocated one byte over their capacity
    (`tools/tests/test_airplay_rtsp_buffer.py`).

11. **The decoder is not swapped under a running stream.** `audio_receiver_set_format()`
    frees and recreates the one decoder that the receive task decodes with, unlocked.
    `port/rtsp_handlers.c` answers an ANNOUNCE, or an AirPlay 1 SETUP (a Transport
    header, no streams), after SETUP (until TEARDOWN) with 455, before anything is
    changed (`tools/tests/test_airplay_sdp.py`, `tools/tests/test_airplay_setup.py`).
    `port/rtsp_server.c`: a replaced session stops the receiver in its own cleanup,
    which the server does not wait for, so a new client holds its requests (at most
    3 s) until the replaced slot has stopped the receiver, decoder and NTP clock.
    Still open: an AirPlay 2 stream SETUP on a live connection, realtime or buffered.
    `handle_setup` calls `audio_receiver_set_format()` in its streams loop before
    anything stops the stream: `audio_receiver_start()` stops and restarts a realtime
    stream only after the decoder has been freed, and `audio_receiver_start_buffered()`
    returns early while the buffered task runs.

12. **DACP's mDNS queries take `modesp::MdnsGuard`.** `dacp_client.c` queries
    `_dacp._tcp` from the RTSP client task (the AirPlay 1 grace period) and its
    `dacp_wk` worker, unguarded, while Wi-Fi may `mdns_free()` under the guard and
    delete the semaphore the query waits on. `mdns_query_ptr` is wrapped at link time
    by `modules/airplay` (`-Wl,--wrap=mdns_query_ptr`, `src/mdns_query_guard.cpp`),
    which already depends on `modesp_net`; this component gains no dependency.

13. **A message too big for the buffer is skipped, not fatal** (`port/rtsp_server.c`).
    The receive loops keep 1024 B free for the next read (one encrypted block), so
    nothing over `RTSP_MESSAGE_MAX` (256 KB less that) can complete; upstream then
    closed the connection, and the AirPlay session with it — for one cover a sender
    sent too big, once covers are asked for (entry 5). Now its bytes are dropped as
    they arrive, the buffer does not grow for it, and the sender gets its 200 with that
    CSeq once the last of them is in; the request reaches no handler, and the next
    message is framed as before (`tools/tests/test_airplay_rtsp_buffer.py`). The covers
    an iPhone sent on 2026-10-13 were 19–83 KB.

14. **A buffered packet with SSRC 0 is not decoded** (`port/buffered_audio.c`). Bytes 8–11
    of each AirPlay 2 buffered packet name its encoding; shairport-sync decodes only an
    SSRC it knows and never 0, while upstream decodes every packet with the decoder SETUP
    made. `audio_crypto_decrypt_buffered()`, the one call that sees the whole packet, is
    wrapped at link time: SSRC 0 is refused before decryption (upstream counts and drops it
    like a packet that did not decrypt), and an unknown SSRC or a switch between known
    formats is decoded as before and said at WARN once per run. `audio_decoder_decode()` is
    wrapped too, so a frame the decoder refuses is said with its length, the last packet's
    SSRC and its first bytes. Seen before this: one AAC frame refused (error 20, decoder
    reset) about 0.7 s before each TEARDOWN at the end of a YouTube item
    (`tools/tests/test_airplay_buffered_ssrc.py`).

15. **GET /info announces the room's volume** (`port/rtsp_handlers.c`, `port/bplist_builder.c`,
    `port/airplay2.c`). Upstream's /info carries no `initialVolume`, so a sender restated the
    level it last used with this receiver as it started, and the room jumped to it. Both answers
    now carry `initialVolume`: the binary one through `bplist_build_info_response_vol()` (two
    more objects; upstream's `bplist_build_info_response()` and its declaration in
    `upstream/plist/plist.h` stay, as a wrapper), the XML one through a port-side
    `plist_dict_real()` because `upstream/plist/plist_xml.c` has no `<real>`. The value is the
    port's volume, rounded to 0.01 dB so both answers carry the same number, which the player
    module keeps at the room's level through `airplay2_report_volume()` (no AIRPLAY2_EV_VOLUME).
    A client takes its slider from that key and sends no level until somebody moves it, as UxPlay
    documents for its clients (`tools/tests/test_airplay_mode.py`).

## Updating

Clone upstream at the new commit, compare it with `upstream/` file by file, copy across,
regenerate `upstream.sha256`, update the commit above, and re-check every entry of the two
tables above against the new tree — a file that moved between "taken" and "not taken" is
the part a plain diff will not show.
