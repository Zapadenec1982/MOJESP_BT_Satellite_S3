/**
 * @file liveness.h
 * @brief Liveness counters: proof of progress from the task that does the work.
 *
 * A module whose work happens on its own task (a decoder, a UART reader, the LVGL loop)
 * bumps a counter every time round its loop. WatchdogService samples every registered
 * counter once a second and reports the ones that stopped moving.
 *
 * Why not BaseModule::last_update_ms(): the main loop stamps it for every RUNNING module
 * on every pass, so a module whose own task is wedged still looks alive, and the one case
 * that would stop the stamps — the main loop itself hanging — is what the hardware task
 * watchdog already covers. A counter is bumped by the task that makes progress, so it
 * stops exactly when the progress does.
 *
 * Registry rules:
 *   - The counter and the name must have static storage duration: entries hold pointers.
 *   - watch()/unwatch() may be called from any task; the table is a fixed set of slots
 *     claimed with an atomic exchange, no lock and no allocation. The watcher may read an
 *     entry that is being replaced and see one torn sample — the cost is one spurious log
 *     line, never a crash, because every pointer in the table points at static storage.
 *   - check() and the watcher-side fields belong to one caller (WatchdogService, on the
 *     main task). Nobody else touches them.
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace modesp {

class BaseModule;

namespace liveness {

using Counter = std::atomic<uint32_t>;

/// Bump from the task that is making progress. Relaxed: nothing is ordered on it.
inline void tick(Counter& c) { c.fetch_add(1, std::memory_order_relaxed); }

struct Entry {
    const char*    name       = nullptr;   ///< as it should appear in the log
    const Counter* counter    = nullptr;
    uint32_t       timeout_ms = 0;         ///< 0 = the watcher's default for the owner's priority
    BaseModule*    owner      = nullptr;   ///< the module to blame, and to restart when asked
    bool           restart    = false;     ///< ModuleManager::restart_module() is a real recovery here

    // Watcher-side bookkeeping — see check().
    uint32_t last_value     = 0;
    uint32_t last_change_ms = 0;
    bool     seen           = false;
    bool     stalled        = false;
};

constexpr size_t MAX_ENTRIES = 16;

/// Registers a counter. False when the table is full or `name` is already watched.
bool watch(const char* name, const Counter* counter, uint32_t timeout_ms = 0,
           BaseModule* owner = nullptr, bool restart = false);

/// Forgets a counter — for a task that is torn down on purpose, such as a source that is
/// not selected. A counter that merely stops is exactly what watch() exists to catch.
void unwatch(const char* name);

/// Watcher side: slot `i` of MAX_ENTRIES, or nullptr when it is empty.
Entry* entry(size_t i);

/// How many slots are live right now (diagnostic; may change under the caller's feet).
size_t count();

enum class Event : uint8_t { NONE, STALLED, RECOVERED };

/// Advances one entry's bookkeeping against the counter's current value.
///
/// The first call only records where the counter stands: a task that has not started yet
/// gets a full timeout of grace from the moment it is first seen, not from boot.
/// `default_timeout_ms` applies when the entry declares none of its own.
Event check(Entry& e, uint32_t now_ms, uint32_t default_timeout_ms);

}  // namespace liveness
}  // namespace modesp
