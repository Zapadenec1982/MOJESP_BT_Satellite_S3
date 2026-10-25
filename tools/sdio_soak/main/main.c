/*
 * tools/sdio_soak — soak test of the ESP32-P4 <-> ESP32-C6 SDIO link (ESP-Hosted 3.0.7).
 *
 * What it does (docs/CONCEPT.md, "Network"): connects to Wi-Fi through the C6 with
 * the credentials the player stored in NVS, then pulls one HTTP(S) stream for hours into
 * a PSRAM ring buffer that a "decoder" drains at a fixed bit rate. Every SOAK_REPORT_SEC
 * it prints one greppable line:
 *
 *   SOAK t=01:23:45 rx=123.4MB kbps=318 ring=61% reconn=2 restart=0 stall=0 wifi_drop=0
 *        rssi=-57 dma=380.1K dma_min=372.9K dma_big=335.9K int=401.2K psram=31.5M
 *
 * Counters: reconn = TCP/HTTP reopen after an error, restart = finite file reached EOF,
 * stall = SOAK_STALL_SEC without a byte, wifi_drop = STA disconnect events, dma_min =
 * minimum free DMA-capable SRAM seen since boot (the number the audio design lives on).
 * Reconnects back off 1 s -> 30 s and never give up (radio semantics).
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "soak";

/* ── Wi-Fi ─────────────────────────────────────────────────────────────── */

static EventGroupHandle_t s_wifi_events;
#define WIFI_CONNECTED_BIT BIT0
static esp_netif_t *s_sta_netif;
static volatile uint32_t s_wifi_drops;

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *e = (wifi_event_sta_disconnected_t *)data;
        s_wifi_drops++;
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "wifi disconnected (reason %u) — reconnecting", e ? e->reason : 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got ip " IPSTR, IP2STR(&e->ip_info.ip));
        /* Unbound UDP (SNTP) only works when the hosted netif is the default one
           (docs/NETWORK.md, "WiFiService"); harmless for TCP, kept for parity with the player. */
        esp_netif_set_default_netif(s_sta_netif);
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

/* Credentials: Kconfig override, else the player's NVS ("wifi"/"ssid","pass"). */
static bool load_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    if (CONFIG_SOAK_WIFI_SSID[0] != '\0') {
        strlcpy(ssid, CONFIG_SOAK_WIFI_SSID, ssid_len);
        strlcpy(pass, CONFIG_SOAK_WIFI_PASS, pass_len);
        return true;
    }
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGE(TAG, "NVS namespace 'wifi' not found — flash the player once and join a network, "
                      "or set SOAK_WIFI_SSID/PASS in menuconfig");
        return false;
    }
    size_t l = ssid_len;
    esp_err_t err = nvs_get_str(h, "ssid", ssid, &l);
    l = pass_len;
    if (err == ESP_OK) err = nvs_get_str(h, "pass", pass, &l);
    nvs_close(h);
    if (err != ESP_OK || ssid[0] == '\0') {
        ESP_LOGE(TAG, "no Wi-Fi credentials in NVS (%s)", esp_err_to_name(err));
        return false;
    }
    return true;
}

static bool wifi_start(void)
{
    char ssid[33] = {0}, pass[65] = {0};
    if (!load_credentials(ssid, sizeof(ssid), pass, sizeof(pass))) return false;

    s_wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, pass, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "connecting to '%s' (creds from %s)", ssid,
             CONFIG_SOAK_WIFI_SSID[0] ? "menuconfig" : "player NVS");

    xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    return true;
}

/* ── PSRAM ring buffer (single producer = HTTP task, single consumer = drain task) ── */

typedef struct {
    uint8_t *buf;
    size_t size;
    volatile size_t head;   /* producer writes */
    volatile size_t tail;   /* consumer reads */
} ring_t;

static ring_t s_ring;

static size_t ring_used(const ring_t *r)  { return (r->head + r->size - r->tail) % r->size; }
static size_t ring_free(const ring_t *r)  { return r->size - 1 - ring_used(r); }

static size_t ring_write(ring_t *r, const uint8_t *src, size_t n)
{
    size_t free = ring_free(r);
    if (n > free) n = free;
    size_t first = r->size - r->head;
    if (first > n) first = n;
    memcpy(r->buf + r->head, src, first);
    memcpy(r->buf, src + first, n - first);
    r->head = (r->head + n) % r->size;
    return n;
}

static size_t ring_drop(ring_t *r, size_t n)
{
    size_t used = ring_used(r);
    if (n > used) n = used;
    r->tail = (r->tail + n) % r->size;
    return n;
}

/* ── Statistics ────────────────────────────────────────────────────────── */

static struct {
    uint64_t rx_bytes;
    uint64_t rx_bytes_at_report;
    uint32_t reconnects;
    uint32_t restarts;
    uint32_t stalls;
    uint32_t http_errors;
    uint32_t ring_overflow_bytes;
    uint32_t ring_empty_events;
    int64_t  last_rx_us;
    int64_t  start_us;
    size_t   dma_min;
    size_t   int_min;
    bool     in_stall;
} s;

static void sample_heap_minimums(void)
{
    size_t dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
    size_t in  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (dma < s.dma_min) s.dma_min = dma;
    if (in < s.int_min)  s.int_min = in;
}

static void report(void)
{
    int64_t now = esp_timer_get_time();
    uint32_t sec = (uint32_t)((now - s.start_us) / 1000000);
    uint64_t delta = s.rx_bytes - s.rx_bytes_at_report;
    s.rx_bytes_at_report = s.rx_bytes;
    uint32_t kbps = (uint32_t)(delta * 8 / 1000 / (CONFIG_SOAK_REPORT_SEC ? CONFIG_SOAK_REPORT_SEC : 1));

    /* Reads 0 over esp_hosted 3.0.7: the sta_get_ap_info response omits rssi (host glue)
       and esp_wifi_sta_get_rssi() is not composable by the host RPC layer (msg_id 341).
       Kept so the column lights up after an esp_hosted bump. */
    int rssi = 0;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) rssi = ap.rssi;

    sample_heap_minimums();
    printf("SOAK t=%02u:%02u:%02u rx=%.1fMB kbps=%u ring=%u%% reconn=%u restart=%u stall=%u "
           "http_err=%u wifi_drop=%u rssi=%d dma=%.1fK dma_min=%.1fK dma_big=%.1fK int=%.1fK "
           "int_min=%.1fK psram=%.1fM\n",
           (unsigned)(sec / 3600), (unsigned)((sec / 60) % 60), (unsigned)(sec % 60),
           (double)s.rx_bytes / 1048576.0, (unsigned)kbps,
           (unsigned)(ring_used(&s_ring) * 100 / s_ring.size),
           (unsigned)s.reconnects, (unsigned)s.restarts, (unsigned)s.stalls, (unsigned)s.http_errors,
           (unsigned)s_wifi_drops, rssi,
           heap_caps_get_free_size(MALLOC_CAP_DMA) / 1024.0,
           s.dma_min / 1024.0,
           heap_caps_get_largest_free_block(MALLOC_CAP_DMA) / 1024.0,
           heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024.0,
           s.int_min / 1024.0,
           heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1048576.0);
    fflush(stdout);
}

/* ── DMA-SRAM ballast: the third axis ──────────────────────────────────── */
/*
 * The link is never measured alone on the real device. On the player the same DMA-capable
 * internal pool also holds the LVGL working buffers, the I2S rings and mbedTLS, and what
 * is left over for the ESP-Hosted RX pool is about 27 KB (docs/CONCEPT.md, "Network"). A baseline
 * taken with 440 KB free says nothing about that, so the test can squeeze the pool down to
 * a named figure and hold it there for a phase.
 *
 * Ballast is taken in 4 KB blocks rather than one big one on purpose: it leaves the pool
 * fragmented the way a running application leaves it, which is what the allocator inside
 * esp_hosted actually meets.
 */

#define BALLAST_BLOCK   4096
#define BALLAST_MAX     512

static void *s_ballast[BALLAST_MAX];
static size_t s_ballast_n;

static size_t ballast_free_all(void)
{
    size_t freed = s_ballast_n * BALLAST_BLOCK;
    while (s_ballast_n) heap_caps_free(s_ballast[--s_ballast_n]);
    return freed;
}

/* Allocate until no more than `target_kb` of DMA-capable SRAM is free. 0 disables. */
static size_t ballast_hold(uint32_t target_kb)
{
    ballast_free_all();
    if (!target_kb) return 0;
    const size_t target = (size_t)target_kb * 1024;
    while (s_ballast_n < BALLAST_MAX && heap_caps_get_free_size(MALLOC_CAP_DMA) > target) {
        void *p = heap_caps_malloc(BALLAST_BLOCK, MALLOC_CAP_DMA);
        if (!p) break;                      /* pool exhausted before the target — say so */
        s_ballast[s_ballast_n++] = p;
    }
    return s_ballast_n * BALLAST_BLOCK;
}

/* ── Consumer: drains the ring at the configured bit rate ──────────────── */

static void drain_task(void *arg)
{
    const uint32_t period_ms = 20;
    const size_t per_tick = (size_t)CONFIG_SOAK_DRAIN_KBPS * 1000 / 8 * period_ms / 1000;
    bool primed = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(period_ms));
        if (CONFIG_SOAK_DRAIN_KBPS == 0) {
            ring_drop(&s_ring, ring_used(&s_ring));
            continue;
        }
        /* Start draining once the ring is 25 % full (decoder pre-buffer). */
        if (!primed) {
            if (ring_used(&s_ring) < s_ring.size / 4) continue;
            primed = true;
        }
        if (ring_drop(&s_ring, per_tick) < per_tick) {
            s.ring_empty_events++;
            primed = false;   /* underrun: re-buffer before draining again */
        }
    }
}

/* ── Producer: HTTP(S) pull with endless reconnect ─────────────────────── */

static esp_err_t http_event_cb(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        size_t n = ring_write(&s_ring, evt->data, evt->data_len);
        if (n < (size_t)evt->data_len) s.ring_overflow_bytes += evt->data_len - n;   /* drain too slow — not a link fault */
        s.rx_bytes += evt->data_len;
        s.last_rx_us = esp_timer_get_time();
        if (s.in_stall) {
            s.in_stall = false;
            ESP_LOGI(TAG, "data resumed");
        }
    }
    return ESP_OK;
}

static void pull_task(void *arg)
{
    uint32_t backoff_ms = 1000;
    bool first = true;
    for (;;) {
        xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

        esp_http_client_config_t cfg = {
            .url = CONFIG_SOAK_URL,
            .event_handler = http_event_cb,
            .timeout_ms = CONFIG_SOAK_HTTP_TIMEOUT_MS,
            .buffer_size = 4096,
            .buffer_size_tx = 1024,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .user_agent = "ModESP-Streamer-soak/0.1 (ESP32-P4; esp_hosted 3.0.7)",
            .disable_auto_redirect = false,
            .max_redirection_count = 5,
        };
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) {
            ESP_LOGE(TAG, "http client init failed");
            vTaskDelay(pdMS_TO_TICKS(backoff_ms));
            continue;
        }
        esp_http_client_set_header(client, "Icy-MetaData", "0");

        int64_t t0 = esp_timer_get_time();
        esp_err_t err = esp_http_client_perform(client);   /* returns at EOF, timeout or error */
        int status = esp_http_client_get_status_code(client);
        int64_t dt_ms = (esp_timer_get_time() - t0) / 1000;
        esp_http_client_cleanup(client);

        if (err == ESP_OK && status >= 200 && status < 300) {
            /* Finite file finished (LAN control run) — re-open at once, no backoff. */
            if (!first) s.restarts++;
            ESP_LOGI(TAG, "stream ended (HTTP %d) after %lld ms, %.1f MB total — reopening",
                     status, (long long)dt_ms, (double)s.rx_bytes / 1048576.0);
            backoff_ms = 1000;
        } else {
            s.reconnects++;
            if (err != ESP_OK) s.http_errors++;
            ESP_LOGW(TAG, "stream broke: %s, HTTP %d, after %lld ms — reconnect #%u in %u ms",
                     esp_err_to_name(err), status, (long long)dt_ms, (unsigned)s.reconnects, (unsigned)backoff_ms);
            vTaskDelay(pdMS_TO_TICKS(backoff_ms));
            backoff_ms = backoff_ms < 30000 ? backoff_ms * 2 : 30000;
        }
        first = false;
    }
}

/* ── Phases: hold one free-DMA figure at a time and summarise each ─────── */

#define PHASE_MAX 8

static uint32_t s_phase_target[PHASE_MAX];
static int s_phase_count;

static int parse_targets(const char *csv)
{
    int n = 0;
    const char *p = csv;
    while (*p && n < PHASE_MAX) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;
        s_phase_target[n++] = (uint32_t)strtoul(p, (char **)&p, 10);
    }
    return n;
}

static void phase_task(void *arg)
{
    for (int i = 0;; i = (i + 1) % s_phase_count) {
        size_t held = ballast_hold(s_phase_target[i]);
        /* The minimum is per phase: a figure carried over from a roomier phase would hide
           exactly the pressure this phase exists to apply. */
        s.dma_min = s.int_min = (size_t)-1;
        sample_heap_minimums();
        uint64_t rx0 = s.rx_bytes;
        uint32_t reconn0 = s.reconnects, stall0 = s.stalls, http0 = s.http_errors, drop0 = s_wifi_drops;
        int64_t t0 = esp_timer_get_time();

        printf("SOAK PHASE %d/%d target=%uK ballast=%uK dma_free=%.1fK dma_big=%.1fK\n",
               i + 1, s_phase_count, (unsigned)s_phase_target[i], (unsigned)(held / 1024),
               heap_caps_get_free_size(MALLOC_CAP_DMA) / 1024.0,
               heap_caps_get_largest_free_block(MALLOC_CAP_DMA) / 1024.0);
        fflush(stdout);

        vTaskDelay(pdMS_TO_TICKS((uint32_t)CONFIG_SOAK_PHASE_MIN * 60 * 1000));

        double sec = (esp_timer_get_time() - t0) / 1e6;
        printf("SOAK PHASE END %d/%d target=%uK ballast=%uK kbps_avg=%.0f rx=%.1fMB "
               "reconn=%u stall=%u http_err=%u wifi_drop=%u dma_min=%.1fK int_min=%.1fK\n",
               i + 1, s_phase_count, (unsigned)s_phase_target[i], (unsigned)(held / 1024),
               (double)(s.rx_bytes - rx0) * 8.0 / 1000.0 / (sec > 0 ? sec : 1),
               (double)(s.rx_bytes - rx0) / 1048576.0,
               (unsigned)(s.reconnects - reconn0), (unsigned)(s.stalls - stall0),
               (unsigned)(s.http_errors - http0), (unsigned)(s_wifi_drops - drop0),
               s.dma_min / 1024.0, s.int_min / 1024.0);
        fflush(stdout);
    }
}

/* ── main ──────────────────────────────────────────────────────────────── */

static void log_heap_banner(const char *when)
{
    ESP_LOGI(TAG, "heap %s: internal %u | DMA %u (largest %u) | PSRAM %u", when,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG, "NVS needs erase — refusing (it holds the player's Wi-Fi credentials)");
        return;
    }
    ESP_ERROR_CHECK(ret);

    s_ring.size = (size_t)CONFIG_SOAK_RING_KB * 1024;
    s_ring.buf = heap_caps_malloc(s_ring.size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_ring.buf) {
        ESP_LOGE(TAG, "ring buffer %u KB not allocated in PSRAM", CONFIG_SOAK_RING_KB);
        return;
    }
    s.dma_min = s.int_min = (size_t)-1;
    s.start_us = esp_timer_get_time();
    s.last_rx_us = s.start_us;

    ESP_LOGI(TAG, "SDIO soak: url=%s duration=%d min ring=%d KB drain=%d kbps stall>%d s",
             CONFIG_SOAK_URL, CONFIG_SOAK_DURATION_MIN, CONFIG_SOAK_RING_KB,
             CONFIG_SOAK_DRAIN_KBPS, CONFIG_SOAK_STALL_SEC);
    log_heap_banner("before Wi-Fi");

    if (!wifi_start()) return;
    log_heap_banner("after Wi-Fi");
    sample_heap_minimums();

    /* Both tasks keep their stacks in internal SRAM (nothing here writes flash, but the
       pull task runs mbedTLS — keep it conservative). */
    xTaskCreatePinnedToCore(pull_task,  "soak_pull",  8192, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(drain_task, "soak_drain", 3072, NULL, 4, NULL, 1);

    if (CONFIG_SOAK_PHASE_MIN > 0) {
        s_phase_count = parse_targets(CONFIG_SOAK_BALLAST_TARGETS);
        if (s_phase_count > 0) {
            ESP_LOGI(TAG, "phases: %d x %d min, free-DMA targets '%s'",
                     s_phase_count, CONFIG_SOAK_PHASE_MIN, CONFIG_SOAK_BALLAST_TARGETS);
            xTaskCreatePinnedToCore(phase_task, "soak_phase", 4096, NULL, 3, NULL, 1);
        }
    }

    const int64_t end_us = CONFIG_SOAK_DURATION_MIN > 0
                         ? s.start_us + (int64_t)CONFIG_SOAK_DURATION_MIN * 60 * 1000000 : 0;
    bool finished = false;
    for (;;) {
        for (int i = 0; i < CONFIG_SOAK_REPORT_SEC; i++) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            sample_heap_minimums();
            int64_t now = esp_timer_get_time();
            if (!s.in_stall && s.rx_bytes > 0 &&
                (now - s.last_rx_us) > (int64_t)CONFIG_SOAK_STALL_SEC * 1000000) {
                s.in_stall = true;
                s.stalls++;
                ESP_LOGW(TAG, "STALL #%u: no data for %d s (ring %u%%)", (unsigned)s.stalls,
                         CONFIG_SOAK_STALL_SEC, (unsigned)(ring_used(&s_ring) * 100 / s_ring.size));
            }
        }
        report();
        if (end_us && !finished && esp_timer_get_time() >= end_us) {
            finished = true;
            printf("SOAK DONE after %d min: rx=%.1fMB reconn=%u restart=%u stall=%u http_err=%u "
                   "wifi_drop=%u ring_underruns=%u ring_overflow=%uB dma_min=%.1fK int_min=%.1fK\n",
                   CONFIG_SOAK_DURATION_MIN, (double)s.rx_bytes / 1048576.0, (unsigned)s.reconnects,
                   (unsigned)s.restarts, (unsigned)s.stalls, (unsigned)s.http_errors, (unsigned)s_wifi_drops,
                   (unsigned)s.ring_empty_events, (unsigned)s.ring_overflow_bytes, s.dma_min / 1024.0, s.int_min / 1024.0);
            fflush(stdout);
            /* Keep pulling and reporting — the overnight run just gets longer. */
        }
    }
}
