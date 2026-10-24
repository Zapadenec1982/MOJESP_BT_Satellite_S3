"""The web UI in four languages (docs/PLAN_WEBUI_2026-10-11.md §3а), and its icons.

The previous dictionaries had drifted to 109 / 82 / 46 / 46 keys, and nine keys the sign-in
form used were in none of them, so the page showed "undefined". These tests keep the four
catalogs one set, every key the code asks for present, and every key present asked for.
"""

import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "webui" / "src"
sys.path.insert(0, str(ROOT / "tools" / "i18n"))

import gen_panel_strings  # noqa: E402

LANGS = ["uk", "en", "de", "pl"]

# $t['key'] / $t["key"] in markup and scripts; K('key') where code hands a key to a component
USE = re.compile(r"""\$t\[\s*['"]([a-z0-9_.]+)['"]\s*\]|\bK\(\s*['"]([a-z0-9_.]+)['"]\s*\)""")
PLACEHOLDER = re.compile(r"\{(\w+)\}")


def catalogs():
    return {lang: json.loads((SRC / "i18n" / f"{lang}.json").read_text(encoding="utf-8")) for lang in LANGS}


def sources():
    return [p for p in SRC.rglob("*") if p.suffix in (".svelte", ".js") and p.is_file()]


def used_keys():
    keys = set()
    for path in sources():
        for m in USE.finditer(path.read_text(encoding="utf-8")):
            keys.add(m.group(1) or m.group(2))
    return keys


def test_four_catalogs_one_key_set():
    cats = catalogs()
    base = set(cats["uk"])
    for lang in LANGS[1:]:
        assert set(cats[lang]) == base, (
            f"{lang}.json differs from uk.json: missing {sorted(base - set(cats[lang]))}, "
            f"extra {sorted(set(cats[lang]) - base)}")


def test_no_empty_string():
    for lang, cat in catalogs().items():
        empty = [k for k, v in cat.items() if not isinstance(v, str) or not v.strip()]
        assert not empty, f"{lang}.json: empty {empty}"


def test_placeholders_agree():
    cats = catalogs()
    for key, uk in cats["uk"].items():
        want = sorted(PLACEHOLDER.findall(uk))
        for lang in LANGS[1:]:
            got = sorted(PLACEHOLDER.findall(cats[lang][key]))
            assert got == want, f"{lang}.json {key}: placeholders {got}, uk has {want}"


def test_every_used_key_exists():
    missing = sorted(used_keys() - set(catalogs()["uk"]))
    assert not missing, f"used in webui/src but not in uk.json: {missing}"


def test_every_key_is_used():
    # The panel reads the same catalogs (docs/PLAN_PANEL_LANG_2026-10-12.md): a key only the
    # glass uses is used.
    unused = sorted(set(catalogs()["uk"]) - used_keys() - gen_panel_strings.panel_keys())
    assert not unused, f"in the catalogs but nowhere in webui/src or the panel's code: {unused}"


def test_icons_are_in_the_font():
    names = json.loads((SRC / "lib" / "icon_names.json").read_text(encoding="utf-8"))
    cps = (SRC / "lib" / "icon_codepoints.js").read_text(encoding="utf-8")
    generated = set(re.findall(r"^\s+(\w+): 0x[0-9A-F]+,$", cps, re.M))
    assert generated == set(names), "icon_codepoints.js is stale: run tools/fonts/build_web_icons.py"
    literal = set()
    for path in sources():
        text = path.read_text(encoding="utf-8")
        literal.update(re.findall(r"""\bicon(?:=|:\s*)['"]([a-z0-9_]+)['"]""", text))
        literal.update(re.findall(r"""<Icon\s+name=['"]([a-z0-9_]+)['"]""", text))
    missing = sorted(literal - set(names))
    assert not missing, f"icons used but not in icon_names.json: {missing}"


# ── the firmware's messages (docs/PLAN_PANEL_LANG_2026-10-12.md, U8.5) ──

def _node(script):
    exe = shutil.which("node")
    assert exe, "Node.js is required for the WebUI checks"
    r = subprocess.run([exe, "--input-type=module", "-e", script], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    return r.stdout


def test_msg_reads_the_firmwares_messages():
    lib = (SRC / "lib" / "msg.js").as_uri()
    out = json.loads(_node(f"""
import {{ msg }} from {json.dumps(lib)};
const d = {{ 'cal.m_progress': 'pomiar: {{hz}} Hz ({{k}} z {{n}})', 'dlna.st_playing': 'Gra: {{title}}',
            'dlna.servers': 'Serwery', 'x.y': 'XY {{a}}' }};
const U = String.fromCharCode(31);
console.log(JSON.stringify([
  msg(d, '@cal.m_progress' + U + 'hz=63' + U + 'k=3' + U + 'n=40'),
  msg(d, '@dlna.st_playing' + U + 'title=a=b'),          // "=" in a value
  msg(d, '@dlna.servers'),
  msg(d, '@dlna.nothing' + U + 'n=1'),                   // a key the catalog lacks
  msg(d, '@Moja muzyka'),                                // a folder's name with an "@"
  msg(d, '@dlna'),                                       // no dot: not a key
  msg(d, 'Медіасервери'),                                // an older firmware's text
  msg(d, '@x.y'),                                        // a missing argument stays as written
  msg(d, ''), msg(d, undefined),
]));
"""))
    assert out == ["pomiar: 63 Hz (3 z 40)", "Gra: a=b", "Serwery", "@dlna.nothing\u001fn=1",
                   "@Moja muzyka", "@dlna", "Медіасервери", "XY {a}", "", ""]


# ── which language the web speaks (docs/PLAN_PANEL_LANG_2026-10-12.md §4, U8.6) ──

def test_the_web_follows_the_panel_unless_this_browser_picked():
    lib = (SRC / "lib" / "lang.js").as_uri()
    out = json.loads(_node(f"""
import {{ chosen, migratedPick, panelCode, browserLang }} from {json.dumps(lib)};
console.log(JSON.stringify([
  chosen('de', 'pl', 'en-US'),         // a pick wins over the panel
  chosen(null, 'pl', 'en-US'),         // no pick: the panel
  chosen(null, null, 'de-AT'),         // the panel has not said yet: the browser
  chosen('xx', 'yy', 'ru-RU'),         // nothing known: the browser
  panelCode(3), panelCode(4), panelCode('1'), panelCode(undefined),
  browserLang('pl-PL'), browserLang('uk'), browserLang('fr-FR'), browserLang(''),
  migratedPick('uk', null, 'uk-UA'),   // the old key held the browser's own: never picked
  migratedPick('de', null, 'uk-UA'),   // anything else was picked: kept
  migratedPick('de', 'pl', 'uk-UA'),   // a new pick is not overwritten
  migratedPick('xx', null, 'uk-UA'),
]));
"""))
    assert out == ["de", "pl", "de", "uk",
                   "pl", None, None, None,
                   "pl", "uk", "en", "en",
                   None, "de", "pl", None]


def test_every_firmware_message_is_in_all_four_catalogs():
    model = gen_panel_strings.resolve(gen_panel_strings.load_catalogs(), gen_panel_strings.scan_uses())
    assert len(model["M"]) >= 30, "the modules' messages were not found"
    cats = catalogs()
    for key in model["M"]:
        for lang in LANGS:
            assert key in cats[lang] and cats[lang][key].strip(), f"{lang}.json lacks the message {key}"
