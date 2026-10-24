/**
 * @file redirect.h
 * @brief Force-included into every file under upstream/ (see CMakeLists.txt), and nowhere else.
 *
 * The one change to upstream's behaviour that cannot live in a file of our own, because the
 * call sits in the middle of upstream functions. It is made here, by name, so the upstream
 * files themselves stay byte-identical — UPSTREAM.md, "Local changes".
 *
 * esp_read_mac(ESP_MAC_WIFI_STA): upstream derives the receiver's device id from the Wi-Fi
 * station MAC in three places — rtsp_get_device_id (the id /info reports), the AirPlay 1
 * Apple-Challenge response, and the accessory info pair-verify signs; the fourth, the RAOP
 * service name, is in mdns_airplay.c, which lives in port/ whole. The ESP32-P4 has no Wi-Fi
 * of its own — SOC_WIFI_SUPPORTED is not defined
 * for it — so esp_read_mac returns ESP_ERR_NOT_SUPPORTED for that type and leaves the buffer
 * as it found it: uninitialised stack. The id the phone pairs with would be whatever was on
 * the stack at the time, and pair-verify signs it. airplay2_read_mac answers that one type
 * with the identity the module passed in (the base MAC, like every other "who am I" string in
 * this firmware) and hands every other type to the real function.
 *
 * Defined before any upstream header is read, the macro renames the declaration in
 * esp_mac.h too, so the redirected calls are type-checked against the real prototype.
 */

#pragma once

#define esp_read_mac airplay2_read_mac
