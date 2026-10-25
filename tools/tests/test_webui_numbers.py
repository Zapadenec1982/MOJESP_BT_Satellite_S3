"""Numbers typed into the web's fields (webui/src/lib/numbers.js) under Node.

A phone's decimal keypad (iOS) has no minus: in a field whose values are all at or below zero a
level typed as "20" was clamped to 0 dBFS and played at full scale; the pages print U+2212, which
the old parser refused. These pin the parser, and the printer the equalizer uses.
"""

import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
LIB = json.dumps((ROOT / "webui" / "src" / "lib" / "numbers.js").as_uri())


def run(script: str) -> None:
    node = shutil.which("node")
    assert node, "Node.js is required for the WebUI number tests"
    prelude = "function check(c, what) { if (!c) { console.error('FAILED: ' + what); process.exit(1); } }\n"
    r = subprocess.run([node, "--input-type=module", "-e", prelude + script], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr


def test_parse_number_reads_commas_k_and_every_minus():
    run(f"""
const {{ parseNumber }} = await import({LIB});
check(parseNumber('1,25') === 1.25, 'a decimal comma');
check(parseNumber('1.2k') === 1200, 'k');
check(parseNumber('-3') === -3, 'a hyphen');
check(parseNumber('\\u22123,5') === -3.5, 'U+2212, as the pages print it');
check(parseNumber('\\u20136') === -6, 'an en dash');
check(parseNumber('+2') === 2, 'a plus');
check(Number.isNaN(parseNumber('abc')), 'no number');
""")


def test_parse_non_positive_reads_an_unsigned_number_as_negative():
    run(f"""
const {{ parseNonPositive }} = await import({LIB});
check(parseNonPositive('20') === -20, '"20" is -20 dBFS, not 0');
check(parseNonPositive('-20') === -20, 'a typed minus stays');
check(parseNonPositive('\\u221212,5') === -12.5, 'U+2212');
check(parseNonPositive('0') === 0 || Object.is(parseNonPositive('0'), -0), 'zero');
check(parseNonPositive('+3') === 3, 'an explicit plus is kept, for the range to refuse');
check(Number.isNaN(parseNonPositive('')), 'nothing');
""")


def test_decimal_writes_the_languages_separator_as_the_glass_does():
    run(f"""
const {{ decimal, parseNumber }} = await import({LIB});
check(decimal(1.25, 2, 'uk') === '1,25', 'uk: a comma');
check(decimal(1.25, 2, 'de') === '1,25' && decimal(1.25, 2, 'pl') === '1,25', 'de, pl: a comma');
check(decimal(1.25, 2, 'en') === '1.25', 'en: a point');
check(decimal(-3, 1, 'uk') === '\u22123,0', 'the decimals it was asked for, and the minus sign');
check(decimal(-0.04, 1, 'uk') === '0,0', 'what rounds to zero has no sign');
check(parseNumber(decimal(-3.5, 1, 'uk')) === -3.5, 'what it prints reads back');
""")


def test_badge_says_a_technical_line_in_the_languages_units():
    # The same lines as tests/host/test_i18n.cpp holds i18n::badge to: the glass and the web agree.
    uk = {"unit.khz": "кГц", "unit.hz": "Гц", "unit.kbps": "кбіт/с", "unit.bit": "біт", "unit.ch": "кан."}
    en = {"unit.khz": "kHz", "unit.hz": "Hz", "unit.kbps": "kbit/s", "unit.bit": "bit", "unit.ch": "ch"}
    run(f"""
const {{ badge }} = await import({LIB});
const uk = {json.dumps(uk, ensure_ascii=False)}, en = {json.dumps(en)};
const eq = (a, b, what) => check(a === b, what + ': ' + JSON.stringify(a) + ' is not ' + JSON.stringify(b));
eq(badge('44.1 kHz 2 ch 128 kbps', uk, 'uk'), '44,1 кГц 128 кбіт/с', 'the radio: stereo unsaid');
eq(badge('16/44.1 kHz 2 ch', uk, 'uk'), '16/44,1 кГц', 'the card: bits/rate');
eq(badge('16/48.0 kHz 1 ch', uk, 'uk'), '16/48 кГц 1 кан.', 'a whole number loses its ,0; mono is said');
eq(badge('LHDC V5 96.0 kHz', uk, 'uk'), 'LHDC V5 96 кГц', 'Bluetooth: the codec stays');
eq(badge('48.0 kHz 24 bit', uk, 'uk'), '48 кГц 24 біт', 'USB');
eq(badge('1000 Hz', uk, 'uk'), '1000 Гц', 'the tone');
eq(badge('AirPlay', uk, 'uk'), 'AirPlay', 'no number, nothing to say');
eq(badge('AAC kHz', uk, 'uk'), 'AAC kHz', 'a unit after a word is a word');
eq(badge('', uk, 'uk'), '', 'nothing');
eq(badge('44.1 kHz 2 ch 128 kbps', en, 'en'), '44.1 kHz 128 kbit/s', 'en: a point');
""")
