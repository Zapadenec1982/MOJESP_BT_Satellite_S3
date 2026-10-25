/**
 * @file i18n.h
 * @brief The panel's strings in four languages.
 *
 * The translations are the web UI's: webui/src/i18n/<lang>.json, one key set for the glass and
 * the browser. tools/i18n/gen_panel_strings.py turns the keys the panel's code names into the
 * ids of i18n_keys.h and the tables of src/i18n_tables.cpp — const data, in flash. What lives in
 * RAM is the one byte that says which language is on.
 *
 * Writers and readers: the language is set by the main task under the LVGL lock, when the
 * screen is about to be rebuilt in it, and read by whoever draws text. One byte, so a reader
 * sees the old language or the new one and never a mix inside a string.
 *
 * Header and runtime are free of IDF and LVGL, so the host tests (tests/host/test_i18n.cpp)
 * compile them as they are.
 */
#pragma once

#include "i18n_keys.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace i18n {

/// The order of ui.lang (0..3) and of LANGS in webui/src/stores/i18n.js.
enum class Lang : uint8_t { UK, EN, DE, PL, COUNT };

/// CLDR's plural categories, in the order the tables keep a count's four forms.
enum class Form : uint8_t { ONE, FEW, MANY, OTHER };

void set_lang(Lang l);
/// ui.lang's number; false, and nothing changes, when it names no language.
bool set_lang_index(int32_t index);
Lang lang();

/// A language by its own name — "Українська", "English", "Deutsch", "Polski" — whatever
/// language is on: a person looks for the one they read (LANG_NAMES in the web, the same).
const char* lang_name(Lang l);

/// The string in the language that is on. Never null: an id out of range is "".
const char* tr(S id);
const char* tr(SU id);

/// The category CLDR 48 gives an integer count `n` in `l` (the sign does not matter):
///   uk  one  n%10 = 1 and n%100 != 11;  few  n%10 in 2..4 and n%100 not in 12..14;  many  the rest
///   pl  one  n = 1;                       few  as uk;                                many  the rest
///   en, de  one  n = 1;  other  the rest
/// OTHER in uk and pl is for fractions, which the panel does not count.
Form plural_form(Lang l, int32_t n);

/// The form of a count that `n` takes in the language that is on — "{n} треки", "{n} Titel".
/// The {n} in it is not filled here; fmt() does that.
const char* plural(P id, int32_t n);

/// A named value for fmt(). A null value is "".
struct Arg {
    const char* name;
    const char* value;
};

/// `pattern` with each {name} replaced by its value: the catalogs' placeholders, the same as the
/// web's fmt(). A {name} with no value is copied as it stands, so a missing argument shows instead
/// of vanishing. `out` is always NUL-terminated when `cap` > 0 and is cut on a character
/// boundary, never inside a UTF-8 letter. Returns the bytes written, without the NUL.
size_t fmt(char* out, size_t cap, const char* pattern, const Arg* args, size_t n_args);
inline size_t fmt(char* out, size_t cap, const char* pattern, std::initializer_list<Arg> args) {
    return fmt(out, cap, pattern, args.begin(), args.size());
}

/// The separator between a message and its arguments (tools/i18n/gen_panel_strings.py):
/// "@cal.m_done" MSG_ARG "hz=63". A module that publishes messages defines the same macro.
#define I18N_MSG_ARG "\x1f"

/// A module's message — "@<key>", then name=value arguments each behind I18N_MSG_ARG — in the
/// panel's language, or `msg` as it is when it is not one: a name from the network that happens
/// to start with "@", an older firmware's text. Only a key the catalog has makes a message, the
/// same rule as the web's msg(). The cut is on a character boundary, as fmt()'s.
size_t render(char* out, size_t cap, const char* msg);

/// The decimal separator of the language that is on: a comma in uk, de and pl, a point in en —
/// as Intl.NumberFormat writes it in the web.
char decimal_sep();

/// A fixed-point number: `scaled` / 10^`decimals` (`decimals` 0..3), with the language's
/// separator, the minus sign (U+2212, three bytes) before a negative and, with `plus`, "+" before
/// the rest — printf's "%+.1f" as the pages print it. Returns `out`.
const char* num(char* out, size_t cap, int32_t scaled, int decimals, bool plus = false);

/// A technical line in the language's units and decimals — the codec badge ("44.1 kHz 2 ch
/// 128 kbps", "16/44.1 kHz", "LDAC 96.0 kHz"), USB's "48.0 kHz 24 bit" — as the web's badge()
/// (webui/src/lib/numbers.js) writes it: «44,1 кГц 128 кбіт/с». Two channels go unsaid and a
/// whole number loses its ",0"; a word that is not a number before its unit stays as it is.
/// Cut on a character boundary, as fmt()'s. Returns the bytes written, without the NUL.
size_t badge(char* out, size_t cap, const char* raw);

namespace detail {

/// One language's strings: offsets into `text` for S, SU, and four per P (one, few, many, other).
struct Table {
    const char*     text;
    const uint16_t* s;
    const uint16_t* su;
    const uint16_t* p;
};

extern const Table TABLES[(int)Lang::COUNT];   // src/i18n_tables.cpp, generated

/// A key a module may publish as a message, and its string.
struct Message {
    const char* key;
    S           id;
};
extern const Message MESSAGES[];                // sorted by key; MESSAGE_COUNT of them

}  // namespace detail
}  // namespace i18n
