# Onboard microphone click diagnostics

> Since 03.10.2026 the same codec and port also serve the port meter (`audio.port_meas`,
> docs/DSP_V2_DESIGN_2026-09-30.md §12.4) through `components/mic_capture/src/mic_input.cpp`,
> which is always built and opens the microphone only for the length of a measurement. It reuses
> the init sequence verified here (ES8311 path only). Do not arm this bench capture while a port
> measurement runs: both drive I2S2 and the ES8311.

This bench feature captures what the panel's microphone hears. It does not measure
the DAC's electrical output or prove a hardware cause. Put the panel near the actual
external loudspeaker. The onboard speaker stays disabled. The earlier constant-output
experiment is inconclusive: the user did not hear it before the crash.

## Build and wiring

Enable with `idf.py -DMODESP_MIC_CAPTURE=ON -DMODESP_AUDIO_DIAGNOSTIC_CONSTANT=OFF build`.
The default is OFF; CMake caches an explicit ON until changed back. Normal playback
is preserved. When enabled, a diagnostic task waits for USB commands; recording is
not automatically started at boot. NVS and board.json need no changes.

Verified against the supplied Guition schematic and ESP-IDF5.5.5 P4 headers:

| Function | Connection |
|---|---|
| Analog microphone | ADC_MIC1_P / ADC_MIC1_N, ES8311 ADC (user confirmed codec) |
| ADC SDOUT | GPIO48 (schematic net is misleadingly named ES7210_SDOUT) |
| Codec MCLK / BCLK / LRCK | GPIO13 / GPIO12 / GPIO10 |
| Shared HAL I2C0 SDA / SCL | GPIO7 / GPIO8 |
| I2S allocation | 0 external DAC/ADC, 1 Bluetooth, 2 microphone |

I2S2 is master RX, Philips stereo16 at nominal48kHz; only MIC1's left slot is
retained. The codec is slave, MCLK256x. Explicit XTAL source avoids taking an APLL
reference and leaves output rate switching available. PLL160 is unavailable on
this board's silicon rev1.3. Fractional XTAL division gives the nominal average
rate, not jitter-free clocks or calibration accuracy.

The driver is pinned to Espressif esp_codec_dev1.4.0, the series used by the local
IDF ES7210 example. Its source is C, with no P4v3-only binary dependency. The initial
assumption of ES7210 from the net name was wrong: no ACK at0x40..43, and the user
confirmed ES8311. The driver checks ES8311 at its documented0x18/19 addresses and
requires read-only chip identity FD=0x83/FE=0x11 before configuration, as specified
in the [Everest ES8311 datasheet, revision10](https://files.waveshare.com/wiki/common/ES8311.DS.pdf).
Only ADC mode is enabled; no DAC reference is inserted into capture. The onboard
NS4150 PA_CTRL11 is held low, and external amplifier GPIO34 remains owned by audio.
The code also retains ES7210 support for a uniquely responding0x40..43 device;
that path uses ACK detection, not chip-ID verification, and is unverified here.
The full board codec/strap sheet has not been supplied. Hardware capture validates
the microphone routing and serial framing. Gain is applied AFTER enable because
ES7210 driver1.4.0 rewrites it during enable. Default gain18dB; commands accept0..30
in3dB steps, rounded down to6dB steps for ES8311. Exported metadata records applied
gain and codec. No software AGC, AEC or denoising; official driver filtering remains.

## Recording and download

Use the project's ESP-IDF Python (pyserial installed). Close other COM12 readers.
The tool opens with DTR and RTS disabled; it does not intentionally reboot the panel.

```text
python tools/mic_capture.py status
python tools/mic_capture.py capture --output build/mic-switch-01
```

After `ARMED`, select another SD or DLNA track. The audio gate start freezes up to
0.5s of rolling history and records1.5s afterwards, then shuts down the microphone
codec and I2S2. The WAV is downloaded only after acquisition has stopped. There is
no SD write or network transfer during capture. One192000-byte PSRAM buffer is
allocated lazily and retained for reuse; I2S DMA and task stack use internal memory.
One second of microphone startup is discarded. A45s session timeout, RX queue
overflow, short read or read error invalidates the capture; missing samples must
not be interpreted as an acoustic transient.

The USB transport carries numbered hex chunks and whole-file FNV-1a checksum.
The tool accepts a WAV only after verifying order, length and checksum, and saves
`.wav`, `.json` metadata and `.uart.log`. Use a new output basename per measurement.
If a download fails, `dump` retries the still-frozen capture without recording again.

```text
python tools/mic_capture.py capture --trigger-after 3 --output build/mic-ambient-01
python tools/mic_capture.py dump --output build/mic-retry-01
python tools/mic_capture.py gain --gain 12
python tools/mic_capture.py cancel
```

`--trigger-after` requests a manual marker after arming, useful for ambient sound or
a gentle clap. A playback start can trigger earlier. Check reported peak and clipped
sample count before comparing recordings. Do not tap the PCB to establish acoustic
response: that introduces mechanical coupling not present in speaker playback.

Direct USB commands are `mic arm`, `mic status`, `mic trigger`, `mic dump`,
`mic cancel`, `mic gain N`. The console is physical USB only; no unauthenticated
HTTP microphone route is introduced. Gain changes apply to the next capture.

`pre` is the WAV sample index of the trigger boundary. `marker_ms` is firmware
uptime at the audio/manual marker; `armed_ms` is arming time, NOT WAV sample-zero
time. The boundary is the next10ms microphone DMA completion, not sample-exact DAC
alignment. Acoustic propagation and speaker/microphone filtering add further delay.
This diagnostic adds load and must be compared with unarmed playback. Soak testing
remains on hold at the user's request.
