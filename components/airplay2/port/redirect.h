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

/*
 * Task stacks in PSRAM, on the ESP32-S3 board only.
 *
 * Upstream gives every task an internal-RAM stack — the realtime receiver alone takes 12 KB
 * (audio_stream_realtime.c), its control port 4 KB more — because on a plain ESP32 a flash write
 * switches the cache off and a PSRAM stack with it (upstream/spiram_task.h). The S3 runs its code
 * from PSRAM (CONFIG_SPIRAM_XIP_FROM_PSRAM), and with that IDF never switches the cache off for a
 * flash write, so a PSRAM stack is as safe as an internal one. What the S3 does not have is the
 * internal RAM: with Wi-Fi up there was not 12 KB in one piece left, the receiver task could not
 * be created and every AirPlay RECORD answered 500 (26.10.2026).
 *
 * So, for the upstream files only and only on that board, every task is created with its stack in
 * PSRAM and deleted the way such a task must be: vTaskDeleteWithCaps, which IDF 5.5 also accepts
 * from the task deleting itself — upstream's tasks all end in vTaskDelete(NULL) — and frees the
 * memory through a short clean-up task instead of leaking it. The FreeRTOS headers are read
 * first, so the macros rename the calls and never the declarations.
 */
#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32S3 && CONFIG_SPIRAM_XIP_FROM_PSRAM && CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#define xTaskCreate(fn, name, depth, param, prio, handle)                                       \
    xTaskCreatePinnedToCoreWithCaps((fn), (name), (depth), (param), (prio), (handle),            \
                                    tskNO_AFFINITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define xTaskCreatePinnedToCore(fn, name, depth, param, prio, handle, core)                     \
    xTaskCreatePinnedToCoreWithCaps((fn), (name), (depth), (param), (prio), (handle), (core),    \
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define vTaskDelete(task) vTaskDeleteWithCaps(task)
#endif
