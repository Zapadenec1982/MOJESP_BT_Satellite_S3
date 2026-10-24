# AirPlay modes

Choose the receiver profile on the web **AirPlay** page, or tap the AirPlay mode row
under the panel's **Settings → Services**. The AirPlay enable switch is separate.

| Setting | Profile | Behavior |
| --- | --- | --- |
| `airplay.mode = 0` | AirPlay 2 (default) | Existing AirPlay 2 behavior; `_airplay._tcp` and `_raop._tcp` on port 7000. |
| `airplay.mode = 1` | AirPlay 1 / Windows compatibility | Classic RAOP advertisement only, port 5000, `AirTunes/105.1`. |

The selection is saved and applied automatically without restarting P4. Only one
receiver profile is advertised at a time. Changing profiles disconnects the current
AirPlay session while the receiver switches; other player services keep running.
Reconnect from the sender after the new profile becomes active.

The web page shows the selected profile, the currently active profile, switching
status, and any error. The panel mode row shows the selection with a switching or
error indication. No restart action is needed. If switching fails, inspect the
error on the web AirPlay page before retrying.

The compatibility profile has no AirPlay 2 features or AirPlay 2 multiroom support.
The Windows label describes its intended compatibility purpose; successful discovery
or protocol probes do not establish that Apple Music on Windows can play audio.
Direct Apple Music playback on Windows remains unresolved: in the captured failed
attempts the sender did not start a media session on P4. AirPlay 1 playback through
TuneBlade is confirmed, including Apple Music with **My Computer** as its selected
output and TuneBlade forwarding system audio to P4. This is a workaround, not a
confirmation that the direct Apple Music connection works.

## Implementation and checks

`airplay.mode` is the persisted selection, restored at boot and observed for live
changes. `airplay.active_mode` and `airplay.active_mode_label` expose the active
profile. `airplay.switching` indicates an operation in progress;
`airplay.mode_status` provides readable status and `airplay.mode_error` is empty
when there is no error. The shared policy in
`components/airplay2/port/mode_policy.h` controls discovery, listener port, Server
version, and XML/binary `/info` responses.

From the repository root, run the wire-profile checks with Python and a native C
compiler:

```sh
python tools/tests/test_airplay_mode.py
python tools/tests/test_airplay_mode_module.py
python tools/tests/test_airplay_reconfigure.py
```

These checks exercise both profiles using mocked transport and mDNS. On hardware,
check switching in both directions while idle and during AirPlay playback, confirm
the active profile and discovery records, and verify that device uptime does not
reset. Device playback and sender interoperability require separate hardware tests.

Hardware checks on 2026-09-28 passed switching in both directions while idle and
with an open RTSP control connection. The previous connection closed, the new
listener and discovery profile matched the selection, and P4 uptime continued
without a reset. Measured transitions took 0.34–0.72 seconds. Bluetooth playback
continued during those checks. Switching during an actual AirPlay audio stream,
extended playback/recovery and AirPlay 2 multiroom still need end-to-end verification.

Artwork on the AirPlay 2 profile was checked on 2026-10-13 with an iPhone
(AirPlay/620.8.2) and Apple Music: every track sent a 512×512 JPEG of 19–83 KB, each
was shown, and `image/none` came between tracks. An AirPlay 2 sender asks for covers by
the features (bit 15), not by `md`; until that bit was advertised it sent the title and
progress but never a cover. A sender keeps the features it read when the receiver was
chosen: after a firmware that changes them, choose another output on the phone and then
P4 again, or the old answer stays in force.

Original files under `components/airplay2/upstream/` remain byte-identical to the
vendored source. Adapted copies live in `port/`; provenance, license, and the exact
local changes are documented in [UPSTREAM.md](../components/airplay2/UPSTREAM.md).
