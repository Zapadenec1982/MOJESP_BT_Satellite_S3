/**
 * @file bt_settings.cpp
 * @brief See bt_settings.h.
 */

#include "bt_settings.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include <atomic>
#include <cstring>
#include <new>

namespace bt_settings {
namespace {

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

/// PSRAM, made on first use: the bonds and their names are ~700 bytes, touched a few times
/// a minute, and internal RAM on this board is the scarce kind.
View* view() {
    static View* v = [] {
        void* m = heap_caps_calloc(1, sizeof(View), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!m) m = heap_caps_calloc(1, sizeof(View), MALLOC_CAP_8BIT);
        return m ? new (m) View{} : nullptr;
    }();
    return v;
}

Pairing           s_pair;
Pending           s_pending;
bool              s_any_pending = false;
std::atomic<bool> s_confirmer{false};

void copy_name(char* dst, const char* src, size_t n) {
    if (n >= NAME_CAP) n = NAME_CAP - 1;
    if (src && n) memcpy(dst, src, n);
    dst[n] = '\0';
}

}  // namespace

// ── reading ──

uint32_t revision() {
    View* v = view();
    if (!v) return 0;
    portENTER_CRITICAL(&s_mux);
    const uint32_t r = v->revision;
    portEXIT_CRITICAL(&s_mux);
    return r;
}

void snapshot(View& out) {
    View* v = view();
    if (!v) { out = View{}; return; }
    portENTER_CRITICAL(&s_mux);
    memcpy(&out, v, sizeof(View));
    portEXIT_CRITICAL(&s_mux);
}

void pairing(Pairing& out) {
    portENTER_CRITICAL(&s_mux);
    out = s_pair;
    portEXIT_CRITICAL(&s_mux);
}

void config(Config& out) {
    View* v = view();
    if (!v) { out = Config{}; return; }
    portENTER_CRITICAL(&s_mux);
    out.known = v->known;
    out.radio = v->radio;
    out.discoverable = v->discoverable;
    out.auto_reconnect = v->auto_reconnect;
    out.codecs_enabled = v->codecs_enabled;
    portEXIT_CRITICAL(&s_mux);
}

void name(char* out, size_t n) {
    if (!out || !n) return;
    out[0] = '\0';
    View* v = view();
    if (!v) return;
    portENTER_CRITICAL(&s_mux);
    if (v->known) {
        const size_t len = strnlen(v->name, NAME_CAP - 1);   // copy_name copies exactly this many
        copy_name(out, v->name, len < n ? len : n - 1);
    }
    portEXIT_CRITICAL(&s_mux);
}

// ── requests ──

void request_config(bool radio, bool discoverable, bool auto_reconnect, uint8_t codecs) {
    portENTER_CRITICAL(&s_mux);
    s_pending.config = true;
    s_pending.radio = radio;
    s_pending.discoverable = discoverable;
    s_pending.auto_reconnect = auto_reconnect;
    s_pending.codecs = codecs;
    s_any_pending = true;
    portEXIT_CRITICAL(&s_mux);
}

bool request_name(const char* name) {
    const size_t n = name ? strlen(name) : 0;
    if (n == 0 || n > NAME_CAP - 1) return false;
    portENTER_CRITICAL(&s_mux);
    s_pending.rename = true;
    memcpy(s_pending.name, name, n);
    s_pending.name[n] = '\0';
    s_any_pending = true;
    portEXIT_CRITICAL(&s_mux);
    return true;
}

void request_forget(const uint8_t* addr) {
    portENTER_CRITICAL(&s_mux);
    s_pending.forget = true;
    s_pending.forget_all = (addr == nullptr);
    if (addr) memcpy(s_pending.forget_addr, addr, 6);
    s_any_pending = true;
    portEXIT_CRITICAL(&s_mux);
}

bool request_pair_reply(uint32_t seq, bool accept, Source by) {
    portENTER_CRITICAL(&s_mux);
    const bool first = s_pair.active && s_pair.seq == seq && s_pair.by == Source::NONE;
    if (first) {
        s_pending.reply = true;
        s_pending.reply_seq = seq;
        s_pending.accept = accept;
        s_any_pending = true;
        s_pair.by = by;
        s_pair.accepted = accept;
    }
    portEXIT_CRITICAL(&s_mux);
    return first;
}

void request_disconnect() {
    portENTER_CRITICAL(&s_mux);
    s_pending.disconnect = true;
    s_any_pending = true;
    portEXIT_CRITICAL(&s_mux);
}

void request_refresh() {
    portENTER_CRITICAL(&s_mux);
    s_pending.refresh = true;
    s_any_pending = true;
    portEXIT_CRITICAL(&s_mux);
}

void set_confirmer(bool on) { s_confirmer = on; }
bool confirmer() { return s_confirmer.load(); }

// ── the bt module's side ──

bool take(Pending& out) {
    portENTER_CRITICAL(&s_mux);
    const bool any = s_any_pending;
    if (any) {
        out = s_pending;
        s_pending = Pending{};
        s_any_pending = false;
    }
    portEXIT_CRITICAL(&s_mux);
    return any;
}

void set_link(bool linked, bool settings) {
    View* v = view();
    if (!v) return;
    portENTER_CRITICAL(&s_mux);
    if (v->linked != linked || v->settings != settings) {
        v->linked = linked;
        v->settings = settings;
        if (!settings) {           // what a satellite said about itself leaves with it
            v->known = false;
            v->count = 0;
            v->total = 0;
        }
        v->revision++;
    }
    portEXIT_CRITICAL(&s_mux);
}

void set_config(bool radio, bool discoverable, bool auto_reconnect, uint8_t supported,
                uint8_t enabled, const char* name, size_t name_len) {
    View* v = view();
    if (!v) return;
    portENTER_CRITICAL(&s_mux);
    v->known = true;
    v->radio = radio;
    v->discoverable = discoverable;
    v->auto_reconnect = auto_reconnect;
    v->codecs_supported = supported;
    v->codecs_enabled = enabled;
    copy_name(v->name, name, name_len);
    v->revision++;
    portEXIT_CRITICAL(&s_mux);
}

void set_bonds(uint8_t total, const Bond* bonds, uint8_t count) {
    View* v = view();
    if (!v) return;
    if (count > BONDS) count = BONDS;
    portENTER_CRITICAL(&s_mux);
    v->total = total;
    v->count = count;
    memcpy(v->bonds, bonds, sizeof(Bond) * count);
    v->revision++;
    portEXIT_CRITICAL(&s_mux);
}

void pair_begin(const uint8_t addr[6], uint8_t kind, uint32_t passkey, const char* name, size_t name_len) {
    portENTER_CRITICAL(&s_mux);
    const bool same = s_pair.active && memcmp(s_pair.addr, addr, 6) == 0 &&
                      s_pair.kind == kind && s_pair.passkey == passkey;
    if (!same) {
        s_pair.seq++;
        s_pair.active = true;
        memcpy(s_pair.addr, addr, 6);
        s_pair.kind = kind;
        s_pair.passkey = passkey;
        copy_name(s_pair.name, name, name_len);
        s_pair.result = 0xFF;
        s_pair.by = Source::NONE;
        s_pair.accepted = false;
    }
    portEXIT_CRITICAL(&s_mux);
}

void pair_end(const uint8_t addr[6], uint8_t status) {
    portENTER_CRITICAL(&s_mux);
    // The result of a request this side never saw still closes whatever is open: there is
    // one pairing at a time, and a dialog left up for a phone that has finished is worse.
    if (s_pair.active || memcmp(s_pair.addr, addr, 6) == 0) {
        s_pair.active = false;
        s_pair.result = status;
        if (s_pair.seq == 0) s_pair.seq = 1;
    }
    portEXIT_CRITICAL(&s_mux);
}

void pair_abandon() {
    portENTER_CRITICAL(&s_mux);
    if (s_pair.active) {
        s_pair.active = false;
        s_pair.result = 3;         // sat::PAIR_FAILED: lost the link, per the contract's own list
    }
    portEXIT_CRITICAL(&s_mux);
}

}  // namespace bt_settings
