/**
 * @file freertos_mock.h
 * @brief HOST BUILD: FreeRTOS stubs using std::mutex.
 *
 * Replaces FreeRTOS semaphore API with C++ std::mutex.
 * Included first in every translation unit via host_prefix.h.
 */
#pragma once

// Guard against real FreeRTOS headers
#ifndef FREERTOS_MOCK_H
#define FREERTOS_MOCK_H

#include <atomic>
#include <mutex>
#include <cassert>
#include <cstdint>

// ── Semaphore → std::mutex ──
using SemaphoreHandle_t = std::mutex*;

inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    return new std::mutex();
}

// The target keeps a static mutex in the caller's buffer; the host one lives on the heap
// either way, so the buffer only has to exist.
struct StaticSemaphore_t {};
inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* /*storage*/) {
    return new std::mutex();
}

inline int xSemaphoreTake(SemaphoreHandle_t m, int /*timeout_ticks*/) {
    if (m) m->lock();
    return 1;  // pdTRUE
}

inline void xSemaphoreGive(SemaphoreHandle_t m) {
    if (m) m->unlock();
}

inline void vSemaphoreDelete(SemaphoreHandle_t m) {
    delete m;
}

// ── FreeRTOS macros ──
#define configASSERT(x)      assert(x)
#define pdTRUE               1
#define pdFALSE              0
#define pdMS_TO_TICKS(ms)    (ms)
#define portMAX_DELAY        0xFFFFFFFF

// ── Critical sections → std::mutex ──
// portMUX_TYPE is a spinlock on the target; a mutex gives the same exclusion here, and a
// test that holds one across a blocking call (which the firmware must never do) simply
// serialises instead of deadlocking a core.
struct portMUX_TYPE {
    std::mutex m;
};
#define portMUX_INITIALIZER_UNLOCKED {}

/// Runs on the task that has just left a critical section, which is where a task on the
/// target gets preempted once its interrupts are back on. Null except in a test that needs a
/// task stopped exactly there (test_error_service.cpp): load makes that interleaving rarely,
/// and never on demand.
inline std::atomic<void (*)()> host_after_exit_critical{nullptr};

inline void portENTER_CRITICAL(portMUX_TYPE* mux) { mux->m.lock(); }
inline void portEXIT_CRITICAL(portMUX_TYPE* mux)  {
    mux->m.unlock();
    if (auto hook = host_after_exit_critical.load()) hook();
}

// ── Task handle stub (not used in host tests) ──
using TaskHandle_t = void*;

#endif // FREERTOS_MOCK_H
