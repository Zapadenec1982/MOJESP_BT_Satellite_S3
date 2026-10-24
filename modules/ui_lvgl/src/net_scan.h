#pragma once
/**
 * @file net_scan.h
 * @brief The Wi-Fi page's scan, off every task that has a deadline.
 *
 * WiFiService::start_scan() blocks until the C6 has walked every channel — seconds, over an
 * esp_hosted RPC. The main loop feeds a five-second watchdog and the LVGL task draws the
 * screen, so neither may wait for it: a scan runs on a short-lived task of its own and leaves
 * its answer here, for the page to pick up on its next refresh.
 *
 * The list is the page's, not the driver's: one row per network name (a mesh answers from
 * several access points under one name, and the strongest is the one that will be joined),
 * hidden networks left out (there is no name to show or to join by), strongest first.
 */

#include <cstdint>

namespace net_scan {

struct Net {
    char    ssid[33];
    int8_t  rssi;
    uint8_t auth;      ///< wifi_auth_mode_t
};

constexpr int MAX = 20;

enum class State : uint8_t { IDLE, BUSY, DONE, FAILED };

/// Start a scan. False while one is running, while the Wi-Fi service is trying a network, or
/// when the task cannot be created. Any task.
bool start();

State    state();
/// Bumps when a scan ends, well or not — the page's cue to copy the list again.
uint32_t seq();
/// The last good list, strongest first; returns how many were copied.
int copy(Net* out, int max);

}  // namespace net_scan
