/**
 * @file ui_frame_meter.h
 * @brief GET /api/ui/frames — how long the glass takes to draw what it shows. See ui_frame_meter.cpp.
 */
#pragma once

#include "esp_http_server.h"

/// Register the route. Called from /api/screenshot's registrar, as ui_fit_register is: all three
/// look at the glass, and modesp::http_routes has one registrar slot left (MAX_REGISTRARS).
void ui_frames_register(httpd_handle_t server);
