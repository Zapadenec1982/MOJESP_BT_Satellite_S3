#pragma once
/**
 * @file userdata_http.h
 * @brief GET / POST /api/userdata — the user's files backed up and restored from the web
 *        (docs/PLAN_WEBUI_2026-10-11.md, A6). See userdata_http.cpp.
 */

/// Ask for the routes in the window before the static wildcard (modesp/net/http_routes.h).
/// Call before http_routes::run_all().
void userdata_http_add_routes();
