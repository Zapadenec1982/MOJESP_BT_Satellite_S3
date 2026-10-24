/**
 * @file test_net_probe_stats.cpp
 * @brief The UDP probe's accounting (components/net_probe/include/net_probe/probe_stats.h).
 *
 * The probe exists to decide whether an AirPlay receiver can live behind the SDIO link, and it
 * decides that from three numbers: packets lost, the longest run of them, the longest silence.
 * A counter that confused a late packet with a lost one, or a duplicate with a late one, would
 * move the verdict, so each case is pinned here with the exact figure it must produce.
 */

#include "doctest.h"
#include "net_probe/probe_stats.h"

#include <initializer_list>

namespace {

constexpr int64_t kPeriodUs = 7982;   // 352 frames at 44.1 kHz: one AirPlay 1 packet

// Feed seqs with arrivals one packet period apart and a constant transit time.
void feed(probe_stats_t& s, std::initializer_list<uint32_t> seqs, int64_t& t)
{
    for (uint32_t q : seqs) {
        probe_stats_on_packet(&s, q, (uint64_t)t, t + 1500);
        t += kPeriodUs;
    }
}

}  // namespace

TEST_CASE("net_probe: an unbroken stream loses nothing") {
    probe_stats_t s;
    probe_stats_reset(&s, 7);
    int64_t t = 1000000;
    for (uint32_t q = 0; q < 500; ++q) {
        probe_stats_on_packet(&s, q, (uint64_t)t, t + 1500);
        t += kPeriodUs;
    }
    CHECK(s.session == 7);
    CHECK(s.received == 500);
    CHECK(probe_stats_lost(&s) == 0);
    CHECK(s.max_run == 0);
    CHECK(s.duplicates == 0);
    CHECK(s.late == 0);
    CHECK(s.stale == 0);
    CHECK(s.max_gap_us == kPeriodUs);
    CHECK(probe_stats_jitter_us(&s) == 0);   // constant transit: no jitter at all
}

TEST_CASE("net_probe: a hole counts every missing packet and its length as the run") {
    probe_stats_t s;
    probe_stats_reset(&s, 1);
    int64_t t = 0;
    feed(s, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}, t);
    t += 5 * kPeriodUs;                       // 10..14 never arrive
    feed(s, {15, 16, 17}, t);
    CHECK(s.received == 13);
    CHECK(probe_stats_lost(&s) == 5);
    CHECK(s.max_run == 5);
    CHECK(s.max_gap_us == 6 * kPeriodUs);     // the silence the hole left
}

TEST_CASE("net_probe: a late packet closes its hole but the run stays as seen") {
    probe_stats_t s;
    probe_stats_reset(&s, 1);
    int64_t t = 0;
    feed(s, {0, 1, 2, 4, 3, 5}, t);
    CHECK(s.received == 6);
    CHECK(s.late == 1);
    CHECK(probe_stats_lost(&s) == 0);
    CHECK(s.max_run == 1);
    CHECK(s.duplicates == 0);
}

TEST_CASE("net_probe: a duplicate is counted once and does not inflate what arrived") {
    probe_stats_t s;
    probe_stats_reset(&s, 1);
    int64_t t = 0;
    feed(s, {0, 1, 2, 2, 3, 1}, t);
    CHECK(s.received == 4);
    CHECK(s.duplicates == 2);
    CHECK(s.late == 0);
    CHECK(probe_stats_lost(&s) == 0);
}

TEST_CASE("net_probe: older than the window is stale, inside it is still a duplicate") {
    probe_stats_t s;
    probe_stats_reset(&s, 1);
    int64_t t = 0;
    for (uint32_t q = 0; q < 2048; ++q) {
        probe_stats_on_packet(&s, q, (uint64_t)t, t);
        t += kPeriodUs;
    }
    probe_stats_on_packet(&s, 2047 - (PROBE_WINDOW - 1), (uint64_t)t, t);   // oldest remembered
    CHECK(s.duplicates == 1);
    probe_stats_on_packet(&s, 2047 - PROBE_WINDOW, (uint64_t)t, t);         // just past it
    CHECK(s.stale == 1);
    CHECK(s.received == 2048);
}

TEST_CASE("net_probe: a jump longer than the window forgets the old marks") {
    probe_stats_t s;
    probe_stats_reset(&s, 1);
    int64_t t = 0;
    feed(s, {0, 1, 2}, t);
    probe_stats_on_packet(&s, 5000, (uint64_t)t, t);
    CHECK(s.max_run == 4997);
    CHECK(probe_stats_lost(&s) == 4997);
    // 5000 - 3000 = 2000 >= window: too old to judge, whatever the bitmap held for slot 3000
    probe_stats_on_packet(&s, 3000, (uint64_t)t, t);
    CHECK(s.stale == 1);
    // 4990 is inside the window and was never received: a late fill, not a duplicate
    probe_stats_on_packet(&s, 4990, (uint64_t)t, t);
    CHECK(s.late == 1);
    CHECK(s.duplicates == 0);
}

TEST_CASE("net_probe: a session that starts with a reordering grows downwards") {
    probe_stats_t s;
    probe_stats_reset(&s, 1);
    int64_t t = 0;
    feed(s, {2, 3, 0, 4}, t);
    CHECK(s.first == 0);
    CHECK(s.received == 4);
    CHECK(probe_stats_lost(&s) == 1);   // 1 never came
    CHECK(s.late == 1);
}

TEST_CASE("net_probe: jitter follows RFC 3550 and ignores the clock offset") {
    probe_stats_t s;
    probe_stats_reset(&s, 1);
    // A sender clock 123456789 us away from ours, and a transit that alternates 1 ms / 3 ms:
    // |D| is 2000 us every time, so J converges on 2000 us from below.
    const int64_t offset = 123456789;
    int64_t t = 0;
    for (int i = 0; i < 400; ++i) {
        const int64_t transit = (i & 1) ? 3000 : 1000;
        probe_stats_on_packet(&s, (uint32_t)i, (uint64_t)(t + offset), t + offset + transit);
        t += kPeriodUs;
    }
    CHECK(probe_stats_jitter_us(&s) >= 1990);
    CHECK(probe_stats_jitter_us(&s) <= 2000);
}

TEST_CASE("net_probe: a reset starts a clean session") {
    probe_stats_t s;
    probe_stats_reset(&s, 1);
    int64_t t = 0;
    feed(s, {0, 2, 2}, t);
    probe_stats_reset(&s, 9);
    CHECK(s.session == 9);
    CHECK(s.received == 0);
    CHECK(probe_stats_lost(&s) == 0);
    CHECK(s.duplicates == 0);
    feed(s, {100, 101}, t);
    CHECK(s.first == 100);
    CHECK(s.received == 2);
    CHECK(probe_stats_lost(&s) == 0);
}
