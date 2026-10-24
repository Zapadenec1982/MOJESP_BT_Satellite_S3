/**
 * @file device_id.h
 * @brief Device identity derived from the base MAC in eFuse.
 *
 * Every Espressif target has a base MAC (esp_efuse_mac_get_default). The Wi-Fi
 * SoftAP MAC does not exist on chips without native Wi-Fi — the ESP32-P4 hosts
 * Wi-Fi on a co-processor — so esp_read_mac(ESP_MAC_WIFI_SOFTAP) is not a
 * portable identity source. Every "who am I" string (MQTT device id, default AP
 * SSID, mDNS hostname) must come from here.
 */

#pragma once

#include <cstdint>

namespace modesp {

/// Base MAC (6 bytes), read once from eFuse; all zeros if the read failed.
const uint8_t* device_mac();

/// Short id "XXYYZZ": last three MAC bytes, upper-case hex, NUL-terminated (7 bytes).
const char* device_id();

} // namespace modesp
