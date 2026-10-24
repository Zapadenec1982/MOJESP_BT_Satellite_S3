/**
 * @file settings.h
 * @brief Upstream's settings API, as far as the vendored core calls it.
 *
 * Upstream keeps its settings in NVS behind a header of this name (settings.c, not taken).
 * The core calls four of those functions — rtsp_conn.c the volume ones, rtsp_handlers.c the
 * name — and these are the same four, with the same signatures, answered by port/airplay2.c
 * from what the module passed in and what the phone last said.
 *
 * Nothing here writes flash. The volume the phone sets becomes audio.volume, which the
 * player already persists; a second copy in upstream's NVS namespace would only be a second
 * answer to the same question.
 */

#pragma once

#include <stddef.h>

#include "esp_err.h"

esp_err_t settings_get_volume(float *volume_db);
esp_err_t settings_set_volume(float volume_db);
esp_err_t settings_persist_volume(void);
esp_err_t settings_get_device_name(char *name, size_t len);
