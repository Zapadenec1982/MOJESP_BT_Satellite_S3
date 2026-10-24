#pragma once
#include <cstddef>
#include <cstdint>
#include "driver/i2c_master.h"

// The onboard microphone as a measuring input: ES8311 ADC (MIC1) on I2S2, 48 kHz, XTAL-clocked so
// the output's APLL stays free to follow the source. Opened for a measurement and closed after:
// nothing is powered or allocated in between. One user at a time, and not together with the
// bench capture (mic_capture.h), which drives the same codec and port. Wiring and init sequence
// are the bench capture's, verified on the panel (docs/MIC_CAPTURE.md).
namespace mic_input {

constexpr unsigned RATE = 48000;

// Find the ES8311 (identity FD=83, FE=11 at 0x18/0x19), hold the onboard NS4150 off, start I2S2
// and the ADC at `gain_db` (0…30, in 6 dB steps). False if any of it fails; nothing is left
// running then.
bool open(i2c_master_bus_handle_t bus, unsigned gain_db);
// `n` mono samples (MIC1's left slot) scaled to ±1, blocking up to `timeout_ms`. False on a short
// read or when the DMA queue overflowed since the previous read: the samples are then not one
// continuous stretch. `peak`, if given, gets the largest |sample|.
bool read(float* out, size_t n, uint32_t timeout_ms, float* peak = nullptr);
// Read and drop about `ms` of samples: after a pause in reading, what the DMA holds is old.
void discard(uint32_t ms);
void close();

}  // namespace mic_input
