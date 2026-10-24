/**
 * @file device_id.cpp
 * @brief Device identity from the base MAC (see device_id.h).
 */

#include "modesp/services/device_id.h"
#include "esp_mac.h"
#include "esp_log.h"

#include <cstdio>

static const char* TAG = "device_id";

namespace modesp {

namespace {

struct Identity {
    uint8_t mac[6] = {};
    char    id[7]  = "000000";
    bool    loaded = false;
};

Identity& identity() {
    static Identity s;
    if (!s.loaded) {
        esp_err_t err = esp_efuse_mac_get_default(s.mac);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_efuse_mac_get_default failed: %s", esp_err_to_name(err));
        }
        std::snprintf(s.id, sizeof(s.id), "%02X%02X%02X", s.mac[3], s.mac[4], s.mac[5]);
        s.loaded = true;
        ESP_LOGI(TAG, "base MAC %02X:%02X:%02X:%02X:%02X:%02X → id %s",
                 s.mac[0], s.mac[1], s.mac[2], s.mac[3], s.mac[4], s.mac[5], s.id);
    }
    return s;
}

} // namespace

const uint8_t* device_mac() { return identity().mac; }
const char*    device_id()  { return identity().id; }

} // namespace modesp
