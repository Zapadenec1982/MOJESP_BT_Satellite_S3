/**
 * @file json_util.h
 * @brief jsmn helpers shared by radio_catalog.cpp and radio_art.cpp. Internal to the component.
 *
 * Header-only and in an anonymous namespace, so each source file gets its own copy, as with
 * jsmn itself (JSMN_STATIC).
 */

#pragma once

#define JSMN_STATIC          // jsmn is header-only and linked in several places
#include "jsmn.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

inline bool key_is(const char* json, const jsmntok_t& t, const char* name) {
    const int n = t.end - t.start;
    return t.type == JSMN_STRING && (int)strlen(name) == n &&
           !strncmp(json + t.start, name, (size_t)n);
}

/// How many tokens one value occupies, everything nested inside it included. jsmn lays them
/// out depth-first, so skipping a value is a matter of counting its children.
///
/// A loop, not recursion: the depth is whatever the body says, and these bodies come over
/// plain HTTP. A few hundred levels of '[' overflowed the 8 KB catalog task's stack and
/// rebooted the panel (deep review 2026-10-12, RP-2). Each token takes the place of one
/// child still owed and owes its own in turn; the value ends when nothing is owed.
inline int span(const jsmntok_t* tok, int count, int i) {
    if (i >= count) return 1;
    int j = i + 1;
    for (int pending = tok[i].size; pending > 0 && j < count; ++j) pending += tok[j].size - 1;
    return j - i;
}

inline int hex4(const char* p) {
    int v = 0;
    for (int i = 0; i < 4; i++) {
        const char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9')      v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else return -1;
    }
    return v;
}

/// Appends one code point as UTF-8, keeping room for the terminator. False if it does not fit.
inline bool put_utf8(uint32_t cp, char* out, size_t& o, size_t cap) {
    unsigned char b[4];
    size_t n;
    if (cp < 0x80) {
        b[0] = (unsigned char)cp; n = 1;
    } else if (cp < 0x800) {
        b[0] = (unsigned char)(0xC0 | (cp >> 6));
        b[1] = (unsigned char)(0x80 | (cp & 0x3F)); n = 2;
    } else if (cp < 0x10000) {
        b[0] = (unsigned char)(0xE0 | (cp >> 12));
        b[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        b[2] = (unsigned char)(0x80 | (cp & 0x3F)); n = 3;
    } else {
        b[0] = (unsigned char)(0xF0 | (cp >> 18));
        b[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
        b[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        b[3] = (unsigned char)(0x80 | (cp & 0x3F)); n = 4;
    }
    if (o + n >= cap) return false;
    memcpy(out + o, b, n);
    o += n;
    return true;
}

/// Copies a JSON string token, undoing every escape — \uXXXX and surrogate pairs included,
/// which Radio Browser does send now and then — and never cutting a UTF-8 sequence in half
/// when the text does not fit. Returns false if it had to cut.
inline bool copy_str(const char* json, const jsmntok_t& t, char* out, size_t cap) {
    if (!cap) return false;
    if (t.type != JSMN_STRING) {            // null, a number: no text, and not the word "null"
        out[0] = '\0';
        return true;
    }
    const char* p   = json + t.start;
    const char* end = json + t.end;
    size_t o = 0;
    bool whole = true;
    while (p < end) {
        const unsigned char c = (unsigned char)*p;
        if (c == '\\' && p + 1 < end) {
            uint32_t cp;
            size_t used = 2;
            switch (p[1]) {
            case 'n': cp = '\n'; break;
            case 't': cp = '\t'; break;
            case 'r': cp = '\r'; break;
            case 'b': cp = '\b'; break;
            case 'f': cp = '\f'; break;
            case 'u': {
                const int h = (end - p >= 6) ? hex4(p + 2) : -1;
                if (h < 0) { cp = 0xFFFD; break; }
                cp = (uint32_t)h;
                used = 6;
                if (cp >= 0xD800 && cp <= 0xDBFF) {          // high half: needs its low half
                    const int lo = (end - p >= 12 && p[6] == '\\' && p[7] == 'u') ? hex4(p + 8) : -1;
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (uint32_t)(lo - 0xDC00);
                        used = 12;
                    } else {
                        cp = 0xFFFD;
                    }
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    cp = 0xFFFD;                              // a low half on its own
                }
                break;
            }
            default: cp = (unsigned char)p[1]; break;          // \" \\ \/ and anything else
            }
            if (!put_utf8(cp, out, o, cap)) { whole = false; break; }
            p += used;
            continue;
        }
        // A raw byte: the whole UTF-8 sequence it starts, or none of it.
        size_t n = (c < 0x80) ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        if (n > (size_t)(end - p)) n = (size_t)(end - p);
        if (o + n >= cap) { whole = false; break; }
        memcpy(out + o, p, n);
        o += n;
        p += n;
    }
    out[o] = '\0';
    return whole;
}

inline uint32_t to_uint(const char* json, const jsmntok_t& t) {
    if (t.type != JSMN_PRIMITIVE) return 0;
    const char c = json[t.start];
    if (c == 't') return 1;                                  // true
    if (c < '0' || c > '9') return 0;                        // null, false, negative
    return (uint32_t)strtoul(json + t.start, nullptr, 10);
}

/// Runs jsmn into the caller's scratch. Returns the token count, or a negative jsmn error.
inline int tokenize(const char* json, size_t len, void* scratch, size_t scratch_sz) {
    if (!json || !scratch) return -1;
    jsmn_parser p;
    jsmn_init(&p);
    return jsmn_parse(&p, json, len, static_cast<jsmntok_t*>(scratch),
                      (unsigned)(scratch_sz / sizeof(jsmntok_t)));
}

/// Looks up `name` among an object's keys. Returns the value's index, or −1.
inline int field(const char* json, const jsmntok_t* tok, int count, int obj, const char* name) {
    if (obj < 0 || obj >= count || tok[obj].type != JSMN_OBJECT) return -1;
    int j = obj + 1;
    for (int k = 0; k < tok[obj].size && j + 1 < count; k++) {
        if (key_is(json, tok[j], name)) return j + 1;
        j += 1 + span(tok, count, j + 1);
    }
    return -1;
}

/// Calls each(object_index) for every object in the array at `arr`.
template <class Each>
bool for_each_object_in(const jsmntok_t* tok, int n, int arr, Each each) {
    if (arr < 0 || arr >= n || tok[arr].type != JSMN_ARRAY) return false;
    int i = arr + 1;
    for (int k = 0; k < tok[arr].size && i < n; k++) {
        const int obj = i;
        i += span(tok, n, obj);
        if (tok[obj].type == JSMN_OBJECT) each(obj);
    }
    return true;
}

}  // namespace
