#include "doctest.h"
#include "bt_clock_health.h"
#include <initializer_list>

TEST_CASE("BT exhausted clock correction leaves a draining queue at every format rate") {
    for (unsigned hz : {44100u, 48000u, 96000u}) {
        audio_core::BtClockHealth health;
        for (unsigned n = 0; n < 31; ++n)
            CHECK_FALSE(health.observe(hz * (99 - n) / 1000, hz, -99, 250));
        CHECK(health.observe(hz * 68 / 1000, hz, -99, 250));
    }
}
TEST_CASE("BT healthy or briefly saturated queues keep clock following") {
    audio_core::BtClockHealth health;
    for (unsigned n = 0; n < 100; ++n)
        CHECK_FALSE(health.observe(4400, 48000, -99, 250));
    health.reset(); // a new announced format is a new observation session
    for (unsigned n = 0; n < 100; ++n)
        CHECK_FALSE(health.observe(4000 + (n % 2 ? 0 : 240), 44100, -99, 250));
    health.reset();
    for (unsigned n = 0; n < 100; ++n)
        CHECK_FALSE(health.observe(4800 - n, 48000, n % 2 ? -99 : -80, 250));
}
TEST_CASE("BT repeated starvation is remembered across priming until a new session") {
    audio_core::BtClockHealth health;
    CHECK_FALSE(health.underrun());
    CHECK(health.underrun());
    health.reset();
    CHECK_FALSE(health.underrun());
}
