/**
 * @file test_i18n.cpp
 * @brief The panel's strings in four languages: modules/ui_lvgl/include/i18n.h.
 *
 * The plural rules are checked against what the web's Intl.PluralRules says for the same
 * counts (Node 22, CLDR 48), so the glass and the browser pick the same form of the same key.
 * fmt() is held to the web's fmt(): a {name} with no value stays as it is written. Its cut is
 * held to modesp/utf8.h: never inside a letter.
 *
 * The tables are the real generated ones: the shell's strings (ui_shell.cpp) are in them.
 */

#include "doctest.h"
#include "../../modules/ui_lvgl/include/i18n.h"

#include <cstring>
#include <string>

using i18n::Form;
using i18n::Lang;
using i18n::S;

namespace {

/// The language is global; every test leaves it Ukrainian, as the panel boots.
struct LangGuard {
    explicit LangGuard(Lang l) { i18n::set_lang(l); }
    ~LangGuard() { i18n::set_lang(Lang::UK); }
};

std::string fmt(const char* pattern, std::initializer_list<i18n::Arg> args, size_t cap = 128) {
    std::string buf(cap, '\x7f');
    const size_t n = i18n::fmt(&buf[0], cap, pattern, args);
    CHECK(n == strlen(buf.c_str()));
    return std::string(buf.c_str());
}

std::string num(int32_t v, int d, bool plus = false) {
    char buf[24];
    return i18n::num(buf, sizeof(buf), v, d, plus);
}

}  // namespace

TEST_CASE("plural forms are CLDR's, as Intl.PluralRules gives them") {
    struct Case { int32_t n; Form uk, pl, en; };
    const Form O = Form::ONE, F = Form::FEW, M = Form::MANY, X = Form::OTHER;
    const Case cases[] = {
        {0, M, M, X},    {1, O, O, O},    {2, F, F, X},    {4, F, F, X},     {5, M, M, X},
        {10, M, M, X},   {11, M, M, X},   {12, M, M, X},   {14, M, M, X},    {19, M, M, X},
        {20, M, M, X},   {21, O, M, X},   {22, F, F, X},   {24, F, F, X},    {25, M, M, X},
        {100, M, M, X},  {101, O, M, X},  {102, F, F, X},  {111, M, M, X},   {112, M, M, X},
        {114, M, M, X},  {121, O, M, X},  {122, F, F, X},  {1000, M, M, X},  {1001, O, M, X},
        {1002, F, F, X}, {1000000, M, M, X},
    };
    for (const Case& c : cases) {
        CAPTURE(c.n);
        CHECK(i18n::plural_form(Lang::UK, c.n) == c.uk);
        CHECK(i18n::plural_form(Lang::PL, c.n) == c.pl);
        CHECK(i18n::plural_form(Lang::EN, c.n) == c.en);
        CHECK(i18n::plural_form(Lang::DE, c.n) == c.en);    // German counts as English does
    }
    // A sign changes nothing (Intl.PluralRules agrees), and the extremes do not overflow.
    CHECK(i18n::plural_form(Lang::UK, -21) == Form::ONE);
    CHECK(i18n::plural_form(Lang::PL, -2) == Form::FEW);
    CHECK(i18n::plural_form(Lang::UK, INT32_MIN) == Form::MANY);   // 2147483648: %10 = 8
}

TEST_CASE("the language: four of them, ui.lang's numbers, Ukrainian at boot") {
    CHECK(i18n::lang() == Lang::UK);
    CHECK_FALSE(i18n::set_lang_index(-1));
    CHECK_FALSE(i18n::set_lang_index(4));
    CHECK(i18n::lang() == Lang::UK);
    {
        LangGuard g(Lang::UK);
        REQUIRE(i18n::set_lang_index(3));
        CHECK(i18n::lang() == Lang::PL);
    }
    CHECK(i18n::lang() == Lang::UK);
}

TEST_CASE("each language by its own name, whatever language is on") {
    LangGuard g(Lang::DE);
    CHECK(std::string(i18n::lang_name(Lang::UK)) == "Українська");
    CHECK(std::string(i18n::lang_name(Lang::EN)) == "English");
    CHECK(std::string(i18n::lang_name(Lang::DE)) == "Deutsch");
    CHECK(std::string(i18n::lang_name(Lang::PL)) == "Polski");
    CHECK(std::string(i18n::lang_name(Lang::COUNT)) == "");
}

TEST_CASE("tr: the generated tables, by language") {
    CHECK(std::string(i18n::tr(S::nav_player)) == "Плеєр");
    CHECK(std::string(i18n::tr(S::pair_phone)) == "Телефон");
    {
        LangGuard g(Lang::EN);
        CHECK(std::string(i18n::tr(S::nav_player)) == "Player");
    }
    {
        LangGuard g(Lang::DE);
        CHECK(std::string(i18n::tr(S::page_wifi)) == "WLAN");
    }
    {
        LangGuard g(Lang::PL);
        CHECK(std::string(i18n::tr(S::nav_player)) == "Odtwarzacz");
    }
    // Out of range is "", never null and never someone else's string.
    CHECK(std::string(i18n::tr(S::COUNT)) == "");
    CHECK(std::string(i18n::tr((S)0xFFFF)) == "");
    CHECK(std::string(i18n::plural(i18n::P::COUNT, 3)) == "");
}

TEST_CASE("tr: every string of every language is there and none is empty") {
    for (int l = 0; l < (int)Lang::COUNT; l++) {
        LangGuard g((Lang)l);
        for (int i = 0; i < (int)S::COUNT; i++) {
            CAPTURE(l);
            CAPTURE(i);
            CHECK(strlen(i18n::tr((S)i)) > 0);
        }
    }
}

TEST_CASE("fmt: {name} by value, as the web's fmt()") {
    CHECK(fmt("Спарено з «{name}»", {{"name", "Pixel 8"}}) == "Спарено з «Pixel 8»");
    CHECK(fmt("{n} з {total}", {{"total", "9"}, {"n", "3"}}) == "3 з 9");
    CHECK(fmt("{n}{n}", {{"n", "ab"}}) == "abab");
    CHECK(fmt("no names", {}) == "no names");
    CHECK(fmt("", {{"n", "1"}}) == "");
    CHECK(fmt(nullptr, {}) == "");
}

TEST_CASE("fmt: what names nothing is left as written") {
    CHECK(fmt("{gone} here", {{"n", "1"}}) == "{gone} here");      // a missing value shows
    CHECK(fmt("{n", {{"n", "1"}}) == "{n");                        // not closed
    CHECK(fmt("{} {-x} {n}", {{"n", "1"}}) == "{} {-x} 1");         // not a name
    CHECK(fmt("{{n}}", {{"n", "1"}}) == "{1}");
    CHECK(fmt("a {n} b", {{"n", nullptr}}) == "a  b");              // a null value is ""
    CHECK(fmt("{name}", {{"nam", "x"}, {"name", "y"}}) == "y");     // whole names only
}

TEST_CASE("fmt: the cut is on a character boundary, and nothing follows it") {
    // Cyrillic is two bytes a letter: 14 bytes of room hold "Спарено", 13 cut inside its "о",
    // and the "о" goes whole.
    CHECK(fmt("Спарено з «{name}»", {{"name", "x"}}, 15) == "Спарено");
    CHECK(fmt("Спарено з «{name}»", {{"name", "x"}}, 14) == "Спарен");
    // The value is cut the same way, and the text after it does not slip in behind the gap.
    CHECK(fmt("{name}!", {{"name", "Łódź"}}, 5) == "Łó");
    CHECK(fmt("{name}!", {{"name", "Łódź"}}, 4) == "Ł");
    CHECK(fmt("ab{n}cd", {{"n", "ЖЖ"}}, 5) == "abЖ");
    // cap 1 is the NUL alone; cap 0 writes nothing at all.
    CHECK(fmt("abc", {}, 1) == "");
    char sentinel[2] = {'q', '\0'};
    CHECK(i18n::fmt(sentinel, 0, "abc", {}) == 0);
    CHECK(sentinel[0] == 'q');
    CHECK(i18n::fmt(nullptr, 8, "abc", {}) == 0);
}

TEST_CASE("num: the language's decimal separator, the minus sign, printf's plus") {
    CHECK(num(35, 1) == "3,5");
    CHECK(num(-35, 1) == "−3,5");                 // U+2212, as the pages print it
    CHECK(num(-5, 1) == "−0,5");                  // the sign of a value under one survives
    CHECK(num(35, 1, true) == "+3,5");
    CHECK(num(0, 1, true) == "+0,0");             // "%+.1f" of zero
    CHECK(num(1234, 2) == "12,34");
    CHECK(num(5, 2) == "0,05");
    CHECK(num(42, 0) == "42");
    CHECK(num(-42, 0, true) == "−42");
    CHECK(num(7, 5) == "0,007");                   // decimals beyond 3 are 3
    CHECK(num(INT32_MIN, 0) == "−2147483648");
    CHECK(i18n::decimal_sep() == ',');
    {
        LangGuard g(Lang::EN);
        CHECK(i18n::decimal_sep() == '.');
        CHECK(num(-35, 1) == "−3.5");
    }
    {
        LangGuard g(Lang::DE);
        CHECK(num(35, 1) == "3,5");
    }
    {
        LangGuard g(Lang::PL);
        CHECK(num(35, 1) == "3,5");
    }
}

namespace {

std::string badge(const char* raw, size_t cap = 64) {
    std::string buf(cap, '\x7f');
    const size_t n = i18n::badge(&buf[0], cap, raw);
    CHECK(n == strlen(buf.c_str()));
    return std::string(buf.c_str());
}

}  // namespace

TEST_CASE("badge: a technical line in the language's units, as the web's badge() says it") {
    // The same lines as tools/tests/test_webui_numbers.py: the glass and the web agree.
    CHECK(badge("44.1 kHz 2 ch 128 kbps") == "44,1 кГц 128 кбіт/с");     // the radio: stereo unsaid
    CHECK(badge("16/44.1 kHz 2 ch") == "16/44,1 кГц");                   // the card: bits/rate
    CHECK(badge("16/48.0 kHz 1 ch") == "16/48 кГц 1 кан.");              // no ",0"; mono is said
    CHECK(badge("LHDC V5 96.0 kHz") == "LHDC V5 96 кГц");                // Bluetooth: the codec stays
    CHECK(badge("48.0 kHz 24 bit") == "48 кГц 24 біт");                  // USB
    CHECK(badge("1000 Hz") == "1000 Гц");
    CHECK(badge("AirPlay") == "AirPlay");
    CHECK(badge("AAC kHz") == "AAC kHz");                                 // a unit after a word is a word
    CHECK(badge("") == "");
    CHECK(badge(nullptr) == "");
    {
        LangGuard g(Lang::EN);
        CHECK(badge("44.1 kHz 2 ch 128 kbps") == "44.1 kHz 128 kbit/s");
    }
    // The cut is whole letters, never half of one: room for 6 bytes ends before «к», for 7 after it.
    CHECK(badge("44.1 kHz", 7) == "44,1 ");
    CHECK(badge("44.1 kHz", 8) == "44,1 к");
    char sentinel[2] = {'q', '\0'};
    CHECK(i18n::badge(sentinel, 0, "44.1 kHz") == 0);
    CHECK(sentinel[0] == 'q');
}

namespace {

std::string render(const char* m, size_t cap = 160) {
    std::string buf(cap, '\x7f');
    const size_t n = i18n::render(&buf[0], cap, m);
    CHECK(n == strlen(buf.c_str()));
    return std::string(buf.c_str());
}

}  // namespace

TEST_CASE("render: a module's message in the panel's language, anything else as it is") {
    // The generated table has the modules' messages (dlna_browser.cpp, audio_module.cpp).
    CHECK(render("@cal.m_progress" I18N_MSG_ARG "hz=63" I18N_MSG_ARG "k=3" I18N_MSG_ARG "n=40") ==
          "Вимір: 63 Гц (3 з 40)");
    CHECK(render("@dlna.servers") == "Медіасервери");
    CHECK(render("@dlna.st_playing" I18N_MSG_ARG "title=a=b") == "Грає: a=b");    // "=" in a value
    {
        LangGuard g(Lang::PL);
        CHECK(render("@cal.m_found" I18N_MSG_ARG "hz=63") == "Znaleziono: 63 Hz");
    }
    // Not a message: shown as it is, the web's msg() the same.
    CHECK(render("@dlna.nothing" I18N_MSG_ARG "n=1") == "@dlna.nothing" I18N_MSG_ARG "n=1");
    CHECK(render("@Moja muzyka") == "@Moja muzyka");
    CHECK(render("@dlna") == "@dlna");
    CHECK(render("@dlna.servers and more") == "@dlna.servers and more");
    CHECK(render("Медіасервери") == "Медіасервери");
    CHECK(render("{name}") == "{name}");
    CHECK(render(nullptr) == "");
    // A missing argument stays as written; the cut is whole letters.
    CHECK(render("@cal.m_found") == "Знайдено: {hz} Гц");
    CHECK(render("@dlna.servers", 7) == "Мед");
}
