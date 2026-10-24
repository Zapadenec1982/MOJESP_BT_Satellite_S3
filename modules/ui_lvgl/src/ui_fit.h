/**
 * @file ui_fit.h
 * @brief GET /api/i18n/overflow — what text on the glass does not fit. See ui_fit.cpp.
 */
#pragma once

#include "esp_http_server.h"

/// Register the route. Called from /api/screenshot's registrar: both look at the glass, and
/// modesp::http_routes has one registrar slot left (MAX_REGISTRARS), not to be spent on this.
void ui_fit_register(httpd_handle_t server);
