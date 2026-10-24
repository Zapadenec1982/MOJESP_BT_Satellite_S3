"""How wide a string is on the panel, read from the LVGL faces themselves.

The faces in modules/ui_lvgl/src/fonts/ are lv_font_conv output, committed (tools/fonts/build_fonts.py).
Each carries its glyphs' advance widths and its class kerning as C tables, and those are the
numbers LVGL lays text out with — so a width taken from them is the width on the glass, give or
take the label's letter spacing (none in this UI). No rendering, no LVGL, no device.

    face = Face(FONTS / "ui_font_400_20.c")
    face.width("Автосон")       # px, kerning included
    face.missing("Łódź →")      # {'→'}: characters the face has no glyph for
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FONTS = ROOT / "modules" / "ui_lvgl" / "src" / "fonts"

# The text faces, one per type role (ui_m3.h); the 160 px clock face draws digits only.
TEXT_FACES = ("ui_font_500_16", "ui_font_500_18", "ui_font_400_18", "ui_font_400_20",
              "ui_font_400_24", "ui_font_500_32", "ui_font_600_48")


def _array(text, name, ctype):
    m = re.search(r"static const " + ctype + r" " + name + r"\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        raise ValueError(f"{name}[] not found")
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    return [int(x) for x in re.findall(r"-?\d+", body)]


class Face:
    """One lv_font_conv face: its glyph set, advances and class kerning."""

    def __init__(self, path):
        self.name = Path(path).stem
        text = Path(path).read_text(encoding="utf-8")
        # glyph_bitmap[] labels each glyph "/* U+0041 "A" */" in glyph-id order, and glyph_dsc[]
        # lists the same glyphs in the same order after the reserved id 0.
        bitmaps = text[text.index("glyph_bitmap[]"):text.index("glyph_dsc[]")]
        codepoints = [int(x, 16) for x in re.findall(r"/\* U\+([0-9A-F]+) ", bitmaps)]
        dsc = text[text.index("glyph_dsc[]"):]
        advances = [int(x) for x in re.findall(r"\.adv_w = (\d+)", dsc)][1:]
        if len(advances) < len(codepoints):
            raise ValueError(f"{self.name}: {len(codepoints)} glyphs, {len(advances)} advances")
        self.glyph = {cp: i + 1 for i, cp in enumerate(codepoints)}
        self.adv = [0] + advances                 # 1/16 px
        self.kerned = "kern_class_values" in text
        if self.kerned:
            self.left = _array(text, "kern_left_class_mapping", "uint8_t")
            self.right = _array(text, "kern_right_class_mapping", "uint8_t")
            self.pairs = _array(text, "kern_class_values", "int8_t")
            self.right_count = int(re.search(r"\.right_class_cnt\s*=\s*(\d+)", text).group(1))
            self.kern_scale = int(re.search(r"\.kern_scale\s*=\s*(\d+)", text).group(1))

    def missing(self, s):
        """The characters of `s` this face cannot draw (controls aside)."""
        return {ch for ch in s if ord(ch) >= 0x20 and ord(ch) not in self.glyph}

    def width(self, s):
        """Pixels from the first glyph's origin to the last one's advance, as LVGL lays it out."""
        w, prev = 0, None
        for ch in s:
            g = self.glyph.get(ord(ch))
            if g is None:
                prev = None
                continue
            w += self.adv[g]
            if self.kerned and prev is not None:
                lc, rc = self.left[prev], self.right[g]
                if lc and rc:
                    # lv_font_fmt_txt.c, get_kern_value(): the class pair value scaled by kern_scale/16
                    w += (self.pairs[(lc - 1) * self.right_count + (rc - 1)] * self.kern_scale) >> 4
            prev = g
        return w / 16.0


def text_faces():
    return [Face(FONTS / f"{name}.c") for name in TEXT_FACES]
