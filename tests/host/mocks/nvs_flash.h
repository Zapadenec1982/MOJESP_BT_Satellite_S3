/**
 * @file nvs_flash.h
 * @brief HOST BUILD: partition-level NVS calls over the in-memory store in nvs.h.
 */
#pragma once

#include "nvs.h"

inline esp_err_t nvs_flash_init() { return ESP_OK; }

/// Like the real one: every namespace gone, every handle with it.
inline esp_err_t nvs_flash_erase() {
    host_nvs::store().spaces.clear();
    host_nvs::store().handles.clear();
    return ESP_OK;
}
