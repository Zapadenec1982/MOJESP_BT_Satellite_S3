/**
 * @file headless_web.cpp
 * @brief GET /api/cover and GET /api/library for a board without a display — see headless_web.h.
 *
 * The same two routes the P4 panel's module serves (modules/ui_lvgl/src/ui_web_media.cpp), under
 * the same names, so the web needs no second version:
 *   - the library is the card's index (components/playlist), a page at a time; its body is
 *     web_media::library_page, which the host suite tests and the panel uses too;
 *   - the cover is NOT a BMP of a decoded tile here — there is no tile (components/cover,
 *     CONFIG_COVER_DECODE_TILE=n). It is the file that arrived, JPEG or PNG, sent as it is with
 *     its own type; the browser decodes it, and the page's object-fit crops it as the panel's
 *     tile was cropped. `px` is accepted and ignored.
 *
 * Bodies come from PSRAM, never the 8 KB httpd stack. Both routes are behind the web's login.
 */

#include "headless_web.h"
#include "web_media.h"
#include "cover.h"
#include "playlist.h"
#include "modesp/net/http_routes.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const char* TAG = "HeadlessWeb";

static_assert(web_media::LIB_REL_CAP == (size_t)player_core::Playlist::MAX_REL,
              "web_media's row buffer must hold a whole index entry");

namespace {

std::atomic<int> s_cover_tile{-1};

esp_err_t send_json(httpd_req_t* req, const char* status, const char* body) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

/// `key` from the request's query as a number, or −1 when absent or not a number.
long query_num(const char* query, const char* key) {
    char v[16];
    if (!query || httpd_query_key_value(query, key, v, sizeof(v)) != ESP_OK || !v[0]) return -1;
    char* end = nullptr;
    const long n = strtol(v, &end, 10);
    return (end && *end == '\0' && n >= 0) ? n : -1;
}

// ── GET /api/library?from=&n= ── (the panel's handler, ui_web_media.cpp, line for line)

esp_err_t library_get(httpd_req_t* req) {
    if (!modesp::http_routes::authorized(req)) return ESP_OK;
    char query[48] = "";
    const bool has_query = httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK;
    const web_media::Page page = web_media::clamp_page(has_query ? query_num(query, "from") : -1,
                                                       has_query ? query_num(query, "n") : -1);

    char* body = (char*)heap_caps_malloc(web_media::LIB_JSON_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) return send_json(req, "500 Internal Server Error", "{\"error\":\"no_memory\"}");

    const auto& q = player_core::playlist();
    char root[player_core::Playlist::MAX_DIR];
    q.root(root, sizeof(root));
    const auto rel = [&q](int i, char* out, size_t n) { return q.rel_at(i, out, n); };

    // The index is settled while its generation is even and the same before and after the page
    // was read; the player rebuilds it when a card appears. Otherwise the page says busy, with
    // no rows, and the web asks again.
    const uint32_t gen = q.generation();
    int rows = 0;
    size_t len = web_media::library_page(body, web_media::LIB_JSON_CAP, gen, (gen & 1u) != 0,
                                         q.count(), q.cursor(), root, page, rel, &rows);
    if (len && !(gen & 1u) && q.generation() != gen) {
        len = web_media::library_page(body, web_media::LIB_JSON_CAP, q.generation(), true, 0, 0,
                                      root, page, rel, &rows);
    }
    if (!len) {
        heap_caps_free(body);
        return send_json(req, "500 Internal Server Error", "{\"error\":\"too_large\"}");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    const esp_err_t err = httpd_resp_send(req, body, len);
    heap_caps_free(body);
    return err;
}

// ── GET /api/cover ──

esp_err_t cover_get(httpd_req_t* req) {
    if (!modesp::http_routes::authorized(req)) return ESP_OK;
    const int t = s_cover_tile.load(std::memory_order_acquire);
    if (t < 0 || t >= player_core::COVER_SOURCES) {
        return send_json(req, "404 Not Found", "{\"error\":\"no_cover\"}");
    }
    const auto tile = (player_core::CoverSource)t;
    // A reference, not a copy: a producer publishing the next picture while this one is still
    // going out drops only the store's own reference (cover.h, cover_bytes_get).
    player_core::CoverBytes b;
    if (!player_core::cover_bytes_get(tile, b)) {
        return send_json(req, "404 Not Found", "{\"error\":\"no_cover\"}");
    }
    player_core::CoverView v;
    v.gen   = b.gen;
    v.bytes = b.len;
    // The same tag for the same picture: a browser that has it gets a 304.
    char etag[24];
    snprintf(etag, sizeof(etag), "\"%ld\"", (long)player_core::cover_id(v, tile));
    char have[24];
    esp_err_t err;
    if (httpd_req_get_hdr_value_str(req, "If-None-Match", have, sizeof(have)) == ESP_OK &&
        strcmp(have, etag) == 0) {
        httpd_resp_set_status(req, "304 Not Modified");
        httpd_resp_set_hdr(req, "ETag", etag);
        httpd_resp_set_hdr(req, "Cache-Control", "private, no-cache");
        err = httpd_resp_send(req, nullptr, 0);
    } else {
        httpd_resp_set_type(req, b.png ? "image/png" : "image/jpeg");
        httpd_resp_set_hdr(req, "ETag", etag);
        httpd_resp_set_hdr(req, "Cache-Control", "private, no-cache");
        err = httpd_resp_send(req, (const char*)b.data, (ssize_t)b.len);
    }
    player_core::cover_bytes_put(b);
    return err;
}

void register_routes(httpd_handle_t server, void*) {
    httpd_uri_t lib{};
    lib.uri = "/api/library";
    lib.method = HTTP_GET;
    lib.handler = library_get;
    if (httpd_register_uri_handler(server, &lib) != ESP_OK) ESP_LOGW(TAG, "/api/library not registered");
    httpd_uri_t cover{};
    cover.uri = "/api/cover";
    cover.method = HTTP_GET;
    cover.handler = cover_get;
    if (httpd_register_uri_handler(server, &cover) != ESP_OK) ESP_LOGW(TAG, "/api/cover not registered");
}

}  // namespace

void headless_web_init() {
    if (!modesp::http_routes::add(register_routes)) {
        ESP_LOGW(TAG, "no room in the route table — /api/library and /api/cover will 404");
    }
}

void headless_web_set_cover(int tile) {
    s_cover_tile.store(tile, std::memory_order_release);
}
