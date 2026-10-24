/**
 * @file eq_web.h
 * @brief The EQ document as the web reads and edits it: GET and POST /api/eq.
 *
 * Pure — no HTTP, no filesystem — so what the web is sent and what it may send are settled by
 * the host tests (tests/host/test_eq_store.cpp), and modules/audio only moves bytes.
 * docs/PLAN_WEBUI_2026-10-11.md §6, A3.
 *
 * GET:  {"rev":N,"fs":48000,"preset":"rock","presets":["custom",…],
 *        "bands":[{"type":"peak","f":1000,"gain":0,"q":1,"on":true},…]}
 *       "presets" are the sounds offered, and a kept preset while it plays (eq_offered).
 * POST: {"rev":N,"bands":[…]}   — section 0's bands, set by hand
 *       {"rev":N,"preset":"rock"}  — a preset by id, or an id it had ("custom" brings the
 *                                   user's own set back)
 *       {"keep":true}            — the page is left: write it now
 * An edit may carry "keep":true too — the last one sent as the page is left.
 *
 * Values from the web are held to what the panel's own page lets a finger reach
 * (screen_eq.cpp): 20 Hz – 20 kHz, ±12 dB, Q 0.3 – 8 and a shelf's Q at SHELF_Q_MAX.
 */

#pragma once

#include "eq_store.h"

#include <cstddef>
#include <cstdint>

namespace audio_core {

static constexpr float EQ_WEB_F_MIN = 20.0f, EQ_WEB_F_MAX = 20000.0f;
static constexpr float EQ_WEB_GAIN_MAX = 12.0f;
static constexpr float EQ_WEB_Q_MIN = 0.3f, EQ_WEB_Q_MAX = 8.0f;

/// GET's body. Returns its length, or 0 when it does not fit.
size_t eq_web_json(const EqDoc& doc, uint32_t rev, float fs_hz, char* buf, size_t buf_sz);

struct EqWebEdit {
    enum Kind : uint8_t { NONE, BANDS, PRESET, KEEP };
    Kind     kind = NONE;
    uint32_t rev = 0;                 ///< the revision the edit was made from
    int      preset = -1;
    EqBand   bands[MAX_BANDS];
    int      count = 0;
    bool     keep = false;            ///< write it now (alone, or with an edit)
};

/// POST's body. False for anything that is not one of the three shapes above: no "rev" on an
/// edit, a preset or a band type with no such name, no bands or more than MAX_BANDS. Numbers
/// out of range are clamped, not refused.
///
/// Its token array is static: one caller, the HTTP server's task.
bool eq_web_parse(const char* json, size_t len, EqWebEdit& out);

/// A band held to the limits above.
void eq_web_clamp(EqBand& b);

} // namespace audio_core
