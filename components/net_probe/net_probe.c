/*
 * net_probe.c — the panel end of tools/net_probe.py. See net_probe.h and
 * docs/NET_LOSS_2026-09-23.md.
 *
 * One task, one UDP socket. It counts DATA packets, echoes the ones the PC flags, obeys
 * CTRL commands and sends a REPORT once a second to whoever spoke last. It does no audio
 * work: the point is to see what reaches the application through esp_hosted and lwIP, and
 * what memory that costs, while the rest of the firmware runs as it always does.
 */

#include "net_probe.h"

#include "sdkconfig.h"

#if CONFIG_NET_PROBE_ENABLE

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "hosted_mem.h"
#include "net_probe/probe_stats.h"
#include "net_probe/probe_wire.h"

static const char* TAG = "NetProbe";

#define PROBE_BUF_BYTES    1600
#define PROBE_REPORT_US    1000000LL
#define PROBE_RECV_TIMEOUT 100   // ms: short enough that reports keep their one-second beat

static probe_stats_t s_stats;    // ~200 B; .bss, touched only by the probe task

static void fill_report(probe_report_t* r)
{
    memset(r, 0, sizeof(*r));
    r->received   = s_stats.received;
    r->duplicates = s_stats.duplicates;
    r->late       = s_stats.late;
    r->stale      = s_stats.stale;
    r->lost       = probe_stats_lost(&s_stats);
    r->highest    = s_stats.highest;
    r->max_run    = s_stats.max_run;
    r->max_gap_us = s_stats.max_gap_us;
    r->jitter_us  = probe_stats_jitter_us(&s_stats);

    r->dma_free      = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_DMA);
    r->dma_min       = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_DMA);
    r->dma_largest   = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
    r->internal_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    hosted_mem_stats_t hm;
    hosted_mem_get_stats(&hm);
    r->frames_psram      = hm.frames_psram;
    r->frames_internal   = hm.frames_internal;
    r->frames_failed_rx  = hm.frames_failed_rx;
    r->frames_failed_tx  = hm.frames_failed_tx;
    r->heap_failed       = hm.heap_failed;
    r->heap_failed_dma   = hm.heap_failed_dma;
    r->last_failed_size  = hm.last_failed_size;
    r->last_failed_caps  = hm.last_failed_caps;
    r->hosted_mem_active = hm.active ? 1 : 0;
    r->frames_in_psram   = hm.in_psram ? 1 : 0;
    r->uptime_s          = (uint32_t)(esp_timer_get_time() / 1000000LL);
}

static void send_report(int sock, const struct sockaddr_in* peer, uint32_t report_no)
{
    uint8_t out[sizeof(probe_hdr_t) + sizeof(probe_report_t)];
    probe_hdr_t h = {
        .magic = PROBE_MAGIC, .type = PROBE_REPORT, .flags = 0,
        .hdr_len = sizeof(probe_hdr_t), .session = s_stats.session, .seq = report_no,
        .t_us = (uint64_t)esp_timer_get_time(),
    };
    probe_report_t r;
    fill_report(&r);
    memcpy(out, &h, sizeof(h));
    memcpy(out + sizeof(h), &r, sizeof(r));
    sendto(sock, out, sizeof(out), 0, (const struct sockaddr*)peer, sizeof(*peer));
}

static void probe_task(void* arg)
{
    (void)arg;
    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket() failed: errno %d", errno);
        vTaskDeleteWithCaps(NULL);
        return;
    }
    struct sockaddr_in local = {
        .sin_family = AF_INET,
        .sin_port = htons(CONFIG_NET_PROBE_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr*)&local, sizeof(local)) != 0) {
        ESP_LOGE(TAG, "bind(%d) failed: errno %d", CONFIG_NET_PROBE_PORT, errno);
        close(sock);
        vTaskDeleteWithCaps(NULL);
        return;
    }
    struct timeval tv = { .tv_sec = 0, .tv_usec = PROBE_RECV_TIMEOUT * 1000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // PSRAM: this buffer is the application's, not the transport's, and the whole exercise is
    // about not taking internal memory the link itself needs.
    uint8_t* buf = heap_caps_malloc(PROBE_BUF_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE(TAG, "no buffer");
        close(sock);
        vTaskDeleteWithCaps(NULL);
        return;
    }

    probe_stats_reset(&s_stats, 0);
    // A test image says what it is at the level the shipping policy shows (WARN).
    ESP_LOGW(TAG, "test image: listening on UDP %d (tools/net_probe.py)", CONFIG_NET_PROBE_PORT);

    struct sockaddr_in peer = {0};
    bool have_peer = false;
    uint32_t report_no = 0;
    int64_t next_report = 0;

    for (;;) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        const int n = recvfrom(sock, buf, PROBE_BUF_BYTES, 0, (struct sockaddr*)&from, &from_len);
        const int64_t now = esp_timer_get_time();

        if (n >= (int)sizeof(probe_hdr_t)) {
            probe_hdr_t h;
            memcpy(&h, buf, sizeof(h));
            if (h.magic == PROBE_MAGIC) {
                peer = from;
                have_peer = true;
                if (h.type == PROBE_DATA && h.session == s_stats.session) {
                    probe_stats_on_packet(&s_stats, h.seq, h.t_us, now);
                    if (h.flags & PROBE_F_ECHO) {
                        h.type = PROBE_ECHO;
                        sendto(sock, &h, sizeof(h), 0, (struct sockaddr*)&from, from_len);
                    }
                } else if (h.type == PROBE_CTRL &&
                           n >= (int)(sizeof(probe_hdr_t) + sizeof(probe_ctrl_t))) {
                    probe_ctrl_t c;
                    memcpy(&c, buf + sizeof(probe_hdr_t), sizeof(c));
                    switch (c.cmd) {
                    case PROBE_CMD_START:
                        probe_stats_reset(&s_stats, h.session);
                        report_no = 0;
                        next_report = 0;          // report at once: the PC waits for it
                        break;
                    case PROBE_CMD_SET_PSRAM:
                        hosted_mem_set_frames_in_psram(c.arg != 0);
                        next_report = 0;
                        break;
                    case PROBE_CMD_REPORT_NOW:
                        next_report = 0;
                        break;
                    default:
                        break;
                    }
                }
            }
        }

        if (have_peer && now >= next_report) {
            send_report(sock, &peer, report_no++);
            next_report = now + PROBE_REPORT_US;
        }
    }
}

void net_probe_start(void)
{
    static bool started = false;
    if (started) return;
    started = true;
    // Core 0 with the network stack and the other producers, priority 5 — above the decoders
    // (1) and far below the audio tasks (22/23) and esp_hosted (21), which is roughly where an
    // AirPlay receiver's socket task would sit. Stack in PSRAM: it never writes flash.
    if (xTaskCreatePinnedToCoreWithCaps(probe_task, "net_probe", 4096, NULL, 5, NULL, 0,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGE(TAG, "task create failed");
        started = false;
    }
}

#else  // !CONFIG_NET_PROBE_ENABLE

void net_probe_start(void) {}

#endif
