/**
 * @file test_liveness.cpp
 * @brief The liveness registry and the stall arithmetic WatchdogService runs on it.
 *
 * Pure C++: no FreeRTOS, no clock. The watcher passes the time in, so a stall that takes
 * thirty seconds on the device takes three calls here.
 */

#include "doctest.h"
#include "modesp/liveness.h"

#include <string>

using namespace modesp::liveness;

namespace {

/// Every case starts from an empty table, whatever the previous one left behind.
struct Clean {
    Clean()  { wipe(); }
    ~Clean() { wipe(); }
    static void wipe()
    {
        for (size_t i = 0; i < MAX_ENTRIES; i++)
            if (Entry* e = entry(i)) unwatch(e->name);
    }
};

Entry* find(const char* name)
{
    for (size_t i = 0; i < MAX_ENTRIES; i++)
        if (Entry* e = entry(i); e && std::string(e->name) == name) return e;
    return nullptr;
}

}  // namespace

TEST_CASE("a counter that keeps moving is never reported") {
    Clean   clean;
    Counter c{0};
    REQUIRE(watch("decoder", &c, 3000));
    Entry* e = find("decoder");
    REQUIRE(e != nullptr);

    CHECK(check(*e, 0, 30000) == Event::NONE);        // first sight: just a baseline
    for (uint32_t t = 1000; t <= 20000; t += 1000) {
        tick(c);
        CHECK(check(*e, t, 30000) == Event::NONE);
    }
    CHECK_FALSE(e->stalled);
}

TEST_CASE("a counter that stops is reported once, and its recovery once") {
    Clean   clean;
    Counter c{5};
    REQUIRE(watch("reader", &c, 3000));
    Entry* e = find("reader");
    REQUIRE(e != nullptr);

    CHECK(check(*e, 100, 30000) == Event::NONE);
    tick(c);
    CHECK(check(*e, 1100, 30000) == Event::NONE);     // moved at 1100
    CHECK(check(*e, 2100, 30000) == Event::NONE);     // still 1000 ms
    CHECK(check(*e, 3100, 30000) == Event::NONE);     // 2000 ms
    CHECK(check(*e, 4100, 30000) == Event::STALLED);  // 3000 ms: the timeout
    CHECK(e->stalled);
    CHECK(check(*e, 5100, 30000) == Event::NONE);     // reported already, stays quiet
    CHECK(check(*e, 60000, 30000) == Event::NONE);

    tick(c);
    CHECK(check(*e, 61000, 30000) == Event::RECOVERED);
    CHECK_FALSE(e->stalled);
    CHECK(check(*e, 62000, 30000) == Event::NONE);
}

TEST_CASE("the first sighting grants a full timeout of grace") {
    // A task registered at boot but not yet scheduled must not be blamed for the time
    // before the watcher first looked at it.
    Clean   clean;
    Counter c{0};
    REQUIRE(watch("late", &c, 2000));
    Entry* e = find("late");
    REQUIRE(e != nullptr);

    CHECK(check(*e, 50000, 30000) == Event::NONE);    // first seen at t = 50 s
    CHECK(check(*e, 51999, 30000) == Event::NONE);
    CHECK(check(*e, 52000, 30000) == Event::STALLED);
}

TEST_CASE("an entry without its own timeout takes the watcher's default") {
    Clean   clean;
    Counter c{0};
    REQUIRE(watch("plain", &c));                       // timeout 0
    Entry* e = find("plain");
    REQUIRE(e != nullptr);

    CHECK(check(*e, 0, 4000) == Event::NONE);
    CHECK(check(*e, 3999, 4000) == Event::NONE);
    CHECK(check(*e, 4000, 4000) == Event::STALLED);
}

TEST_CASE("the millisecond clock may wrap under a running entry") {
    Clean   clean;
    Counter c{0};
    REQUIRE(watch("wrap", &c, 3000));
    Entry* e = find("wrap");
    REQUIRE(e != nullptr);

    const uint32_t near_end = 0xFFFFFFFFu - 1000;
    CHECK(check(*e, near_end, 30000) == Event::NONE);
    tick(c);
    CHECK(check(*e, near_end + 500, 30000) == Event::NONE);
    CHECK(check(*e, 1000, 30000) == Event::NONE);      // 1500 ms later, across the wrap
    CHECK(check(*e, 2600, 30000) == Event::STALLED);   // 3100 ms after the last move
}

TEST_CASE("the table refuses a second entry with the same name and frees on unwatch") {
    Clean   clean;
    Counter a{0}, b{0};
    REQUIRE(watch("twice", &a, 1000));
    CHECK_FALSE(watch("twice", &b, 1000));
    CHECK(count() == 1);

    unwatch("twice");
    CHECK(count() == 0);
    CHECK(find("twice") == nullptr);

    REQUIRE(watch("twice", &b, 1000));                 // the slot is reusable
    CHECK(find("twice")->counter == &b);
}

TEST_CASE("a full table says so instead of dropping an entry silently") {
    Clean   clean;
    Counter c[MAX_ENTRIES + 1];
    static const char* names[MAX_ENTRIES + 1] = {
        "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "t9",
        "t10", "t11", "t12", "t13", "t14", "t15", "t16"};
    for (size_t i = 0; i < MAX_ENTRIES; i++) CHECK(watch(names[i], &c[i], 1000));
    CHECK_FALSE(watch(names[MAX_ENTRIES], &c[MAX_ENTRIES], 1000));
    CHECK(count() == MAX_ENTRIES);

    unwatch("t7");
    CHECK(watch(names[MAX_ENTRIES], &c[MAX_ENTRIES], 1000));
    CHECK(find("t16") != nullptr);
}

TEST_CASE("re-registering resets the watcher's bookkeeping") {
    Clean   clean;
    Counter c{0};
    REQUIRE(watch("again", &c, 1000));
    Entry* e = find("again");
    REQUIRE(e != nullptr);
    CHECK(check(*e, 0, 1000) == Event::NONE);
    CHECK(check(*e, 1000, 1000) == Event::STALLED);
    CHECK(e->stalled);

    unwatch("again");
    REQUIRE(watch("again", &c, 1000));
    e = find("again");
    REQUIRE(e != nullptr);
    CHECK_FALSE(e->stalled);
    CHECK_FALSE(e->seen);
    CHECK(check(*e, 5000, 1000) == Event::NONE);       // a fresh baseline, not a stall
}
