#!/usr/bin/env python3
"""Generate the panel's string tables from the web UI's catalogs (docs/PLAN_PANEL_LANG_2026-10-12.md §3).

One source of translations for the glass and the web: webui/src/i18n/<lang>.json, flat keys, the
same set in each (tools/tests/test_webui_i18n.py). The panel's code names a string by its key with
'.' spelled '_', and this script turns the keys the code names into C++:

    S::<id>    one string                         i18n::tr(S::nav_player)
    SU::<id>   the same string in capitals         i18n::tr(SU::svc_audio) — made here with
                                                   str.upper() ("ß" -> "SS"), not on the device
    P::<id>    a count in words: <key>.one|few|many|other, picked by i18n::plural()

Modules that publish text the panel and the web show — a status line, a progress — publish a
message instead: "@<key>", its arguments after it as name=value, each behind U+001F:

    state_set("audio.port_meas_status", "@cal.m_done" MSG_ARG "hz=63");    // MSG_ARG is "\x1f"

The script finds every "@<key>" literal in the modules listed in SCAN_MSG, checks the key and the
names of its arguments against the catalog, and gives the panel a table to look the key up at
run time (i18n::render). The web reads the same format (its msg()).

Only the keys the code names go into flash. Output, committed like the fonts and the country
table, so the firmware build needs no step of its own:

    modules/ui_lvgl/include/i18n_keys.h    the enums
    modules/ui_lvgl/src/i18n_tables.cpp    per language: one char array of NUL-separated strings
                                           and uint16_t offsets into it — const data, so it stays
                                           in flash; no pointer table that could land in RAM

    python tools/i18n/gen_panel_strings.py           # write both files
    python tools/i18n/gen_panel_strings.py --check   # exit 1 if they are stale

Re-run it after naming a new key in the code or editing a catalog; tools/tests/test_panel_i18n.py
fails while the files are stale.
"""

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CATALOGS = ROOT / "webui" / "src" / "i18n"
# The order is ui.lang's 0..3 and LANGS in webui/src/stores/i18n.js. Ukrainian first: it is the
# language every other one falls back to.
LANGS = ("uk", "en", "de", "pl")
PLURAL_FORMS = ("one", "few", "many", "other")     # i18n::Form, in this order
# Where the panel's code is looked for. Generated files are skipped: they name every id.
SCAN = (ROOT / "modules" / "ui_lvgl",)
# Where messages ("@<key>" literals) are published from; ui_lvgl reads them.
SCAN_MSG = (ROOT / "modules" / "airplay", ROOT / "modules" / "audio", ROOT / "components" / "dlna_client",
            ROOT / "modules" / "ui_lvgl")
OUT_H = ROOT / "modules" / "ui_lvgl" / "include" / "i18n_keys.h"
OUT_CPP = ROOT / "modules" / "ui_lvgl" / "src" / "i18n_tables.cpp"
GENERATED = (OUT_H, OUT_CPP)

USE = re.compile(r"\b(S|SU|P)::([a-z][a-z0-9_]*)\b")
MSG = re.compile(r"^@([a-z][a-z0-9_.]*)$")          # a message literal: exactly "@<key>"
MSG_ARG = re.compile(r"^\s*MSG_ARG\s*$")             # what may stand between a message and its arguments
ARG = re.compile(r"^(\w+)=")


class GenError(Exception):
    pass


# ── C++ source, read just enough ──

def cpp_tokens(text):
    """Split C++ source into ('code', s), ('comment', s) and ('string', body) pieces.

    Enough of the lexer for this job: line and block comments, character literals, ordinary
    and raw string literals. Escapes inside a string are kept as written.
    """
    out, i, n, code = [], 0, len(text), []

    def flush():
        if code:
            out.append(("code", "".join(code)))
            code.clear()

    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            flush(); out.append(("comment", text[i:j])); i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            flush(); out.append(("comment", text[i:j])); i = j
        elif c == "'":
            j = i + 1
            while j < n and text[j] != "'":
                j += 2 if text[j] == "\\" else 1
            code.append(text[i:j + 1]); i = j + 1
        elif c == '"':
            raw = re.match(r'R"([^(\s]*)\(', text[i - 1:i + 20]) if i > 0 and text[i - 1] == "R" else None
            if raw:
                end = text.find(")" + raw.group(1) + '"', i)
                body = text[i + 1 + len(raw.group(1)) + 1:end]
                flush(); out.append(("string", body)); i = end + len(raw.group(1)) + 2
                continue
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            flush(); out.append(("string", text[i + 1:j])); i = j + 1
        else:
            code.append(c); i += 1
    flush()
    return out


def sources(bases=SCAN):
    for base in bases:
        for p in sorted(base.rglob("*")):
            if p.suffix in (".cpp", ".h", ".hpp", ".c") and p.is_file() and p not in GENERATED \
                    and "fonts" not in p.relative_to(base).parts:
                yield p


def _rel(p):
    try:
        return str(Path(p).relative_to(ROOT))
    except ValueError:
        return str(p)


def scan_messages(tokens, rel, out):
    """"@<key>" literals and the argument names that follow them: "@k" MSG_ARG "hz=%d" MSG_ARG ..."""
    for i, (kind, piece) in enumerate(tokens):
        m = MSG.match(piece) if kind == "string" else None
        if not m:
            continue
        entry = out.setdefault(m.group(1), {"files": [], "args": []})
        entry["files"].append(rel)
        names, j = set(), i + 1
        while j + 1 < len(tokens) and tokens[j][0] == "code" and MSG_ARG.match(tokens[j][1]) \
                and tokens[j + 1][0] == "string":
            a = ARG.match(tokens[j + 1][1])
            if a:
                names.add(a.group(1))
            j += 2
        entry["args"].append(names)


def scan_uses(files=None):
    """{'S': {id: [file, ...]}, 'SU': ..., 'P': ..., 'M': {key: {files, args}}} — the ids the code
    names outside comments and string literals, and the messages it publishes. `files` defaults to
    every source under SCAN, and under SCAN_MSG for the messages."""
    uses = {"S": {}, "SU": {}, "P": {}, "M": {}}
    for p in sources() if files is None else files:
        tokens = cpp_tokens(p.read_text(encoding="utf-8"))
        for kind, piece in tokens:
            if kind != "code":
                continue
            for m in USE.finditer(piece):
                uses[m.group(1)].setdefault(m.group(2), []).append(_rel(p))
        if files is not None:
            scan_messages(tokens, _rel(p), uses["M"])
    if files is None:
        for p in sources(SCAN_MSG):
            scan_messages(cpp_tokens(p.read_text(encoding="utf-8")), _rel(p), uses["M"])
    return uses


# ── the catalogs ──

def load_catalogs(directory=CATALOGS):
    return {lang: json.loads((directory / f"{lang}.json").read_text(encoding="utf-8")) for lang in LANGS}


def ident(key):
    return key.replace(".", "_")


def resolve(cats, uses):
    """The keys behind the ids: {'S': [key...], 'SU': [key...], 'P': [base...]}, each sorted."""
    uk = cats["uk"]
    by_id = {}
    for key in uk:
        other = by_id.setdefault(ident(key), key)
        if other != key:
            raise GenError(f"keys '{other}' and '{key}' would both be {ident(key)}: rename one")
    bases = {}
    for key in uk:
        base, _, form = key.rpartition(".")
        if form in PLURAL_FORMS:
            bases.setdefault(ident(base), base)
    model = {"S": [], "SU": [], "P": [], "M": []}
    for kind in ("S", "SU"):
        for i, files in sorted(uses[kind].items()):
            if i not in by_id:
                raise GenError(f"{kind}::{i} ({files[0]}): no key '{i.replace('_', '.')}' (or similar) in uk.json")
            model[kind].append(by_id[i])
    for i, files in sorted(uses["P"].items()):
        base = bases.get(i)
        if base is None:
            raise GenError(f"P::{i} ({files[0]}): uk.json has no <key>.one/.few/.many/.other for it")
        missing = [f for f in PLURAL_FORMS if f"{base}.{f}" not in uk]
        if missing:
            raise GenError(f"P::{i}: '{base}' lacks the forms {missing}")
        model["P"].append(base)
    placeholder = re.compile(r"\{(\w+)\}")
    for key, use in sorted(uses.get("M", {}).items()):
        if key not in uk:
            raise GenError(f"message @{key} ({use['files'][0]}): no such key in uk.json")
        want = set(placeholder.findall(uk[key]))
        for got in use["args"]:
            if got != want:
                raise GenError(f"message @{key} ({use['files'][0]}): arguments {sorted(got)}, "
                               f"the catalog's text has {sorted(want)}")
        model["M"].append(key)
        if key not in model["S"]:
            model["S"].append(key)     # a message is a string the panel may show
    for kind in model:
        model[kind].sort(key=ident)
    model["M"].sort()                  # by the key's own spelling: what i18n::render searches
    return model


def text_of(cats, lang, key, upper=False):
    """The string a language shows for a key: Ukrainian where the language has none."""
    s = cats[lang].get(key)
    if not isinstance(s, str) or not s.strip():
        s = cats["uk"][key]
    return s.upper() if upper else s


def entries(cats, model, lang):
    """Every string of one language in table order: (label, text). S, then SU, then P × 4."""
    out = [(k, text_of(cats, lang, k)) for k in model["S"]]
    out += [(k + " ↑", text_of(cats, lang, k, upper=True)) for k in model["SU"]]
    for base in model["P"]:
        out += [(f"{base}.{f}", text_of(cats, lang, f"{base}.{f}")) for f in PLURAL_FORMS]
    return out


def panel_keys(files=None, cats=None):
    """The catalog keys the panel's code uses — for the web's 'every key is used' test."""
    model = resolve(cats or load_catalogs(), scan_uses(files))
    keys = set(model["S"]) | set(model["SU"])
    for base in model["P"]:
        keys |= {f"{base}.{f}" for f in PLURAL_FORMS}
    return keys


# ── output ──

HEAD = "// Generated by tools/i18n/gen_panel_strings.py from webui/src/i18n/*.json — do not edit.\n"


def c_string(s):
    out = []
    for ch in s:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ord(ch) < 0x20:
            raise GenError(f"control character U+{ord(ch):04X} in {s!r}")
        else:
            out.append(ch)
    return "".join(out)


def render_header(model):
    def enum(name, keys, doc):
        rows = [f"/// {doc}", f"enum class {name} : uint16_t {{"]
        width = max([len(ident(k)) for k in keys] + [0]) + 1
        rows += [f"    {(ident(k) + ','):<{width}}  // {k}" for k in keys]
        rows += ["    COUNT", "};"]
        return "\n".join(rows)

    return "\n".join([
        HEAD.rstrip("\n"),
        "// The ids are the catalog keys the panel's code names, '.' spelled '_'. Name a new one in",
        "// the code, re-run the script; tools/tests/test_panel_i18n.py fails while this is stale.",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace i18n {",
        "",
        enum("S", model["S"], "One string."),
        "",
        enum("SU", model["SU"], "One string in capitals, made by the generator (\"ß\" is \"SS\")."),
        "",
        enum("P", model["P"], "A count in words: the key's .one / .few / .many / .other."),
        "",
        "/// Messages modules publish (\"@<key>\"), in i18n_tables.cpp's MESSAGES.",
        f"constexpr uint16_t MESSAGE_COUNT = {len(model['M'])};",
        "",
        "}  // namespace i18n",
        "",
    ])


def render_tables(cats, model):
    rows = [
        HEAD.rstrip("\n"),
        "// Per language: the strings in one char array, NUL after each, and uint16_t offsets into it",
        "// for S, SU and P (four per P: one, few, many, other). All of it const, so it stays in flash.",
        "",
        '#include "i18n.h"',
        "",
        "namespace i18n {",
        "namespace detail {",
        "namespace {",
    ]
    names = []
    for lang in LANGS:
        L = lang.upper()
        ents = entries(cats, model, lang)
        offsets, pos = [], 0
        rows += ["", f"// ── {lang} ──", "", f"const char TEXT_{L}[] ="]
        if not ents:
            rows.append('    "";')
        for i, (label, s) in enumerate(ents):
            offsets.append(pos)
            pos += len(s.encode("utf-8")) + 1
            end = ";" if i == len(ents) - 1 else ""
            rows.append(f'    "{c_string(s)}\\0"{end}  // {label}')
        if pos > 0xFFFF:
            raise GenError(f"{lang}: {pos} bytes of text do not fit uint16_t offsets")
        n_s, n_su = len(model["S"]), len(model["SU"])
        parts = {"S": offsets[:n_s], "SU": offsets[n_s:n_s + n_su], "P": offsets[n_s + n_su:]}
        for kind, offs in parts.items():
            body = ", ".join(str(o) for o in offs) if offs else "0"   # an array may not be empty
            rows.append(f"const uint16_t {kind}_{L}[] = {{{body}}};")
        names.append(L)
    rows += ["", "}  // namespace", "",
             "const Table TABLES[(int)Lang::COUNT] = {"]
    rows += [f"    {{TEXT_{L}, S_{L}, SU_{L}, P_{L}}}," for L in names]
    rows += ["};", "",
             "// The messages modules publish, sorted by key for i18n::render's binary search.",
             "const Message MESSAGES[] = {"]
    rows += [f'    {{"{k}", S::{ident(k)}}},' for k in model["M"]] or ['    {"", S::COUNT},   // none: an array may not be empty']
    rows += ["};", "", "}  // namespace detail", "}  // namespace i18n", ""]
    return "\n".join(rows)


def generate(cats=None, files=None):
    cats = cats or load_catalogs()
    model = resolve(cats, scan_uses(files))
    return model, {OUT_H: render_header(model), OUT_CPP: render_tables(cats, model)}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true", help="exit 1 if the generated files are stale")
    args = ap.parse_args(argv)
    try:
        model, files = generate()
    except GenError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    stale = [p for p, text in files.items() if not p.exists() or p.read_text(encoding="utf-8") != text]
    if args.check:
        for p in stale:
            print(f"stale: {p.relative_to(ROOT)}", file=sys.stderr)
        return 1 if stale else 0
    for p in stale:
        p.write_text(files[p], encoding="utf-8", newline="\n")
    print(f"{len(model['S'])} strings, {len(model['SU'])} in capitals, {len(model['P'])} counts; "
          f"{'wrote ' + ', '.join(str(p.relative_to(ROOT)) for p in stale) if stale else 'up to date'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
