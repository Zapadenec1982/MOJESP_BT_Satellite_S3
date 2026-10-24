/**
 * @file esp_dlna_compat.h
 * @brief One switch, for the one place the prebuilt UPnP library's ABI does not match ours.
 *
 * See httpd_uri_compat.c for what it is and why it exists. Turn it on immediately before
 * the library registers its routes and off immediately after — never leave it on, because
 * while it is on, a WebSocket handler cannot be registered.
 */

#pragma once

#include <stdbool.h>
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/// true while the UPnP library is registering URI handlers, false at all other times.
void esp_dlna_httpd_compat(bool on);
// Replace the binary library's AVTransport POST handler; it discards setter errors.
void esp_dlna_avt_handler(esp_err_t (*handler)(httpd_req_t*));

#ifdef __cplusplus
}
#endif
