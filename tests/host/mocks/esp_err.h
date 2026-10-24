/**
 * @file esp_err.h
 * @brief HOST BUILD: the corner of esp_err.h that host-built sources use. One copy, so that
 *        a translation unit pulling in two IDF stubs (esp_littlefs.h and nvs.h, say) does
 *        not define esp_err_to_name twice.
 */
#pragma once

#include "hal_types_mock.h"   // esp_err_t, ESP_OK, ESP_FAIL, ESP_ERR_NOT_FOUND

#include <cstdio>
#include <cstdlib>

/// Only ever printed in a log line here, so a name per code is not worth keeping in step.
static inline const char* esp_err_to_name(esp_err_t err) {
    return err == ESP_OK ? "ESP_OK" : "ESP_ERR (host stub)";
}

#define ESP_ERROR_CHECK(x) do {                                                  \
        esp_err_t err_rc_ = (x);                                                 \
        if (err_rc_ != ESP_OK) {                                                 \
            fprintf(stderr, "ESP_ERROR_CHECK failed: 0x%x at %s:%d\n",           \
                    (unsigned)err_rc_, __FILE__, __LINE__);                      \
            abort();                                                             \
        }                                                                        \
    } while (0)
