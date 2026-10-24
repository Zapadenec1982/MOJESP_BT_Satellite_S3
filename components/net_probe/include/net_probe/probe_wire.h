/**
 * @file probe_wire.h
 * @brief The UDP probe's packets. tools/net_probe.py is the other end and repeats these
 *        layouts with struct.pack('<...'); tools/tests/test_net_probe_wire.py keeps the two
 *        in step. Little-endian on both sides (RISC-V and x86).
 *
 * PC -> panel   DATA    header + filler, the shape of an AirPlay 1 audio packet: 352 stereo
 *                       16-bit frames (1408 bytes of audio) every 7.98 ms. flags bit 0 asks
 *                       for an ECHO back, which is how the PC times the small upstream
 *                       traffic a receiver sends (resend requests, timing requests).
 * PC -> panel   CTRL    header + probe_ctrl_t: start a session, switch where esp_hosted's
 *                       frame copies go, ask for a report now.
 * panel -> PC   REPORT  header + probe_report_t, once a second to whoever sent last.
 * panel -> PC   ECHO    the DATA header sent back unchanged apart from its type.
 */
#pragma once

#include <stdint.h>

#define PROBE_MAGIC 0x504E3450u   ///< "P4NP" as bytes on the wire

enum {
    PROBE_DATA   = 1,
    PROBE_CTRL   = 2,
    PROBE_REPORT = 3,
    PROBE_ECHO   = 4,
};

enum { PROBE_F_ECHO = 0x01 };

enum {
    PROBE_CMD_START      = 1,  ///< new session: counters to zero, session id from the header
    PROBE_CMD_SET_PSRAM  = 2,  ///< arg 1/0: esp_hosted frame copies to PSRAM / back to internal
    PROBE_CMD_REPORT_NOW = 3,
};

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  type;
    uint8_t  flags;
    uint16_t hdr_len;   ///< sizeof(probe_hdr_t) as the sender knew it
    uint32_t session;
    uint32_t seq;
    uint64_t t_us;      ///< sender's clock; the panel's own in a REPORT
} probe_hdr_t;

typedef struct __attribute__((packed)) {
    uint8_t cmd;
    uint8_t arg;
} probe_ctrl_t;

typedef struct __attribute__((packed)) {
    // What arrived in this session (probe_stats.h)
    uint32_t received;          ///< unique DATA packets
    uint32_t duplicates;
    uint32_t late;              ///< arrived after a higher sequence number, filling a hole
    uint32_t stale;             ///< too old to tell a duplicate from a late packet
    uint32_t lost;              ///< holes still open between the first and highest seq
    uint32_t highest;
    uint32_t max_run;           ///< longest run of consecutive missing packets
    uint32_t max_gap_us;        ///< longest time without any DATA packet
    uint32_t jitter_us;         ///< RFC 3550 interarrival jitter
    // Memory, now
    uint32_t dma_free;          ///< heap_caps_get_free_size(MALLOC_CAP_DMA)
    uint32_t dma_min;           ///< heap_caps_get_minimum_free_size(MALLOC_CAP_DMA), since boot
    uint32_t dma_largest;
    uint32_t internal_free;
    // esp_hosted frame copies (hosted_mem.h)
    uint32_t frames_psram;
    uint32_t frames_internal;
    uint32_t frames_failed_rx;
    uint32_t frames_failed_tx;
    uint32_t heap_failed;
    uint32_t heap_failed_dma;
    uint32_t last_failed_size;
    uint32_t last_failed_caps;
    uint8_t  hosted_mem_active;
    uint8_t  frames_in_psram;
    uint16_t reserved;
    uint32_t uptime_s;
} probe_report_t;

#ifdef __cplusplus
#define PROBE_WIRE_ASSERT static_assert
#else
#define PROBE_WIRE_ASSERT _Static_assert
#endif
PROBE_WIRE_ASSERT(sizeof(probe_hdr_t) == 24, "tools/net_probe.py packs a 24-byte header");
PROBE_WIRE_ASSERT(sizeof(probe_ctrl_t) == 2, "tools/net_probe.py packs a 2-byte command");
PROBE_WIRE_ASSERT(sizeof(probe_report_t) == 92, "tools/net_probe.py unpacks a 92-byte report");
