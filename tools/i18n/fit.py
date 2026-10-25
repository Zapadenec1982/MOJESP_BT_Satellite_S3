#!/usr/bin/env python3
"""Do the panel's strings fit in every language?

The rule needs no table of slots. Ukrainian is the language the glass was laid out and accepted
in, so a translation no wider than its Ukrainian fits wherever the Ukrainian does. A wider one
has to be looked at on the glass, and modules/ui_lvgl/i18n_fit.json says which ones have been:

    "checked":   seen on the panel in that language and fitting (overflow route, screenshots)
    "unchecked": known to be wider and not yet seen — the work list of the check on the glass

Each entry is the width that was recorded, so a later edit that makes a string wider still
fails again until someone looks again. Widths come from the panel's own body face
(font_metrics.py); other faces of the same family scale with it.

    python tools/i18n/fit.py            # the report: what is wider than Ukrainian, and by how much
    python tools/i18n/fit.py --record   # put every wider string not yet listed under "unchecked"
    python tools/i18n/fit.py --accept   # after the check on the glass: "unchecked" -> "checked"
    python tools/i18n/fit.py --accept @seen.txt   # ... only those seen, one label:lang a line

The second form is what the glass check produces: GET /api/i18n/overflow?all=1 lists every
label that fits, page by page, and the strings found among them are the ones seen.
"""

import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import font_metrics                 # noqa: E402
import gen_panel_strings as gen     # noqa: E402

ROOT = gen.ROOT
FIT = ROOT / "modules" / "ui_lvgl" / "i18n_fit.json"
REFERENCE = "ui_font_400_20"        # body_l: the rows' titles, the commonest text on the glass
SLACK = 0.5                         # px: rounding, not leniency

COMMENT = ("Panel strings wider than their Ukrainian (tools/i18n/fit.py). "
           "px in the reference face. checked: seen fitting on the glass; unchecked: not yet seen.")


def measure(cats=None, files=None, face=None):
    """{label: {lang: px}} for every string the panel uses, in table order."""
    cats = cats or gen.load_catalogs()
    model = gen.resolve(cats, gen.scan_uses(files))
    face = face or font_metrics.Face(font_metrics.FONTS / f"{REFERENCE}.c")
    out = {}
    for lang in gen.LANGS:
        for label, text in gen.entries(cats, model, lang):
            out.setdefault(label, {})[lang] = round(face.width(text), 1)
    return out


def wider(widths):
    """{label: {lang: px}} — the translations wider than their Ukrainian."""
    out = {}
    for label, by_lang in widths.items():
        for lang, px in by_lang.items():
            if lang != "uk" and px > by_lang["uk"] + SLACK:
                out.setdefault(label, {})[lang] = px
    return out


def load(path=FIT):
    if not path.exists():
        return {"checked": {}, "unchecked": {}}
    data = json.loads(path.read_text(encoding="utf-8"))
    return {"checked": data.get("checked", {}), "unchecked": data.get("unchecked", {})}


def save(fit, path=FIT):
    data = {"_comment": COMMENT, "face": REFERENCE,
            "checked": dict(sorted(fit["checked"].items())),
            "unchecked": dict(sorted(fit["unchecked"].items()))}
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")


def problems(widths, fit):
    """What fails: a wider string with no entry, or wider than its entry; an entry for a string
    the panel no longer uses."""
    out = []
    for label, by_lang in wider(widths).items():
        for lang, px in by_lang.items():
            seen = fit["checked"].get(label, {}).get(lang, fit["unchecked"].get(label, {}).get(lang))
            if seen is None:
                out.append(f"{label} [{lang}] {px} px, Ukrainian {widths[label]['uk']} px: not in i18n_fit.json "
                           f"— shorten it, or run tools/i18n/fit.py --record and check it on the glass")
            elif px > seen + SLACK:
                out.append(f"{label} [{lang}] {px} px, recorded {seen} px: wider than when it was recorded")
    for part in ("checked", "unchecked"):
        for label in fit[part]:
            if label not in widths:
                out.append(f"i18n_fit.json {part}: '{label}' is not a panel string any more")
    return out


def accept(fit, seen=None):
    """Move "unchecked" entries to "checked": all of them, or those in `seen`, {(label, lang)}."""
    for label, by_lang in list(fit["unchecked"].items()):
        for lang in list(by_lang):
            if seen is None or (label, lang) in seen:
                fit["checked"].setdefault(label, {})[lang] = by_lang.pop(lang)
        if not by_lang:
            del fit["unchecked"][label]
    return fit


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0], fromfile_prefix_chars="@")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--record", action="store_true", help="list new wider strings under 'unchecked'")
    g.add_argument("--accept", action="store_true", help="move 'unchecked' to 'checked' (after the glass check)")
    ap.add_argument("seen", nargs="*", help="with --accept: only these, as label:lang")
    args = ap.parse_args(argv)
    if args.seen and not args.accept:
        ap.error("label:lang arguments go with --accept")
    widths = measure()
    fit = load()
    if args.record:
        for part in ("checked", "unchecked"):
            fit[part] = {k: v for k, v in fit[part].items() if k in widths}
        for label, by_lang in wider(widths).items():
            for lang, px in by_lang.items():
                seen = fit["checked"].get(label, {}).get(lang)
                if seen is None or px > seen + SLACK:
                    fit["unchecked"].setdefault(label, {})[lang] = px
        save(fit)
    elif args.accept:
        accept(fit, {tuple(x.rsplit(":", 1)) for x in args.seen} or None)
        save(fit)
    w = wider(widths)
    for label, by_lang in w.items():
        print(f"{label:28s} uk {widths[label]['uk']:6.1f}  " +
              "  ".join(f"{lang} {px:6.1f}" for lang, px in sorted(by_lang.items())))
    probs = problems(widths, fit)
    print(f"{len(widths)} strings, {len(w)} wider than Ukrainian in some language; "
          f"{len(fit['unchecked'])} unchecked, {len(fit['checked'])} checked; {len(probs)} problems")
    for p in probs:
        print("  " + p)
    return 1 if probs else 0


if __name__ == "__main__":
    sys.exit(main())
