**English** · [Українська](MIC_CAPTURE.uk.md)

# On-board microphone capture (click diagnostics)

A bench feature: it records what the panel's microphone hears around the start of playback — to hear and
see the click at a track switch. It does not measure the DAC's electrical output and does not prove a
hardware cause. Place the panel next to an external speaker; the on-board speaker is off.

The same codec and I2S port are used by the port measurement (`audio.port_meas`, [AUDIO.md](AUDIO.md), "Speaker calibration: port frequency") through
`components/mic_capture/src/mic_input.cpp`: it is always built and opens the microphone only for the
duration of the measurement. Both drive I2S2 and the ES8311, and nothing in the firmware stops them from
doing it at the same time — do not arm the capture while a port measurement is running, and vice versa.

## Build

```
idf.py -DMODESP_MIC_CAPTURE=ON -DMODESP_AUDIO_DIAGNOSTIC_CONSTANT=OFF build
```

`MODESP_MIC_CAPTURE` is off by default; CMake caches an explicit `ON` until it is set back to `OFF`.
`MODESP_AUDIO_DIAGNOSTIC_CONSTANT=OFF` resets a cached diagnostic mode of the audio core in which the DAC
receives only a keepalive instead of programme material. Normal playback remains. With the option on, the
`mic_capture` task (core 0) waits for commands from the USB console; capture does not start by itself at
boot. NVS and `board.json` are not changed.

## Connections

| Function | Connection |
|---|---|
| Microphone | analogue, ADC_MIC1_P / ADC_MIC1_N → ES8311 ADC |
| ADC SDOUT | GPIO48 (the net on the schematic is called `ES7210_SDOUT`; the codec is ES8311) |
| Codec MCLK / BCLK / LRCK | GPIO13 / GPIO12 / GPIO10 |
| I2C control | the shared HAL bus `i2c_0`: SDA GPIO7, SCL GPIO8 |
| I2S | I2S0 (`port_main`) and I2S1 (`port_bt`) belong to the audio core ([AUDIO.md](AUDIO.md), "I2S ports"); the microphone — I2S2 |

I2S2 is a receive master, Philips stereo 16 bit, nominally 48 kHz; only the left slot (MIC1) is kept.
The codec is a slave, MCLK = 256 × fs. The clock source is XTAL with a fractional divider: the P4 v1.3
has no PLL160 for I2S, and the APLL stays free for the output, which retunes its rate to follow the
source. A fractional divider gives the nominal average rate, not clean clocks and not calibration accuracy.

## Codec

The driver is `espressif/esp_codec_dev` 1.4.0 (C, no binary dependencies for P4 v3). Detection:

1. Probe the ES7210 addresses 0x40..0x43. If exactly one answers — ES7210 (by ACK only, without
   checking an identifier). On this board nobody answers there.
2. Otherwise ES8311 at 0x18 / 0x19: before any write the identifier
   FD = 0x83, FE = 0x11 must read back ([Everest ES8311 datasheet, rev. 10](https://files.waveshare.com/wiki/common/ES8311.DS.pdf)).
3. Not exactly one codec — the capture refuses (`expected one supported microphone codec, found N`).

Only the ADC is enabled; there is no DAC reference signal in the capture. Before capturing, GPIO11 (PA_CTRL of
the on-board NS4150) is set LOW; GPIO34 (the external amplifier) belongs to the audio core and is not
touched here.

Gain — 18 dB by default, commands accept 0..30 in steps of 3; on the ES8311 it is rounded down to a
6 dB step. It is applied **after** `enable`, because driver 1.4.0 rewrites the gain during
`enable`. A new value takes effect from the next capture. There is no software AGC, AEC or noise
suppression; the driver's filtering remains.

## Capture and download

It needs the IDF Python (which has pyserial). The port must not be open in another program.

```
python tools/mic_capture.py status
python tools/mic_capture.py capture --output build/mic-switch-01
```

`--port` — default `COM12`; on Linux — `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_*-if00`.
The tool sets DTR and RTS to False before opening the port; on Windows the panel then does not
reboot. On Linux the `cdc_acm` driver raises both lines on open, and pyserial
then clears DTR before RTS — that is the USB-Serial/JTAG reset pulse, so opening the port resets the P4.

The `capture` sequence:

1. `mic arm`: a 192000-byte buffer in PSRAM (2 s mono 16 bit, allocated at the first capture and
   kept), codec and I2S2 initialisation, 1 s of warm-up is discarded, then `ARMED`.
2. After `ARMED` — switch to another track. The marker is set by the audio core when the source gate
   from the ring opens (a file from SD, radio/DLNA, AirPlay, USB). The capture keeps up to 0.5 s before
   the marker and 1.5 s after, then turns off the codec and I2S2.
3. The tool asks for `mic dump` itself. The WAV is transferred only after the capture stops: during the
   capture there is neither SD writing nor network transfer.

The capture is invalid if 45 s have passed since arming, the I2S RX queue overflowed, or a read was
short or failed (`capture invalid: …`): missing samples cannot be read as an acoustic transient.
The I2S DMA and the task stack are in internal memory.

Transfer — numbered hex lines `@MIC_DATA` and FNV-1a over the whole file (`@MIC_END fnv1a=…`).
The tool accepts the WAV only after checking the order, length and checksum, and saves `.wav`,
`.json` (metadata) and `.uart.log`; it does not overwrite an existing `.wav` — a new name for every
measurement. If the download failed, `dump` repeats it from the still-frozen capture.

```
python tools/mic_capture.py capture --trigger-after 3 --output build/mic-ambient-01
python tools/mic_capture.py dump --output build/mic-retry-01
python tools/mic_capture.py gain --gain 12
python tools/mic_capture.py cancel
```

`--trigger-after N` (0.5…35 s after `ARMED`) sets the marker by hand — for ambient sound or a quiet
clap; the start of playback can set it earlier. Before comparing captures, look at `peak` and the
number of clipped samples in the line `READY: peak=… clipped=…`. Do not tap the board: that is a mechanical
coupling which does not exist when playing through a speaker. A failed `capture` sends `mic cancel`.

USB console commands directly: `mic arm`, `mic status`, `mic trigger`, `mic dump`, `mic cancel`,
`mic gain N`. The console is physical USB only; there is no HTTP route to this capture.

## Metadata

- `pre` — the WAV sample index at the marker boundary.
- `marker_ms` — firmware uptime at the moment of the marker; `armed_ms` — the moment of arming, **not** WAV sample zero.
- The boundary is the completion of the next 10 ms microphone DMA block, not sample-accurate alignment
  with the DAC; sound propagation and the filtering of the speaker and the microphone add delay
  (`trigger_alignment` in the `.json`).
- `gain` — the applied gain, `codec` — the detected codec.

The capture adds load; compare with playback without an armed capture.
