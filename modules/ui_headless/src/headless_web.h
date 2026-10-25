/**
 * @file headless_web.h
 * @brief GET /api/cover and GET /api/library on a board without a display.
 */

#pragma once

/// Asks HttpService for the two routes (modesp::http_routes). Once, from on_init().
void headless_web_init();

/// Which cover the Player shows now (player_core::CoverSource), −1 for none. The main loop says
/// it with ui.cover; GET /api/cover sends that source's file and no other.
void headless_web_set_cover(int tile);
