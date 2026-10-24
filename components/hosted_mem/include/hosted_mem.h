/**
 * @file hosted_mem.h
 * @brief Where esp_hosted keeps the frames it copies, and a count of the ones it lost.
 *
 * esp_hosted 3.0.7 copies every frame that crosses the SDIO link into a buffer of its own:
 * each received frame while it de-aggregates an SDIO read, and each frame lwIP sends before
 * it is packed into the next aggregate. Both copies come from eh_host_port_dma_alloc(),
 * which is heap_caps_malloc(n, MALLOC_CAP_DMA) — internal SRAM, the one resource this board
 * is short of. When that allocation fails the frame is gone: on the receive path without a
 * log line or a counter, on the send path as ESP_ERR_NO_MEM to lwIP, which does not resend
 * an ACK or a ping reply. hosted_mem.c moves those copies to PSRAM and counts what it could
 * not place. The evidence and the reasoning are in docs/NET_LOSS_2026-09-23.md.
 *
 * All of this is ESP32-P4 + ESP-Hosted only. Elsewhere the functions exist and report an
 * inactive component, so callers need no #if of their own.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     active;            ///< the wrap is linked in (CONFIG_HOSTED_MEM_FRAMES_IN_PSRAM)
    bool     in_psram;          ///< frame copies are routed to PSRAM right now
    uint32_t frames_psram;      ///< frame copies served from PSRAM
    uint32_t frames_internal;   ///< ... from internal DMA RAM (routing off, or PSRAM refused)
    uint32_t frames_failed_rx;  ///< received frames esp_hosted dropped: no buffer for the copy
    uint32_t frames_failed_tx;  ///< outgoing frames dropped the same way (ACKs, ping replies)
    uint32_t heap_failed;       ///< every failed heap allocation in the firmware since boot
    uint32_t heap_failed_dma;   ///< ... of which asked for MALLOC_CAP_DMA
    uint32_t last_failed_size;  ///< the most recent failed allocation
    uint32_t last_failed_caps;
} hosted_mem_stats_t;

/// Snapshot of the counters. Safe from any task; the counters only ever grow.
void hosted_mem_get_stats(hosted_mem_stats_t* out);

/// Route frame copies to PSRAM (true) or back to esp_hosted's own internal DMA allocation
/// (false), at run time. Exists for A/B measurement on the bench (tools/net_probe.py): both
/// kinds of buffer are released with heap_caps_free(), so switching with frames in flight
/// is safe. Does nothing when the component is inactive.
void hosted_mem_set_frames_in_psram(bool on);

#ifdef __cplusplus
}
#endif
