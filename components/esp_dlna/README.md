**English** · [Українська](README.uk.md)

# esp_dlna — vendored

The three UPnP services a DLNA MediaRenderer has to answer — AVTransport, RenderingControl and
ConnectionManager — as service tables on top of Espressif's UPnP stack.

## Origin

`espressif/esp-adf`, branch `release/v2.x`, `examples/advanced_examples/dlna/components/esp_dlna`.
Licence: ESPRESSIF MIT, free of charge for use on Espressif chips (file headers).
Vendored rather than a dependency: in ADF the component sits in the example's tree and has no
package in the registry.

The UPnP stack under it is not vendored: `espressif/esp_media_protocols` 1.0.1 from the registry
(pinned in `idf_component.yml`; the library is prebuilt, and its esp32p4 archive contains no
instructions the P4 v1.3 lacks) does SSDP, the device description, SOAP parsing and GENA events, and
attaches to an existing `httpd_handle_t`.

## Local changes

Everything else is as in ADF, not reformatted: the diff against ADF has to stay readable. A
`p4-player:` comment in the `.c` files stands next to changes 1–5; the XML in item 5 and changes 6–7
carry no mark.

1. `esp_dlna.c` — `audio_mem.h` / `audio_calloc()` replaced with `<stdlib.h>` / `calloc()`: the ADF
   header would drag in `audio_sal` for the sake of one allocation.
2. `esp_dlna.c` — device description strings (`manufacturer` "ModESP", `model_name` — the
   project's device name from `generated/project_info.h`, "P4 Player" on the panel and "S3 Player"
   on the ESP32-S3 board — `model_description` naming the chip, URLs) in place of the ADF example's
   strings: the phone shows them in its device list.
3. `esp_dlna.c` — `esp_dlna_start()` frees the handle when `esp_upnp_init()` fails.
4. `esp_dlna_avt.c` — `SetAVTransportURI` also hands `CurrentURIMetaData` to the renderer as
   `AVT_SET_TRACK_METADATA`, so the renderer knows the track title.
5. `esp_dlna_cmr.c` and `xml/EventProtocolCMR.xml` — ConnectionManager's sink protocol list names
   only what the firmware decodes: MP3 (`audio/mpeg`, `audio/mp3`, `audio/x-mpeg`, `audio/mpeg3`), FLAC
   (`audio/flac`, `audio/x-flac`), WAV (`audio/wav`, `audio/x-wav`, `audio/wave`) and the playlists
   `modules/radio` parses (`audio/mpegurl`, `audio/x-mpegurl`, `audio/x-scpls`). No
   `http-get:*:*:*`, no L16 and no AAC. A declared format the decoder does not take plays as silence,
   with no error; Music Assistant decides from this list what to transcode on the server.
6. `esp_dlna_avt.c`, `include/esp_dlna.h` — next-track queue: the `SetNextAVTransportURI` and
   `GetCurrentTransportActions` actions; `NextURI` / `NextURIMetaData` in `GetMediaInfo` and the
   `NextAVTransportURI`, `NextAVTransportURIMetaData`, `CurrentTransportActions`, `TransportStatus`
   events are taken through the callback rather than from constants. The new `renderer_request_type_t`
   values (`AVT_SET_NEXT_URI`, `AVT_SET_NEXT_METADATA`, `AVT_GET_NEXT_URI`, `AVT_GET_NEXT_METADATA`,
   `AVT_GET_ACTIONS`) are appended to the end of the enum so the existing values do not shift.
7. `httpd_uri_compat.c`, `include/esp_dlna_compat.h` — new files:
   - ABI shim. The `esp_media_protocols` library is built without `CONFIG_HTTPD_WS_SUPPORT` and passes
     a 16-byte `httpd_uri_t`, while in the firmware it is 24 bytes (WebSocket for the web UI); httpd
     would read `supported_subprotocol` past the end of the object. `httpd_register_uri_handler` is
     wrapped at link time (`-Wl,--wrap`, `CMakeLists.txt`), and while `esp_dlna_httpd_compat(true)` is
     on — only for the duration of `esp_dlna_start()` — the request is rebuilt from the four real
     fields. All other registrations, WebSocket included, pass unchanged.
   - AVTransport handler replacement. `esp_dlna_avt_handler()` sets a handler of its own; during that
     same registration the library's `/AVTransport/…` POST routes get it instead of its dispatcher.

Six XML files (`rootDesc`, `rootDescLogo`, `rootDescService`, `devicelist`, `SoapResponseOk`,
`SoapResponseErr`) are needed by the prebuilt library, not by this component; they are embedded here,
and the component is linked `WHOLE_ARCHIVE` (`CMakeLists.txt`).

## How it is used

`modules/dlna` owns the handle. Before `esp_dlna_start()` the module removes httpd's wildcard route,
registers `esp_dlna_avt_handler(avt_soap)` and turns on `esp_dlna_httpd_compat(true)`; afterwards it
turns the shim off and puts the wildcard back.

- **AVTransport** (`POST /AVTransport/Control`) never reaches the library's callback: it is parsed by
  the module's `avt_soap()` (`modules/dlna/src/dlna_soap.inc`), which builds the replies itself and
  returns real UPnP errors (`500` with `UPnPError`). So the AVTransport actions in the `esp_dlna_avt.c`
  table (change 4 and the actions of change 6) are not executed on SOAP requests; only the events
  from that table take effect.
- **RenderingControl, ConnectionManager** and the values for GENA events (AVTransport's included) go
  through the one callback of `esp_dlna_start()` as `renderer_request_type_t`; the module translates
  them into SharedState keys and back.
- Events are sent by `esp_dlna_notify()` / `esp_dlna_notify_avt_by_action()` — this is how the volume
  slider and the play button in the control point follow the panel.

The DLNA renderer and the media server browser as the user sees them: [NETWORK.md](../../docs/NETWORK.md).
