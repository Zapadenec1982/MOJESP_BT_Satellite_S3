/**
 * @file dac.h
 * @brief Upstream's DAC API, as far as the vendored core refers to it.
 *
 * rtsp_handlers.c includes a header of this name and calls dac_set_volume() only when
 * upstream's own Bluetooth sink is built (CONFIG_BT_A2DP_ENABLE), to undo what Bluetooth did
 * to the volume. That never happens here: Bluetooth is the satellite's, and the phone's
 * volume reaches the audio core through settings_set_volume() instead.
 *
 * Declared so the include resolves. Deliberately not defined: an upstream that starts
 * calling it unconditionally should fail the link, not silently do nothing.
 */

#pragma once

void dac_set_volume(float volume_db);
