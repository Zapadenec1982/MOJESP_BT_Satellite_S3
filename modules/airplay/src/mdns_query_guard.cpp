/**
 * @file mdns_query_guard.cpp
 * @brief The AirPlay receiver's mDNS queries, serialized with Wi-Fi's mDNS init/free.
 *
 * components/airplay2/upstream/dacp_client.c asks mDNS for the phone's _dacp._tcp service
 * from the RTSP client task (the AirPlay 1 grace period, every ~5 s, while it holds the
 * server's cleanup lock) and from its dacp_wk worker. WiFiService frees and restarts mDNS on
 * the ESP event task under modesp::MdnsGuard; mdns_free() deletes the semaphore an in-flight
 * query waits on and frees its search. upstream/ stays as published, so the query is wrapped
 * at link time instead (-Wl,--wrap=mdns_query_ptr, ../CMakeLists.txt): dacp_client.c is its
 * only caller. A query holds the guard for its timeout, 2 s at most.
 */

#include "mdns.h"
#include "modesp/net/mdns_guard.h"

extern "C" esp_err_t __real_mdns_query_ptr(const char *service_type, const char *proto,
                                           uint32_t timeout, size_t max_results,
                                           mdns_result_t **results);

extern "C" esp_err_t __wrap_mdns_query_ptr(const char *service_type, const char *proto,
                                           uint32_t timeout, size_t max_results,
                                           mdns_result_t **results) {
    modesp::MdnsGuard guard;
    if (!guard.locked()) return ESP_ERR_INVALID_STATE;
    return __real_mdns_query_ptr(service_type, proto, timeout, max_results, results);
}
