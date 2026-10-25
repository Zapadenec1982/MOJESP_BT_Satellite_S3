/**
 * @file cover_bytes.cpp
 * @brief Artwork kept as the file that arrived — cover.h, for a build without a display
 *        (CONFIG_COVER_DECODE_TILE=n, the ESP32-S3 board).
 *
 * The P4 decodes every cover into a 280-pixel tile because its panel draws one; cover.cpp is
 * that. A board with no display has no use for the tile, and its chip has neither the JPEG
 * engine nor the PPA that made the tile cheap: the software decoder would cost seconds of a
 * core and up to 12 MB of PSRAM per picture (cover_guard.h), on a board with 8 MB. The one
 * reader left is the browser, which decodes JPEG and PNG itself and crops with CSS.
 *
 * So the picture is kept as its bytes, reference-counted: GET /api/cover holds a reference while
 * it sends, and a producer publishing the next track's cover meanwhile only drops the store's
 * own. The colour for the web's backdrop (ui.cover_rgb) is the same cover_tone() the panel uses,
 * run over a tile filled from an eighth-scale decode by the ROM's TJpgDec: baseline JPEG only,
 * a few kilobytes of work area, the DC coefficients alone at that scale. A PNG or a progressive
 * JPEG gets no colour, and the web shows its plain surface, as it does for no picture at all.
 *
 * The producers do not change. Each hands over a buffer it frees right after the call, so the
 * bytes are copied once here; the generation rules (cancel on clear, publish only at the expected
 * generation) are cover.cpp's.
 */

#include "cover.h"
#include "sdkconfig.h"

#if !CONFIG_COVER_DECODE_TILE

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_rom_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#if ESP_ROM_HAS_JPEG_DECODE
#include "rom/tjpgd.h"
#endif

#include <atomic>
#include <cstring>
#include <new>

namespace player_core {
namespace {

const char* TAG = "Cover";

/// The largest file kept. Covers that arrive by AirPlay and Bluetooth are capped at 256 KB by
/// their senders, DLNA art at 512 KB by its fetch buffer; the player reads cover.jpg files up to
/// 1 MB, and one that large is a scan, not artwork — keeping five of them (one per source) would
/// be 5 MB of an 8 MB PSRAM. Bigger files are refused like a broken one: the player then tries
/// the folder's next candidate, the radio its station logo.
constexpr size_t   MAX_BYTES = 512 * 1024;
/// What the header may claim. A browser decodes anything, but a 16k x 16k "cover" is a
/// decompression bomb for the phone that shows it.
constexpr uint32_t MAX_SIDE  = 4096;

bool is_png(const uint8_t* p, size_t len) {
    return len >= 8 && !memcmp(p, "\x89PNG\r\n\x1a\n", 8);
}

/// PNG: the IHDR chunk is first, its width and height big-endian at 16 and 20.
bool png_size(const uint8_t* p, size_t len, uint32_t& w, uint32_t& h) {
    if (len < 24 || memcmp(p + 12, "IHDR", 4) != 0) return false;
    w = (uint32_t)p[16] << 24 | (uint32_t)p[17] << 16 | (uint32_t)p[18] << 8 | p[19];
    h = (uint32_t)p[20] << 24 | (uint32_t)p[21] << 16 | (uint32_t)p[22] << 8 | p[23];
    return true;
}

/// JPEG: walk the markers to the frame header. Baseline, extended and progressive (C0-C2) are
/// what browsers decode; lossless and arithmetic-coded files are not artwork anyone ships.
bool jpeg_size(const uint8_t* p, size_t len, uint32_t& w, uint32_t& h, bool& progressive) {
    if (len < 4 || p[0] != 0xFF || p[1] != 0xD8) return false;
    size_t i = 2;
    while (i + 3 < len) {
        if (p[i] != 0xFF) { ++i; continue; }          // resync over fill bytes
        const uint8_t m = p[i + 1];
        if (m == 0xFF) { ++i; continue; }
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { i += 2; continue; }
        if (m == 0xDA || m == 0xD9) return false;      // image data began, no frame header seen
        if (m == 0xC0 || m == 0xC1 || m == 0xC2) {
            if (i + 8 >= len) return false;
            h = (uint32_t)p[i + 5] << 8 | p[i + 6];
            w = (uint32_t)p[i + 7] << 8 | p[i + 8];
            progressive = m == 0xC2;
            return true;
        }
        if ((m >= 0xC3 && m <= 0xCF) && m != 0xC4 && m != 0xC8 && m != 0xCC) return false;
        const size_t seg = ((size_t)p[i + 2] << 8) | p[i + 3];
        if (seg < 2) return false;
        i += 2 + seg;
    }
    return false;
}

/// One published file. The data follows the header in the same allocation.
struct Blob {
    std::atomic<int32_t> refs{1};
    size_t len = 0;
    bool   png = false;
    uint8_t*       data()       { return reinterpret_cast<uint8_t*>(this + 1); }
    const uint8_t* data() const { return reinterpret_cast<const uint8_t*>(this + 1); }
};

Blob* blob_new(const uint8_t* p, size_t len, bool png) {
    void* m = heap_caps_malloc(sizeof(Blob) + len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!m) return nullptr;
    Blob* b = new (m) Blob;
    b->len = len;
    b->png = png;
    memcpy(b->data(), p, len);
    return b;
}

/// Never under the spinlock: the last reference frees.
void blob_unref(Blob* b) {
    if (b && b->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        b->~Blob();
        heap_caps_free(b);
    }
}

struct Store {
    Blob*                 blob = nullptr;   ///< under publication_mux
    std::atomic<uint32_t> gen{0};
    uint32_t              rgb = 0;
};

Store stores[COVER_SOURCES];
portMUX_TYPE publication_mux = portMUX_INITIALIZER_UNLOCKED;

/// Generations start where no earlier boot's did. ui.cover and GET /api/cover's ETag are made of
/// (generation, source) alone, so with every boot counting from 0 the first cover after a restart
/// carried the id of the first cover before it: a browser holding that file answered the route's
/// ETag with a 304 and kept the old album, and a page left open did not fetch at all. 24 bits of
/// the hardware RNG per source, far below UINT32_MAX ("no expected generation").
struct GenSeed {
    GenSeed() {
        for (Store& g : stores) g.gen.store(esp_random() & 0x00FFFFFFu, std::memory_order_relaxed);
    }
} s_gen_seed;

/// Producers one at a time, as in cover.cpp: it bounds what the colour's decode holds at once
/// (a tile and a work area) to one picture's worth however many sources publish together.
SemaphoreHandle_t producer_mutex() {
    static StaticSemaphore_t storage;
    static SemaphoreHandle_t mutex = xSemaphoreCreateMutexStatic(&storage);
    return mutex;
}
struct ProducerLock {
    ProducerLock() { xSemaphoreTake(producer_mutex(), portMAX_DELAY); }
    ~ProducerLock() { xSemaphoreGive(producer_mutex()); }
};

#if ESP_ROM_HAS_JPEG_DECODE
/// The colour, from the ROM decoder: the centred square of an eighth-scale (or larger) decode,
/// nearest-sampled into a COVER_PX tile, then cover_tone() exactly as on the panel.
struct ToneJob {
    const uint8_t* src;
    size_t         len;
    size_t         pos;
    uint16_t*      tile;
    uint32_t       side, ox, oy;   ///< the square, in decoded (scaled) pixels
};

UINT tone_in(JDEC* jd, BYTE* buf, UINT n) {
    auto* j = static_cast<ToneJob*>(jd->device);
    const size_t left = j->len - j->pos;
    if (n > left) n = (UINT)left;
    if (buf) memcpy(buf, j->src + j->pos, n);
    j->pos += n;
    return n;
}

UINT tone_out(JDEC* jd, void* bitmap, JRECT* r) {
    auto* j = static_cast<ToneJob*>(jd->device);
    const uint8_t* p = static_cast<const uint8_t*>(bitmap);   // RGB888, the rectangle row by row
    for (uint32_t y = r->top; y <= r->bottom; y++) {
        for (uint32_t x = r->left; x <= r->right; x++, p += 3) {
            if (x < j->ox || y < j->oy || x >= j->ox + j->side || y >= j->oy + j->side) continue;
            const uint32_t sx = x - j->ox, sy = y - j->oy;
            uint32_t tx0 = sx * COVER_PX / j->side, tx1 = (sx + 1) * COVER_PX / j->side;
            uint32_t ty0 = sy * COVER_PX / j->side, ty1 = (sy + 1) * COVER_PX / j->side;
            if (tx1 <= tx0) tx1 = tx0 + 1;
            if (ty1 <= ty0) ty1 = ty0 + 1;
            const uint16_t px = (uint16_t)(((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3));
            for (uint32_t ty = ty0; ty < ty1 && ty < (uint32_t)COVER_PX; ty++)
                for (uint32_t tx = tx0; tx < tx1 && tx < (uint32_t)COVER_PX; tx++)
                    j->tile[ty * COVER_PX + tx] = px;
        }
    }
    return 1;
}

uint32_t jpeg_tone(const uint8_t* jpg, size_t len, uint32_t w, uint32_t h) {
    // TJpgDec R0.01 (the ROM's) asks for about 3.1 KB of pool with a 512-byte input buffer.
    constexpr size_t POOL = 4096;
    void* pool = heap_caps_malloc(POOL, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!pool) pool = heap_caps_malloc(POOL, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    auto* tile = static_cast<uint16_t*>(heap_caps_calloc(
        (size_t)COVER_PX * COVER_PX, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    uint32_t rgb = 0;
    if (pool && tile) {
        // As much reduction as still leaves the square 70 decoded pixels across — what
        // cover_tone() samples of a 280-pixel tile anyway (every 4th pixel each way).
        const uint32_t short_side = w < h ? w : h;
        uint8_t scale = 0;
        while (scale < 3 && (short_side >> (scale + 1)) >= COVER_PX / 4) scale++;
        ToneJob job{jpg, len, 0, tile, 0, 0, 0};
        JDEC jd = {};
        const JRESULT prep = jd_prepare(&jd, tone_in, pool, POOL, &job);
        if (prep == JDR_OK) {
            const uint32_t sw = jd.width >> scale, sh = jd.height >> scale;
            job.side = sw < sh ? sw : sh;
            job.ox = (sw - job.side) / 2;
            job.oy = (sh - job.side) / 2;
            if (job.side && jd_decomp(&jd, tone_out, scale) == JDR_OK)
                rgb = cover_tone(tile, COVER_PX, COVER_PX);
        } else {
            ESP_LOGD(TAG, "no colour: TJpgDec says %d", (int)prep);
        }
    }
    heap_caps_free(tile);
    heap_caps_free(pool);
    return rgb;
}
#else
uint32_t jpeg_tone(const uint8_t*, size_t, uint32_t, uint32_t) { return 0; }
#endif

}  // namespace

bool cover_set_jpeg(const uint8_t* jpg, size_t len, CoverSource source, uint32_t expected_generation) {
    ProducerLock lock;
    Store& g = stores[(int)source];
    if (expected_generation != UINT32_MAX && g.gen.load() != expected_generation) return false;
    if (!jpg || len < 8) return false;

    const bool png = is_png(jpg, len);
    uint32_t w = 0, h = 0;
    bool progressive = false;
    if (!(png ? png_size(jpg, len, w, h) : jpeg_size(jpg, len, w, h, progressive))) {
        ESP_LOGW(TAG, "not a JPEG or a PNG (%u bytes)", (unsigned)len);
        return false;
    }
    if (!w || !h || w > MAX_SIDE || h > MAX_SIDE) {
        ESP_LOGW(TAG, "artwork %lux%lu is out of range (limit %u a side)",
                 (unsigned long)w, (unsigned long)h, (unsigned)MAX_SIDE);
        return false;
    }
    if (len > MAX_BYTES) {
        ESP_LOGW(TAG, "artwork of %u KB not kept (limit %u KB)", (unsigned)(len >> 10),
                 (unsigned)(MAX_BYTES >> 10));
        return false;
    }
    Blob* b = blob_new(jpg, len, png);
    if (!b) {
        ESP_LOGW(TAG, "no room for %u bytes of artwork", (unsigned)len);
        return false;
    }
    // The colour from the copy, outside the spinlock; TJpgDec cannot read progressive files.
    const uint32_t rgb = (png || progressive) ? 0 : jpeg_tone(b->data(), len, w, h);

    Blob* old = nullptr;
    portENTER_CRITICAL(&publication_mux);
    if (expected_generation != UINT32_MAX && g.gen.load() != expected_generation) {
        portEXIT_CRITICAL(&publication_mux);
        blob_unref(b);
        return false;
    }
    old = g.blob;
    g.blob = b;
    g.rgb = rgb;
    g.gen.fetch_add(1, std::memory_order_release);
    portEXIT_CRITICAL(&publication_mux);
    blob_unref(old);
    ESP_LOGI(TAG, "artwork %lux%lu %s, %u bytes kept, colour %06lx", (unsigned long)w,
             (unsigned long)h, png ? "PNG" : progressive ? "progressive JPEG" : "JPEG",
             (unsigned)len, (unsigned long)rgb);
    return true;
}

void cover_clear(CoverSource source) {
    Store& g = stores[(int)source];
    Blob* old = nullptr;
    portENTER_CRITICAL(&publication_mux);
    old = g.blob;
    g.blob = nullptr;
    g.rgb = 0;
    // Also cancels a producer which has not published yet.
    g.gen.fetch_add(1, std::memory_order_release);
    portEXIT_CRITICAL(&publication_mux);
    blob_unref(old);
}

CoverView cover_view(CoverSource source) {
    Store& g = stores[(int)source];
    CoverView v;
    portENTER_CRITICAL(&publication_mux);
    v.gen = g.gen.load(std::memory_order_acquire);
    if (g.blob) {
        v.bytes = g.blob->len;
        v.rgb = g.rgb;
    }
    portEXIT_CRITICAL(&publication_mux);
    return v;
}

CoverView cover_now(int32_t source, bool push, CoverSource& tile) {
    if (!cover_tile_for(source, push, tile)) return CoverView{};
    CoverView v = cover_view(tile);
    if (tile == CoverSource::Network && !cover_present(v)) {
        tile = CoverSource::Radio;
        v = cover_view(tile);
    }
    return v;
}

bool cover_bytes_get(CoverSource source, CoverBytes& out) {
    Store& g = stores[(int)source];
    portENTER_CRITICAL(&publication_mux);
    Blob* b = g.blob;
    if (b) b->refs.fetch_add(1, std::memory_order_relaxed);
    const uint32_t gen = g.gen.load(std::memory_order_acquire);
    portEXIT_CRITICAL(&publication_mux);
    if (!b) {
        out = CoverBytes{};
        return false;
    }
    out.data = b->data();
    out.len  = b->len;
    out.png  = b->png;
    out.gen  = gen;
    out.ref  = b;
    return true;
}

void cover_bytes_put(CoverBytes& b) {
    blob_unref(static_cast<Blob*>(b.ref));
    b = CoverBytes{};
}

}  // namespace player_core

#endif  // !CONFIG_COVER_DECODE_TILE
