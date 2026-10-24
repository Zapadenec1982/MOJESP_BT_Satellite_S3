"""The web UI's colours are the panel's: m3_schemes.css is generated from ui_m3.cpp and must
not fall behind it (docs/PLAN_WEBUI_2026-10-11.md §3)."""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "ui"))

import web_tokens  # noqa: E402


def _render():
    return web_tokens.render(web_tokens.HEADER.read_text(encoding="utf-8"),
                             web_tokens.TABLE.read_text(encoding="utf-8"))


def test_css_matches_the_panel_table():
    assert web_tokens.OUT.read_text(encoding="utf-8") == _render(), \
        "webui/src/styles/m3_schemes.css is out of date: run python tools/ui/web_tokens.py"


def test_every_scheme_defines_every_role():
    css = _render()
    blocks = re.findall(r"\{([^}]*)\}", css)
    roles = web_tokens.roles(web_tokens.HEADER.read_text(encoding="utf-8"))
    assert len(blocks) == 4, "ui.theme has four schemes"
    for block in blocks:
        names = re.findall(r"--([a-z-]+):", block)
        assert names == [r.replace("_", "-") for r in roles]


def test_a_colour_count_mismatch_is_refused():
    header = "struct Colors {\n uint32_t a;\n uint32_t b;\n};"
    table = 'const Theme THEMES[1] = {\n    {"X",\n     {0x000000}, T},\n};'
    try:
        web_tokens.render(header, table)
    except SystemExit as e:
        assert "1 colours for 2 roles" in str(e)
    else:
        raise AssertionError("a short scheme was accepted")
