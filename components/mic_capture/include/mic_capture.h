#pragma once
#include "driver/i2c_master.h"

namespace mic_capture {
#ifdef MODESP_MIC_CAPTURE
// Called once after HAL creation. Hardware stays idle until a USB "mic arm" command.
void start(i2c_master_bus_handle_t bus);
// Real-time safe: bounded critical section and timestamp; no I/O or allocation.
void mark_start();
#else
inline void start(i2c_master_bus_handle_t) {}
inline void mark_start() {}
#endif
}
