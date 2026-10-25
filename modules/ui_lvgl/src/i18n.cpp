/**
 * @file i18n.cpp
 * @brief The language that is on, and what the tables are read through. See i18n.h.
 */

#include "i18n.h"
#include "modesp/utf8.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace i18n {
namespace {

std::atomic<uint8_t> s_lang{(uint8_t)Lang::UK};

const detail::Table& table() { return detail::TABLES[s_lang.load(std::memory_order_relaxed)]; }

bool name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

}  // namespace

void set_lang(Lang l) {
    if (l < Lang::COUNT) s_lang.store((uint8_t)l, std::memory_order_relaxed);
}

bool set_lang_index(int32_t index) {
    if (index < 0 || index >= (int32_t)Lang::COUNT) return false;
    set_lang((Lang)index);
    return true;
}

Lang lang() { return (Lang)s_lang.load(std::memory_order_relaxed); }

const char* lang_name(Lang l) {
    // Not translations, so not in the catalogs: each name is spelled in its own language.
    static constexpr const char* NAMES[] = {"Українська", "English", "Deutsch", "Polski"};
    static_assert(sizeof(NAMES) / sizeof(NAMES[0]) == (size_t)Lang::COUNT, "a name per language");
    return l < Lang::COUNT ? NAMES[(int)l] : "";
}

const char* tr(S id) {
    if ((uint16_t)id >= (uint16_t)S::COUNT) return "";
    const detail::Table& t = table();
    return t.text + t.s[(uint16_t)id];
}

const char* tr(SU id) {
    if ((uint16_t)id >= (uint16_t)SU::COUNT) return "";
    const detail::Table& t = table();
    return t.text + t.su[(uint16_t)id];
}

Form plural_form(Lang l, int32_t n) {
    const uint32_t a = n < 0 ? 0u - (uint32_t)n : (uint32_t)n;
    const uint32_t m10 = a % 10, m100 = a % 100;
    const bool few = m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14);
    switch (l) {
    case Lang::UK:
        if (m10 == 1 && m100 != 11) return Form::ONE;
        return few ? Form::FEW : Form::MANY;
    case Lang::PL:
        if (a == 1) return Form::ONE;
        return few ? Form::FEW : Form::MANY;
    default:
        return a == 1 ? Form::ONE : Form::OTHER;
    }
}

const char* plural(P id, int32_t n) {
    if ((uint16_t)id >= (uint16_t)P::COUNT) return "";
    const detail::Table& t = table();
    return t.text + t.p[(uint16_t)id * 4 + (uint8_t)plural_form(lang(), n)];
}

size_t fmt(char* out, size_t cap, const char* pattern, const Arg* args, size_t n_args) {
    if (!out || cap == 0) return 0;
    size_t n = 0;
    bool full = false;
    // Appends what fits; the first piece that does not is cut on a character boundary and
    // ends the string, so a later short piece cannot slip in after a gap.
    auto put = [&](const char* s, size_t len) {
        if (full) return;
        const size_t room = cap - 1 - n;
        if (len > room) {
            len = modesp::utf8_fit(s, len, room);
            full = true;
        }
        memcpy(out + n, s, len);
        n += len;
    };
    const char* p = pattern ? pattern : "";
    while (*p && !full) {
        if (*p == '{') {
            const char* e = p + 1;
            while (name_char(*e)) e++;
            if (*e == '}' && e > p + 1) {
                const size_t len = (size_t)(e - p - 1);
                const Arg* hit = nullptr;
                for (size_t i = 0; i < n_args && args; i++) {
                    const Arg& a = args[i];
                    if (a.name && strlen(a.name) == len && memcmp(a.name, p + 1, len) == 0) { hit = &a; break; }
                }
                if (hit) {
                    const char* v = hit->value ? hit->value : "";
                    put(v, strlen(v));
                    p = e + 1;
                    continue;
                }
            }
        }
        // A run of plain text, up to the next '{' (a '{' that named nothing is part of it).
        const char* q = p + 1;
        while (*q && *q != '{') q++;
        put(p, (size_t)(q - p));
        p = q;
    }
    out[n] = '\0';
    return n;
}

namespace {

/// The message for `key` (`len` bytes, not terminated), or nullptr when the catalog has none.
const detail::Message* find_message(const char* key, size_t len) {
    int lo = 0, hi = (int)MESSAGE_COUNT - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const char* k = detail::MESSAGES[mid].key;
        int c = strncmp(k, key, len);
        if (c == 0 && k[len] != '\0') c = 1;          // k is longer: it sorts after
        if (c == 0) return &detail::MESSAGES[mid];
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return nullptr;
}

/// "@" and a key of the catalog's shape: a lower-case word, then one or more ".word".
size_t key_length(const char* msg) {
    if (msg[0] != '@' || !(msg[1] >= 'a' && msg[1] <= 'z')) return 0;
    size_t i = 1, dots = 0;
    for (;; i++) {
        const char c = msg[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') continue;
        if (c == '.' && msg[i + 1] && msg[i + 1] != '.' && msg[i + 1] != '\x1f') { dots++; continue; }
        break;
    }
    if (!dots || (msg[i] != '\0' && msg[i] != '\x1f')) return 0;
    return i - 1;
}

}  // namespace

size_t render(char* out, size_t cap, const char* msg) {
    if (!out || cap == 0) return 0;
    if (!msg) msg = "";
    const size_t klen = key_length(msg);
    const detail::Message* m = klen ? find_message(msg + 1, klen) : nullptr;
    if (!m) return fmt(out, cap, "{s}", {{"s", msg}});         // as it is, cut whole
    // The arguments, split in a copy: each separator ends the value before it and starts a
    // name=value piece; a value may hold "=" itself, only the first one splits.
    char buf[160];
    const char* rest = msg + 1 + klen;
    modesp::utf8_copy(buf, sizeof(buf), rest, strlen(rest));
    Arg args[6];
    size_t n = 0;
    for (char* p = buf; *p == '\x1f' && n < sizeof(args) / sizeof(args[0]);) {
        *p++ = '\0';
        char* next = strchr(p, '\x1f');
        char* eq = strchr(p, '=');
        if (eq && (!next || eq < next)) {
            *eq = '\0';
            args[n++] = Arg{p, eq + 1};
        }
        if (!next) break;
        p = next;
    }
    return fmt(out, cap, tr(m->id), args, n);
}

char decimal_sep() { return lang() == Lang::EN ? '.' : ','; }

const char* num(char* out, size_t cap, int32_t scaled, int decimals, bool plus) {
    if (!out || cap == 0) return out;
    static constexpr int32_t POW10[] = {1, 10, 100, 1000};
    if (decimals < 0) decimals = 0;
    if (decimals > 3) decimals = 3;
    const int64_t v = scaled;
    const uint64_t a = (uint64_t)(v < 0 ? -v : v);
    const uint64_t whole = a / (uint64_t)POW10[decimals];
    const uint64_t frac  = a % (uint64_t)POW10[decimals];
    const char* sign = v < 0 ? "\xE2\x88\x92" : (plus ? "+" : "");   // U+2212, the minus
    if (decimals == 0) {
        snprintf(out, cap, "%s%llu", sign, (unsigned long long)whole);
    } else {
        snprintf(out, cap, "%s%llu%c%0*llu", sign, (unsigned long long)whole, decimal_sep(),
                 decimals, (unsigned long long)frac);
    }
    return out;
}

size_t badge(char* out, size_t cap, const char* raw) {
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!raw) return 0;
    struct Unit {
        const char* word;
        S           id;
    };
    static constexpr Unit UNITS[] = {{"kHz", S::unit_khz}, {"Hz", S::unit_hz}, {"kbps", S::unit_kbps},
                                     {"bit", S::unit_bit}, {"ch", S::unit_ch}};
    size_t len = 0;
    auto put = [&](const char* s, size_t n) { len += modesp::utf8_copy(out + len, cap - len, s, n); };
    auto word = [&](const char* s, size_t n) {
        if (len) put(" ", 1);
        put(s, n);
    };
    const char* p = raw;
    while (*p) {
        const char* w = p;                                  // this word
        while (*p && *p != ' ') p++;
        const size_t wn = (size_t)(p - w);
        const char* u = *p ? p + 1 : p;                     // the next
        const char* q = u;
        while (*q && *q != ' ') q++;
        const size_t un = (size_t)(q - u);
        const Unit* unit = nullptr;
        for (const Unit& x : UNITS)
            if (strlen(x.word) == un && memcmp(x.word, u, un) == 0) unit = &x;
        // a number: [digits "/"] digits ["." digits] — "16/44.1"
        auto digits = [&](size_t k) { while (k < wn && w[k] >= '0' && w[k] <= '9') k++; return k; };
        size_t from = 0, to = digits(0), frac = 0, end = 0;
        if (to > 0 && to < wn && w[to] == '/') { from = to + 1; to = digits(from); }
        bool number = unit && to > from;
        if (number && to < wn) {
            end = digits(to + 1);
            number = w[to] == '.' && end > to + 1 && end == wn;
            frac = to + 1;
        }
        bool tenths = false;                                // a fraction that says something
        for (size_t k = frac; frac && k < end; k++) tenths |= w[k] != '0';
        if (!number) {
            word(w, wn);
            if (*p) p++;
            continue;
        }
        p = *q ? q + 1 : q;
        const bool stereo = unit->id == S::unit_ch && from == 0 && to == 1 && w[0] == '2' && !tenths;
        if (stereo) continue;                               // the norm, and the room the artist needs
        word(w, to);
        if (tenths) {
            const char sep = decimal_sep();
            put(&sep, 1);
            put(w + frac, end - frac);
        }
        word(tr(unit->id), strlen(tr(unit->id)));
    }
    return len;
}

}  // namespace i18n
