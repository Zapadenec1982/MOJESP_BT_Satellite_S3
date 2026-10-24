/**
 * @file esp_littlefs.h
 * @brief HOST BUILD: esp_littlefs stub. Nothing is mounted on the host — ConfigService::load()
 *        is pointed at a fixture directory instead — but config_service.cpp has to compile.
 */
#pragma once

#include "esp_err.h"   // esp_err_t, ESP_OK, esp_err_to_name

typedef struct {
    const char* base_path;
    const char* partition_label;
    unsigned char format_if_mount_failed;
    unsigned char dont_mount;
} esp_vfs_littlefs_conf_t;

static inline esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t*) { return ESP_OK; }
