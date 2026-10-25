/**
 * @file ui_web_media.h
 * @brief GET /api/library and GET /api/cover — the card's index and the Player's picture, for
 *        the web.
 */

#pragma once

/// Asks HttpService for the two routes (modesp::http_routes). Once, from on_init(); the routes
/// do not need the screen, so a panel whose display failed still answers.
void ui_web_media_init();

/// Which cover tile the Player shows now (player_core::CoverSource), −1 for none. The main loop
/// says it with ui.cover (mirror_state()); GET /api/cover sends that tile and no other.
void ui_web_media_set_cover(int tile);
