/**
 * @file user_backup.cpp
 * @brief See user_backup.h.
 */

#include "user_backup.h"
#include "user_store.h"

#define JSMN_STATIC          // jsmn is header-only and linked in several places
#include "jsmn.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace modesp {
namespace user_backup {
namespace {

/// The envelope and five string values, with room for fields a later format may add.
constexpr int MAX_TOKENS = 64;

bool key_is(const char* json, const jsmntok_t& t, const char* name) {
    const int n = t.end - t.start;
    return t.type == JSMN_STRING && (int)strlen(name) == n && !strncmp(json + t.start, name, n);
}

/// Tokens `tok[i]` spans, itself included.
int span(const jsmntok_t* tok, int n, int i) {
    int k = 1;
    for (int c = 0; c < tok[i].size && i + k < n; c++) k += span(tok, n, i + k);
    return k;
}

int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

bool parse(const char* body, size_t len, Entry* out, size_t cap, size_t& count) {
    count = 0;
    static jsmntok_t tok[MAX_TOKENS];
    jsmn_parser p;
    jsmn_init(&p);
    const int n = jsmn_parse(&p, body, len, tok, MAX_TOKENS);
    if (n < 1 || tok[0].type != JSMN_OBJECT) return false;

    int files = -1;
    for (int i = 1, f = 0; f < tok[0].size && i + 1 < n; f++) {
        const int v = i + 1;
        if (key_is(body, tok[i], "modesp_userdata")) {
            if (tok[v].type != JSMN_PRIMITIVE) return false;
            if (atoi(body + tok[v].start) > VERSION) return false;
        } else if (key_is(body, tok[i], "files")) {
            if (tok[v].type != JSMN_OBJECT) return false;
            files = v;
        }
        i = v + span(tok, n, v);
    }
    if (files < 0) return false;

    // In UserStore::FILES order, whatever order the body has them in.
    for (const char* const* name = UserStore::FILES; *name && count < cap; ++name) {
        for (int i = files + 1, f = 0; f < tok[files].size && i + 1 < n; f++) {
            const int v = i + 1;
            if (key_is(body, tok[i], *name)) {
                if (tok[v].type != JSMN_STRING) return false;
                out[count].name = *name;
                out[count].text = body + tok[v].start;
                out[count].len = (size_t)(tok[v].end - tok[v].start);
                count++;
                break;
            }
            i = v + span(tok, n, v);
        }
    }
    return count > 0;
}

bool unescape(const char* s, size_t n, char* out, size_t cap, size_t& len) {
    size_t w = 0;
    auto put = [&](uint32_t cp) {
        char b[4];
        size_t k;
        if (cp < 0x80)       { b[0] = (char)cp; k = 1; }
        else if (cp < 0x800) { b[0] = (char)(0xC0 | (cp >> 6)); b[1] = (char)(0x80 | (cp & 0x3F)); k = 2; }
        else if (cp < 0x10000) {
            b[0] = (char)(0xE0 | (cp >> 12)); b[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
            b[2] = (char)(0x80 | (cp & 0x3F)); k = 3;
        } else {
            b[0] = (char)(0xF0 | (cp >> 18)); b[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
            b[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[3] = (char)(0x80 | (cp & 0x3F)); k = 4;
        }
        if (w + k >= cap) return false;
        memcpy(out + w, b, k);
        w += k;
        return true;
    };
    auto hex4 = [&](size_t at, uint32_t& v) {
        if (at + 4 > n) return false;
        v = 0;
        for (size_t k = 0; k < 4; k++) {
            const int h = hexval(s[at + k]);
            if (h < 0) return false;
            v = v * 16 + (uint32_t)h;
        }
        return true;
    };
    for (size_t i = 0; i < n;) {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x20) return false;
        if (c != '\\') {
            if (w + 1 >= cap) return false;
            out[w++] = (char)c;
            i++;
            continue;
        }
        if (i + 1 >= n) return false;
        uint32_t cp = 0;
        switch (s[i + 1]) {
        case '"':  cp = '"';  break;
        case '\\': cp = '\\'; break;
        case '/':  cp = '/';  break;
        case 'b':  cp = '\b'; break;
        case 'f':  cp = '\f'; break;
        case 'n':  cp = '\n'; break;
        case 'r':  cp = '\r'; break;
        case 't':  cp = '\t'; break;
        case 'u': {
            if (!hex4(i + 2, cp)) return false;
            if (cp >= 0xD800 && cp <= 0xDBFF) {          // a surrogate pair: one code point
                uint32_t lo = 0;
                if (i + 7 >= n || s[i + 6] != '\\' || s[i + 7] != 'u' || !hex4(i + 8, lo) ||
                    lo < 0xDC00 || lo > 0xDFFF) return false;
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                i += 6;
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                return false;                             // half a pair
            }
            if (cp == 0) return false;                    // a NUL would cut the file short
            i += 4;
            break;
        }
        default: return false;
        }
        if (!put(cp)) return false;
        i += 2;
    }
    out[w] = '\0';
    len = w;
    return true;
}

bool is_json_file(const char* name) {
    return name && strcmp(name, "last_track") != 0;
}

bool is_json_object(const char* s, size_t n) {
    size_t i = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
    if (i >= n || s[i] != '{') return false;
    // Without a token array jsmn only counts, which checks the syntax at no memory — but in that
    // mode it does not see a document cut short, so the brackets are counted here as well.
    int depth = 0;
    bool in_str = false, esc = false;
    for (size_t k = i; k < n; k++) {
        const char c = s[k];
        if (in_str) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') in_str = false;
        } else if (c == '"') {
            in_str = true;
        } else if (c == '{' || c == '[') {
            depth++;
        } else if (c == '}' || c == ']') {
            if (--depth < 0) return false;
        }
    }
    if (in_str || depth != 0) return false;
    jsmn_parser p;
    jsmn_init(&p);
    return jsmn_parse(&p, s, n, nullptr, 0) > 0;
}

size_t max_bytes(const char* name) {
    if (!name) return 0;
    // Each reader's buffer less its NUL. modesp_services depends on this component, and the
    // modules on both, so the constants are copied rather than included.
    static constexpr struct { const char* name; size_t max; } LIMITS[] = {
        {"eq.json",        4096 - 1},       // eq_config.cpp MAX_FILE, read as sizeof - 1
        {"stations.json",  24 * 1024 - 1},  // radio_module.cpp STATIONS_JSON_MAX, read as n - 1
        // TRACK_MAX as it was. The player keeps 255 bytes of it (last_dir_[256]) and writes
        // at most a Playlist::MAX_REL path; a longer one only fails to resume.
        {"last_track",     1024},
        {"devices.json",   4096 - 1},       // ConfigService::MAX_JSON_SIZE - 1, read_json_file
        {"eq_custom.json", 4096 - 1},       // as eq.json
    };
    for (const auto& l : LIMITS) {
        if (!strcmp(name, l.name)) return l.max;
    }
    return 0;
}

}  // namespace user_backup
}  // namespace modesp
