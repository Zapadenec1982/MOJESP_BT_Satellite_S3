/**
 * @file net_scan.cpp
 * @brief See net_scan.h.
 */

#include "net_scan.h"
#include "modesp/net/wifi_service.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>
#include <cstring>

namespace net_scan {
namespace {

const char* TAG = "net_scan";

/// Enough for start_scan()'s logging and the RPC underneath it; the records themselves are in
/// PSRAM (WiFiService::get_scan_results). The task lives for one scan and is gone.
constexpr uint32_t STACK = 4096;

/// PSRAM, taken on the first scan: internal .bss is what the SDIO pool and the I2S
/// descriptors share, and a list nobody has asked for yet has no business there.
Net*                s_nets = nullptr;
int                 s_count = 0;
portMUX_TYPE        s_mux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<State>  s_state{State::IDLE};
std::atomic<uint32_t> s_seq{0};

using Raw = modesp::WiFiService::ScanResult;

/// One row per name, the strongest of each, strongest first. n is at most MAX: insertion
/// sort is the whole algorithm.
int tidy(const Raw* raw, int n, Net* out) {
    int m = 0;
    for (int i = 0; i < n; i++) {
        if (raw[i].ssid[0] == '\0') continue;                   // hidden: nothing to show
        int k = 0;
        while (k < m && strcmp(out[k].ssid, raw[i].ssid) != 0) k++;
        if (k == m) {
            if (m == MAX) continue;
            memcpy(out[m].ssid, raw[i].ssid, sizeof(out[m].ssid));
            out[m].rssi = raw[i].rssi;
            out[m].auth = raw[i].authmode;
            m++;
        } else if (raw[i].rssi > out[k].rssi) {
            out[k].rssi = raw[i].rssi;
            out[k].auth = raw[i].authmode;
        }
    }
    for (int i = 1; i < m; i++) {
        const Net v = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].rssi < v.rssi) { out[j + 1] = out[j]; j--; }
        out[j + 1] = v;
    }
    return m;
}

void task(void*) {
    auto* w = modesp::WiFiService::instance();
    auto* raw = static_cast<Raw*>(heap_caps_calloc(modesp::WiFiService::MAX_SCAN_RESULTS, sizeof(Raw),
                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    auto* tidied = static_cast<Net*>(heap_caps_calloc(MAX, sizeof(Net), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    bool ok = w && raw && tidied && w->start_scan();
    int n = 0;
    if (ok) {
        const int got = w->get_scan_results(raw, modesp::WiFiService::MAX_SCAN_RESULTS);
        n = tidy(raw, got, tidied);
        portENTER_CRITICAL(&s_mux);
        memcpy(s_nets, tidied, sizeof(Net) * (size_t)n);
        s_count = n;
        portEXIT_CRITICAL(&s_mux);
    }
    ESP_LOGI(TAG, "%s, %d networks", ok ? "done" : "failed", n);
    heap_caps_free(raw);
    heap_caps_free(tidied);
    s_state = ok ? State::DONE : State::FAILED;
    s_seq++;
    vTaskDelete(nullptr);
}

}  // namespace

bool start() {
    auto* w = modesp::WiFiService::instance();
    if (!w || w->is_trying()) return false;
    if (!s_nets) {
        s_nets = static_cast<Net*>(heap_caps_calloc(MAX, sizeof(Net), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!s_nets) return false;
    }
    State expect = s_state.load();
    if (expect == State::BUSY) return false;
    if (!s_state.compare_exchange_strong(expect, State::BUSY)) return false;
    if (xTaskCreate(task, "net_scan", STACK, nullptr, 3, nullptr) != pdPASS) {
        s_state = State::FAILED;
        s_seq++;
        return false;
    }
    return true;
}

State state() { return s_state.load(); }

uint32_t seq() { return s_seq.load(); }

int copy(Net* out, int max) {
    if (!s_nets) return 0;
    portENTER_CRITICAL(&s_mux);
    const int n = s_count < max ? s_count : max;
    memcpy(out, s_nets, sizeof(Net) * (size_t)n);
    portEXIT_CRITICAL(&s_mux);
    return n;
}

}  // namespace net_scan
