/**
 * @file probe_stats.h
 * @brief What the UDP probe counts, kept free of IDF so tests/host can pin it.
 *
 * The questions it answers are the ones that decide whether an AirPlay 1 receiver can live
 * behind this link: how many packets never came, how many of
 * the missing ones came in a row, and the longest stretch with nothing at all — a receiver
 * with a 2 s buffer survives scattered loss by asking again, and dies of a long hole.
 *
 *  lost        holes still open between the first and the highest sequence number seen.
 *              A late packet closes its hole, so this is the loss as of now, not ever.
 *  max_run     the longest run of consecutive missing numbers, measured when the jump is
 *              seen (a later late arrival does not shrink it).
 *  max_gap_us  the longest time between two arrivals, whatever their numbers.
 *  jitter      RFC 3550 §6.4.1 interarrival jitter over (arrival - send) transit times, so
 *              the two clocks' offset cancels; kept x16 as the RFC's integer form suggests.
 *  duplicates / late / stale — told apart by a bitmap of the last PROBE_WINDOW numbers;
 *              anything older than that is "stale", counted, and otherwise ignored.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define PROBE_WINDOW 1024u   ///< sequence numbers remembered: 8 s at 125 packets/s

typedef struct {
    uint32_t session;
    bool     started;
    uint32_t first;
    uint32_t highest;
    uint32_t received;
    uint32_t duplicates;
    uint32_t late;
    uint32_t stale;
    uint32_t max_run;
    int64_t  last_arrival_us;
    uint32_t max_gap_us;
    bool     have_transit;
    int64_t  prev_transit_us;
    uint32_t jitter_x16;
    uint8_t  seen[PROBE_WINDOW / 8];
} probe_stats_t;

static inline void probe_stats_reset(probe_stats_t* s, uint32_t session)
{
    memset(s, 0, sizeof(*s));
    s->session = session;
}

static inline bool probe_seen_get(const probe_stats_t* s, uint32_t seq)
{
    const uint32_t i = seq % PROBE_WINDOW;
    return (s->seen[i >> 3] >> (i & 7u)) & 1u;
}

static inline void probe_seen_set(probe_stats_t* s, uint32_t seq, bool on)
{
    const uint32_t i = seq % PROBE_WINDOW;
    if (on) s->seen[i >> 3] |= (uint8_t)(1u << (i & 7u));
    else    s->seen[i >> 3] &= (uint8_t)~(1u << (i & 7u));
}

static inline uint32_t probe_stats_lost(const probe_stats_t* s)
{
    if (!s->started) return 0;
    const uint32_t span = s->highest - s->first + 1u;
    return span > s->received ? span - s->received : 0u;
}

static inline uint32_t probe_stats_jitter_us(const probe_stats_t* s)
{
    return s->jitter_x16 >> 4;
}

/// One DATA packet: its sequence number, the sender's timestamp, and when it arrived here.
static inline void probe_stats_on_packet(probe_stats_t* s, uint32_t seq, uint64_t sent_us,
                                         int64_t arrived_us)
{
    // Transit time in the sender's clock minus ours: the offset between the clocks is the
    // same in every sample, so only the differences mean anything, and those are the jitter.
    const int64_t transit = arrived_us - (int64_t)sent_us;
    if (s->have_transit) {
        int64_t d = transit - s->prev_transit_us;
        if (d < 0) d = -d;
        // J += (|D| - J) / 16, in J*16 units: J16 += |D| - J16/16. Never goes negative.
        const int64_t j16 = (int64_t)s->jitter_x16 + d - (int64_t)(s->jitter_x16 >> 4);
        s->jitter_x16 = j16 > (int64_t)UINT32_MAX ? UINT32_MAX : (uint32_t)j16;
    }
    s->prev_transit_us = transit;
    s->have_transit = true;

    if (!s->started) {
        s->started = true;
        s->first = seq;
        s->highest = seq;
        s->received = 1;
        s->last_arrival_us = arrived_us;
        probe_seen_set(s, seq, true);
        return;
    }

    const int64_t gap = arrived_us - s->last_arrival_us;
    if (gap > (int64_t)s->max_gap_us) {
        s->max_gap_us = gap > (int64_t)UINT32_MAX ? UINT32_MAX : (uint32_t)gap;
    }
    s->last_arrival_us = arrived_us;

    if ((int32_t)(seq - s->highest) > 0) {
        // Newer than anything so far. Everything skipped over is a hole; the window slides,
        // so the slots it slides onto are cleared before they are reused.
        const uint32_t advance = seq - s->highest;
        if (advance - 1u > s->max_run) s->max_run = advance - 1u;
        if (advance >= PROBE_WINDOW) {
            memset(s->seen, 0, sizeof(s->seen));
        } else {
            for (uint32_t k = 1; k <= advance; ++k) probe_seen_set(s, s->highest + k, false);
        }
        s->highest = seq;
        probe_seen_set(s, seq, true);
        s->received++;
        return;
    }

    // Not newer: a duplicate, a late packet filling a hole, or too old to tell.
    const uint32_t age = s->highest - seq;
    if (age >= PROBE_WINDOW) {
        s->stale++;
    } else if ((int32_t)(seq - s->first) < 0) {
        // Older than the first packet this session saw, yet inside the window: the session
        // began with a reordering. The range grows downwards, and anything between this one
        // and the old first is a hole like any other.
        s->first = seq;
        probe_seen_set(s, seq, true);
        s->received++;
        s->late++;
    } else if (probe_seen_get(s, seq)) {
        s->duplicates++;
    } else {
        probe_seen_set(s, seq, true);
        s->received++;
        s->late++;
    }
}
