#include "modesp/net/mdns_guard.h"
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace modesp {
namespace {
std::atomic<SemaphoreHandle_t> gate{nullptr};
}
MdnsGuard::MdnsGuard(uint32_t wait_ms) {
    SemaphoreHandle_t mutex = gate.load();
    if (!mutex) {
        SemaphoreHandle_t candidate = xSemaphoreCreateMutex();
        if (!candidate) return;
        SemaphoreHandle_t expected = nullptr;
        if (gate.compare_exchange_strong(expected, candidate)) mutex = candidate;
        else { vSemaphoreDelete(candidate); mutex = expected; }
    }
    const TickType_t wait = wait_ms == WAIT_FOREVER ? portMAX_DELAY : pdMS_TO_TICKS(wait_ms);
    held_ = xSemaphoreTake(mutex, wait) == pdTRUE;
}
MdnsGuard::~MdnsGuard() {
    if (held_) xSemaphoreGive(gate.load());
}
} // namespace modesp
