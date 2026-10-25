/**
 * @file ui_headless_module.cpp
 * @brief The player without a display — see ui_headless_module.h.
 *
 * Everything here is what modules/ui_lvgl does on the main loop, without the screen: the
 * encoder (ui_lvgl_module.cpp, poll_inputs; ui_shell.cpp, ui_shell_encoder), the Player's
 * play/pause (screen_now.cpp, transport; ui_lvgl_module.cpp, apply_requests: TRANSPORT,
 * RADIO_CMD, PLAY_QUEUE) and the Player's picture for the web (mirror_state). Where the panel
 * goes through its request ring to reach the main loop, this already runs on it and writes
 * SharedState directly.
 */

#include "ui_headless_module.h"
#include "headless_web.h"

#include "modesp/hal/driver_manager.h"
#include "modesp/hal/driver_interfaces.h"
#include "modesp/types.h"
#include "cover.h"
#include "playlist.h"

#include "esp_log.h"

#include <cstring>

static const char* TAG = "UiHeadless";

namespace {

/// The knob's hold: fired while still held, so the mute comes when the hand expects it.
constexpr uint32_t HOLD_MS = 650;
/// How often the Player's picture is looked at: the panel's mirror rate.
constexpr uint32_t COVER_EVERY_MS = 50;
/// ui.theme and ui.lang: four of each, the panel's order (webui/src/lib/lang.js, schemes.js).
constexpr int32_t THEMES = 4;
constexpr int32_t LANGS  = 4;

}  // namespace

UiHeadlessModule::UiHeadlessModule()
    : BaseModule("ui_headless", modesp::ModulePriority::LOW)   // as ui_lvgl: after the sources
{}

void UiHeadlessModule::on_bind(modesp::DriverManager& drivers, const modesp::BindingTable&,
                               modesp::HAL&) {
    encoder_    = drivers.find_sensor("encoder_main");
    encoder_sw_ = drivers.find_sensor("encoder_sw");
    ESP_LOGI(TAG, "bound: encoder=%s button=%s", encoder_ ? encoder_->type() : "no",
             encoder_sw_ ? encoder_sw_->type() : "no");
}

bool UiHeadlessModule::on_init() {
    state_set("ui.encoder_pos", (int32_t)0);
    state_set("ui.encoder_sw", false);
    state_set("ui.cover", (int32_t)0);
    state_set("ui.cover_rgb", (int32_t)0);

    // The web reads both and the panel is not here to have set them: keep what was persisted,
    // within the four the web knows.
    const int32_t th = read_int("ui.theme", 0);
    state_set("ui.theme", th >= 0 && th < THEMES ? th : (int32_t)0);
    const int32_t lang = read_int("ui.lang", 0);
    state_set("ui.lang", lang >= 0 && lang < LANGS ? lang : (int32_t)0);

    // GET /api/cover and /api/library.
    headless_web_init();

    // Bluetooth pairing is NOT routed to a confirmer here. With a screen, the satellite asks the
    // panel and the panel asks the person in front of it. Without one, nobody may be looking at
    // the web when a phone pairs, and an unanswered request is a pairing that fails after a
    // minute and a half (modules/bt, bt_settings.h) — so the satellite accepts on its own, as a
    // speaker does.
    return true;
}

void UiHeadlessModule::on_update(uint32_t dt_ms) {
    poll_encoder(dt_ms);
    cover_ms_ += dt_ms;
    if (cover_ms_ >= COVER_EVERY_MS) {
        cover_ms_ = 0;
        mirror_cover();
    }
}

void UiHeadlessModule::poll_encoder(uint32_t dt_ms) {
    int32_t pos = last_pos_;
    bool sw = last_sw_;
    float v = 0.0f;
    if (encoder_ && encoder_->read(v)) pos = (int32_t)v;
    if (encoder_sw_ && encoder_sw_->read(v)) sw = (v > 0.5f);

    if (pos != last_pos_) {
        const int32_t delta = pos - last_pos_;
        last_pos_ = pos;
        state_set("ui.encoder_pos", pos);
        if (state_get("audio.volume").has_value()) {
            int32_t vol = read_int("audio.volume", 0) + delta * 2;
            if (vol < 0) vol = 0; else if (vol > 100) vol = 100;
            // A turn is wanting sound at that level: a mute set on the web ends with it, or the
            // knob would turn a silent player up with nothing to hear.
            if (read_bool("audio.mute", false)) state_set("audio.mute", false);
            if (vol != read_int("audio.volume", -1)) state_set("audio.volume", vol);
        }
    }

    if (sw) {
        sw_held_ms_ += dt_ms;
        if (!sw_hold_done_ && sw_held_ms_ >= HOLD_MS) {
            sw_hold_done_ = true;
            if (state_get("audio.mute").has_value())
                state_set("audio.mute", !read_bool("audio.mute", false));
        }
    }
    if (sw != last_sw_) {
        last_sw_ = sw;
        state_set("ui.encoder_sw", sw);
        if (!sw) {                                   // released
            if (!sw_hold_done_) play_pause();
            sw_held_ms_ = 0;
            sw_hold_done_ = false;
        }
    }
}

/// The Player's play button, in the source's own language (screen_now.cpp, transport(); the
/// codes are ui_lvgl's apply_requests TRANSPORT / RADIO_CMD / PLAY_QUEUE).
void UiHeadlessModule::play_pause() {
    const int32_t source = read_int("audio.source", 0);
    char mode[12] = "";
    read_string("radio.mode", mode, sizeof(mode));
    const bool network_push = source == 5 && strcmp(mode, "push") == 0;
    const bool is_radio = source == 5 && !network_push;
    const bool has_track = source == 4 || network_push || source == 3 || source == 6;
    if (!is_radio && !has_track) return;             // the tone, AUX, USB, off: nothing to command

    if (source == 3) {
        // Nobody to command without a phone.
        char bt[16] = "";
        read_string("bt.state", bt, sizeof(bt));
        if (strcmp(bt, "connected") != 0 && strcmp(bt, "streaming") != 0) return;
        post_command_to("bt", "bt.cmd", (int32_t)6);  // bt.cmd speaks player.cmd's codes
        return;
    }
    if (source == 6) {
        // An AirPlay sender that gave no remote (AirPlay 2 mode) gets nothing — and the press must
        // not fall through to the card player either, which would take the output from the phone.
        if (read_bool("airplay.remote", false)) post_command_to("airplay", "airplay.cmd", (int32_t)6);
        return;
    }
    if (is_radio) {
        char st[16] = "";
        read_string("radio.state", st, sizeof(st));
        post_command_to("radio", "radio.cmd", (int32_t)(strcmp(st, "playing") == 0 ? 2 : 1));
        return;
    }
    if (network_push) {
        char st[24] = "";
        read_string("dlna.state", st, sizeof(st));
        post_command_to("dlna", "dlna.cmd", (int32_t)(strcmp(st, "PLAYING") == 0 ? 3 : 1));
        return;
    }
    // The card. The toggle only pauses and resumes a track that is open, so from idle it would do
    // nothing: start the track the cursor is on.
    char st[12] = "";
    read_string("player.state", st, sizeof(st));
    if (strcmp(st, "playing") != 0 && strcmp(st, "paused") != 0) {
        char full[MODESP_MAX_LONG_STRING_LENGTH];
        const auto& q = player_core::playlist();
        if (q.count() > 0 && q.path_at(q.cursor(), full, sizeof(full))) {
            state_set("player.track", full);
            post_command_to("player", "player.cmd", (int32_t)1);
            ESP_LOGI(TAG, "play %d: %s", q.cursor(), full);
        }
        return;
    }
    post_command_to("player", "player.cmd", (int32_t)6);
}

/// The Player's picture, by the panel's rule (cover_now): ui.cover moves with every new one and
/// GET /api/cover sends that tile's file; ui.cover_rgb is the backdrop made from its colour.
void UiHeadlessModule::mirror_cover() {
    const int32_t source = read_int("audio.source", 0);
    char mode[12] = "";
    read_string("radio.mode", mode, sizeof(mode));
    const bool network_push = source == 5 && strcmp(mode, "push") == 0;

    player_core::CoverSource tile;
    const auto cv = player_core::cover_now(source, network_push, tile);
    const bool present = player_core::cover_present(cv);
    headless_web_set_cover(present ? (int)tile : -1);
    const int32_t id = player_core::cover_id(cv, tile);
    if (id != cover_id_) {
        cover_id_ = id;
        state_set("ui.cover", id);
        state_set("ui.cover_rgb", (int32_t)(present ? player_core::cover_backdrop(cv.rgb) : 0u));
    }
}
