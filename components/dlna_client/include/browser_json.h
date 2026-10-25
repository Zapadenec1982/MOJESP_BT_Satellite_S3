/**
 * @file browser_json.h
 * @brief The body of GET /api/dlna/browser, written from a browser snapshot (dlna_browser.h).
 *
 * Header-only and free of IDF so the host suite holds it to its shape
 * (tests/host/test_dlna_browser_json.cpp); modules/dlna/src/dlna_browser_http.inc sends it.
 *
 * The web's DLNA page reads the same snapshot the panel's
 * page draws, and commands it the same way: dlna.browse_cmd, encode()d with the revision shown.
 * `can_back` is what the panel's header ← asks before it climbs a level; without it the web
 * could only guess whether "up" exists.
 */

#pragma once

#include "dlna_browser.h"
#include <cstddef>
#include <cstdio>

namespace dlna_client {

/// Enough for any snapshot: twelve rows of two 95-byte strings plus the title and the status,
/// every byte escaped at its worst (\u00XX, six), is 15.1 KB with the frame. In PSRAM.
constexpr size_t BROWSER_JSON_CAP = 16384;

namespace browser_json_detail {

struct Out {
    char*  p;
    size_t cap;
    size_t n  = 0;
    bool   ok = true;

    void put(char c) {
        if (n + 1 < cap) p[n++] = c;
        else ok = false;
    }
    void raw(const char* s) {
        while (*s) put(*s++);
    }
    void flag(bool b) { raw(b ? "true" : "false"); }
    void num(unsigned v) {
        char b[12];
        snprintf(b, sizeof(b), "%u", v);
        raw(b);
    }
    /// A string field of the snapshot, quoted and escaped; never read past `max`.
    void text(const char* s, size_t max) {
        put('"');
        for (size_t i = 0; i < max && s[i]; ++i) {
            const unsigned char c = (unsigned char)s[i];
            if (c == '"' || c == '\\') {
                put('\\');
                put((char)c);
            } else if (c < 32) {
                char e[8];
                snprintf(e, sizeof(e), "\\u%04x", c);
                raw(e);
            } else {
                put((char)c);
            }
        }
        put('"');
    }
};

}  // namespace browser_json_detail

/// {"revision","busy","servers","can_back","count","next","previous","title","status",
///  "rows":[{"title","artist","folder","playable"}]}. Returns its length, NUL-terminated in
/// `out`, or 0 when `cap` could not hold it — never a cut document.
inline size_t browser_json(const View& v, char* out, size_t cap) {
    if (!out || cap == 0) return 0;
    browser_json_detail::Out o{out, cap};
    const int count = v.count > PAGE_SIZE ? PAGE_SIZE : v.count;
    o.raw("{\"revision\":");
    o.num(v.revision);
    o.raw(",\"busy\":");
    o.flag(v.busy);
    o.raw(",\"servers\":");
    o.flag(v.servers);
    o.raw(",\"can_back\":");
    o.flag(v.can_back);
    o.raw(",\"count\":");
    o.num((unsigned)count);
    o.raw(",\"next\":");
    o.flag(v.has_next);
    o.raw(",\"previous\":");
    o.flag(v.has_prev);
    o.raw(",\"title\":");
    o.text(v.title, sizeof(v.title));
    o.raw(",\"status\":");
    o.text(v.status, sizeof(v.status));
    o.raw(",\"rows\":[");
    for (int i = 0; i < count; ++i) {
        const Row& r = v.rows[i];
        o.raw(i ? ",{\"title\":" : "{\"title\":");
        o.text(r.title, sizeof(r.title));
        o.raw(",\"artist\":");
        o.text(r.subtitle, sizeof(r.subtitle));
        o.raw(",\"folder\":");
        o.flag(r.folder);
        o.raw(",\"playable\":");
        o.flag(r.playable);
        o.put('}');
    }
    o.raw("]}");
    if (!o.ok) {
        out[0] = '\0';
        return 0;
    }
    out[o.n] = '\0';
    return o.n;
}

}  // namespace dlna_client
