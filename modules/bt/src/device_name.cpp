/**
 * @file device_name.cpp
 * @brief The panel's copy of its one name, in NVS (device_name.h).
 */

#include "device_name.h"
#include "modesp/services/nvs_helper.h"
#include <cstdio>
#include <cstring>

namespace device_name {
namespace {
constexpr const char* NVS_NS  = "devname";
constexpr const char* NVS_KEY = "name";
}  // namespace

void get(char* out, size_t n) {
    if (!out || !n) return;
    out[0] = '\0';
    if (!modesp::nvs_helper::read_str(NVS_NS, NVS_KEY, out, n) || !out[0])
        snprintf(out, n, "%s", FALLBACK);
}

bool kept() {
    char buf[CAP] = "";
    return modesp::nvs_helper::read_str(NVS_NS, NVS_KEY, buf, sizeof buf) && buf[0];
}

void keep(const char* name) {
    if (!name || !name[0] || strlen(name) >= CAP) return;
    char cur[CAP] = "";
    if (modesp::nvs_helper::read_str(NVS_NS, NVS_KEY, cur, sizeof cur) && strcmp(cur, name) == 0) return;
    modesp::nvs_helper::write_str(NVS_NS, NVS_KEY, name);
}

}  // namespace device_name
