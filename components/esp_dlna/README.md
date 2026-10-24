# esp_dlna — vendored

The three UPnP services a DLNA MediaRenderer has to answer — AVTransport, RenderingControl
and ConnectionManager — as service tables over Espressif's UPnP stack.

## Where it comes from

`espressif/esp-adf`, branch `release/v2.x`,
`examples/advanced_examples/dlna/components/esp_dlna`, fetched 18.09.2026.
Licence: ESPRESSIF MIT — free of charge for use on Espressif silicon, which is what this
board is. Same licence class as `esp_audio_codec`, settled in CONCEPT §10а.

It is **vendored, not depended on**: ADF ships it inside an example tree, with no package in
the component registry, and taking ESP-ADF whole is what CONCEPT §8 refused when it chose
`esp_audio_codec` over the full framework. Twenty-five kilobytes of service tables is a
cheaper thing to own than a second framework.

The UPnP stack under it is *not* vendored — `espressif/esp_media_protocols` 1.0.1 from the
registry does SSDP, the device description, SOAP dispatch and GENA events, and it attaches
to an `httpd_handle_t` we already have running.

## What was changed

Four edits, each marked `p4-player:` in the source:

1. `esp_dlna.c` — `audio_mem.h` / `audio_calloc()` replaced by `<stdlib.h>` / `calloc()`.
   ADF's header is a thin wrapper that would have pulled in `audio_sal`; the allocation is
   a few dozen bytes, once.
2. `esp_dlna.c` — `esp_dlna_start()` frees its handle when `esp_upnp_init()` fails.
   Upstream returns holding it.
3. `esp_dlna_avt.c` — `SetAVTransportURI` now also extracts `CurrentURIMetaData` and hands
   it to the renderer as `AVT_SET_TRACK_METADATA`. Upstream takes the URI and throws the
   DIDL-Lite away, so a renderer built on it can never show a track title. The request enum
   already had the value; no action pointed at it.
4. `esp_dlna_cmr.c` — the ConnectionManager sink list names the formats this firmware
   actually decodes (MP3, FLAC, WAV/L16, plus the two playlist types modules/radio resolves)
   instead of opening with `http-get:*:*:*` and promising WMA, AC3, Vorbis and MP4 as well.
   Claiming a format we cannot decode fails silently: the stream opens, the decoder refuses,
   and the log looks healthy. It is also what Music Assistant reads to decide what to
   transcode on the server, where there is a CPU to do it with.

Nothing else is reformatted. Keep it that way — a diff against ADF should stay readable.

## How it is used

`modules/dlna` owns the handle. `esp_dlna_start()` takes one callback, and every UPnP action
arrives through it as a `renderer_request_type_t`; the module turns those into SharedState
keys and back. Events flow the other way with `esp_dlna_notify*()`, which is what makes a
control point's volume slider and play button follow the panel instead of fighting it.
