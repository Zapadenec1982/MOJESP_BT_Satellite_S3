"""
components/airplay2/upstream/ is somebody else's code, kept exactly as published: that is what
lets its licence notice, the provenance in UPSTREAM.md and the next update from upstream all
mean what they say. A fix made in place there would be invisible — it would look like
upstream's code and vanish at the next update — so every change belongs in port/.

upstream.sha256 lists every file with its hash. These tests hold the directory to the list: a
file edited in place, a file added without being listed, a file listed and gone.
"""
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMP = ROOT / "components" / "airplay2"
UPSTREAM = COMP / "upstream"
LISTING = COMP / "upstream.sha256"


def listed():
    out = {}
    for line in LISTING.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        digest, rel = line.split(None, 1)
        out[rel.strip()] = digest
    return out


def test_the_listing_is_not_empty():
    assert len(listed()) > 50, "upstream.sha256 lists almost nothing — regenerated wrongly?"


def test_every_listed_file_is_as_published():
    changed = [rel for rel, digest in sorted(listed().items())
               if not (COMP / rel).is_file()
               or hashlib.sha256((COMP / rel).read_bytes()).hexdigest() != digest]
    assert not changed, (f"differs from upstream or missing: {changed} — a change to upstream "
                         "code goes in port/ (UPSTREAM.md, 'Layout')")


def test_nothing_in_upstream_is_unlisted():
    present = {p.relative_to(COMP).as_posix() for p in UPSTREAM.rglob("*") if p.is_file()}
    unlisted = sorted(present - set(listed()))
    assert not unlisted, f"in upstream/ but not in upstream.sha256: {unlisted}"


def test_licence_travels_with_the_code():
    text = (COMP / "LICENSE").read_text(encoding="utf-8")
    assert "Non-Commercial License" in text and "Remi Bouteiller" in text
