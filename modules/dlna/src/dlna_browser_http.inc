// GET /api/dlna/browser — the local browser's snapshot, for the web's DLNA page (PLAN_WEBUI §2,
// A7) and tools/dlna_acceptance.py. Commands use the ordinary SharedState mailbox
// (dlna.browse_cmd), exactly as the panel does, including revision validation.
//
// Behind the web's login: it lists what is on the household's media servers. The snapshot and
// its body live in PSRAM for the length of the call, not on the 8 KB httpd stack.
esp_err_t browser_http(httpd_req_t *req) {
    if (!modesp::http_routes::authorized(req)) return ESP_OK;
    auto *view = (dlna_client::View *)heap_caps_malloc(sizeof(dlna_client::View),
                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char *response = (char *)heap_caps_malloc(dlna_client::BROWSER_JSON_CAP,
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!view || !response) {
        heap_caps_free(view);
        heap_caps_free(response);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
    }
    new (view) dlna_client::View{};
    if (!dlna_client::snapshot(*view)) {
        heap_caps_free(view);
        heap_caps_free(response);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Browser unavailable");
    }
    const size_t used = dlna_client::browser_json(*view, response, dlna_client::BROWSER_JSON_CAP);
    heap_caps_free(view);
    if (!used) {
        heap_caps_free(response);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Snapshot too large");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    auto result = httpd_resp_send(req, response, used);
    heap_caps_free(response);
    return result;
}
