/**
 * @file liveness.cpp
 * @brief The liveness registry: fixed slots, claimed without a lock.
 */

#include "modesp/liveness.h"

#include <string.h>

namespace modesp {
namespace liveness {

namespace {

enum : uint8_t { FREE = 0, CLAIMED = 1, LIVE = 2 };

struct Slot {
    std::atomic<uint8_t> state{FREE};
    Entry                entry;
};

Slot g_slots[MAX_ENTRIES];

bool same_name(const char* a, const char* b)
{
    return a && b && strcmp(a, b) == 0;
}

}  // namespace

bool watch(const char* name, const Counter* counter, uint32_t timeout_ms,
           BaseModule* owner, bool restart)
{
    if (!name || !counter) return false;

    // Best effort against a double registration: a task restarted without an unwatch()
    // in between would otherwise hold two slots and be reported twice.
    for (auto& s : g_slots)
        if (s.state.load(std::memory_order_acquire) == LIVE && same_name(s.entry.name, name))
            return false;

    for (auto& s : g_slots) {
        uint8_t expected = FREE;
        if (!s.state.compare_exchange_strong(expected, CLAIMED, std::memory_order_acq_rel))
            continue;
        Entry& e     = s.entry;
        e.name       = name;
        e.counter    = counter;
        e.timeout_ms = timeout_ms;
        e.owner      = owner;
        e.restart    = restart;
        e.last_value     = 0;
        e.last_change_ms = 0;
        e.seen           = false;
        e.stalled        = false;
        s.state.store(LIVE, std::memory_order_release);
        return true;
    }
    return false;
}

void unwatch(const char* name)
{
    for (auto& s : g_slots)
        if (s.state.load(std::memory_order_acquire) == LIVE && same_name(s.entry.name, name))
            s.state.store(FREE, std::memory_order_release);
}

Entry* entry(size_t i)
{
    if (i >= MAX_ENTRIES) return nullptr;
    Slot& s = g_slots[i];
    return s.state.load(std::memory_order_acquire) == LIVE ? &s.entry : nullptr;
}

size_t count()
{
    size_t n = 0;
    for (auto& s : g_slots)
        if (s.state.load(std::memory_order_acquire) == LIVE) n++;
    return n;
}

Event check(Entry& e, uint32_t now_ms, uint32_t default_timeout_ms)
{
    const uint32_t v = e.counter->load(std::memory_order_relaxed);

    if (!e.seen) {
        e.seen           = true;
        e.last_value     = v;
        e.last_change_ms = now_ms;
        return Event::NONE;
    }

    if (v != e.last_value) {
        e.last_value     = v;
        e.last_change_ms = now_ms;
        if (e.stalled) {
            e.stalled = false;
            return Event::RECOVERED;
        }
        return Event::NONE;
    }

    const uint32_t timeout = e.timeout_ms ? e.timeout_ms : default_timeout_ms;
    if (e.stalled || (uint32_t)(now_ms - e.last_change_ms) < timeout) return Event::NONE;

    e.stalled = true;
    return Event::STALLED;
}

}  // namespace liveness
}  // namespace modesp
