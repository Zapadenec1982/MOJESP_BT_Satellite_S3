#!/usr/bin/env python3
"""
check_protocol_mirror.py — verify that the link protocol contract is mirrored byte-for-byte
between this repository (the P4 player) and the Bluetooth satellite repository.

The contract is two copies on purpose (see the header of docs/LINK_PROTOCOL.md): neither
firmware may depend on the other tree existing on disk, so the document and the shared
wire headers are duplicated by hand — and this script is what keeps the duplicates honest.
They have drifted before (audit 2026-09-21, plan 5.6 D / 10.6).

Mirrored files (player path -> satellite path):

    docs/LINK_PROTOCOL.md                               -> docs/PROTOCOL.md
    components/sat_link/include/sat_link/cover_wire.h   -> components/satlink/include/satlink/cover_wire.h
    components/sat_link/include/sat_link/format_sync.h  -> components/satlink/include/satlink/format_sync.h

In addition, every header under components/sat_link/include/sat_link/ whose leading
comment declares it shared ("Shared with satellite", "identical in both") is compared
against the satellite header of the same name, so a newly shared header cannot escape
the check by being forgotten here. stream_control.h is deliberately NOT mirrored: it is
the P4-side publisher/consumer state machine (C++, std::atomic) and the satellite has no
counterpart — its wire constants come from format_sync.h, which is.

Only line endings are normalised (CRLF -> LF) before comparison: the player tree is
checked out with core.autocrlf=true, the satellite tree is LF, and that is not a protocol
difference. Everything else — whitespace, a comment, a version label — is.

Usage:

    python tools/check_protocol_mirror.py --satellite D:\\P4_DSP\\modesp-bt-satellite
    P4_SATELLITE_ROOT=/path/to/modesp-bt-satellite python tools/check_protocol_mirror.py

Exit status: 0 when every mirrored file matches; 1 with a unified diff per mismatch (or
a line per missing file); 2 when no usable satellite root was given.
tools/tests/test_protocol_mirror.py runs this against $P4_SATELLITE_ROOT when it is set.
"""
from __future__ import annotations

import argparse
import difflib
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PLAYER_ROOT = HERE.parent

PLAYER_HEADERS = Path("components/sat_link/include/sat_link")
SATELLITE_HEADERS = Path("components/satlink/include/satlink")

# (player path, satellite path), each relative to its repository root.
MIRRORED: list[tuple[Path, Path]] = [
    (Path("docs/LINK_PROTOCOL.md"), Path("docs/PROTOCOL.md")),
    (PLAYER_HEADERS / "cover_wire.h", SATELLITE_HEADERS / "cover_wire.h"),
    (PLAYER_HEADERS / "format_sync.h", SATELLITE_HEADERS / "format_sync.h"),
]

# A header whose first lines carry one of these phrases is a shared header and is
# compared too, even when it is not in MIRRORED yet.
SHARED_MARKERS = ("shared with satellite", "identical in both")
MARKER_LINES = 5


def normalise(data: bytes) -> str:
    """CRLF -> LF and nothing else: a BOM, trailing whitespace or a comment is a difference."""
    return data.replace(b"\r\n", b"\n").decode("utf-8", errors="surrogateescape")


def mirrored_pairs(player_root: Path) -> list[tuple[Path, Path]]:
    """The fixed list plus every header under sat_link/ that declares itself shared."""
    pairs = list(MIRRORED)
    known = {player_rel for player_rel, _ in pairs}
    header_dir = player_root / PLAYER_HEADERS
    headers = sorted(header_dir.glob("*.h")) if header_dir.is_dir() else []
    for header in headers:
        rel = PLAYER_HEADERS / header.name
        if rel in known:
            continue
        head = "\n".join(normalise(header.read_bytes()).splitlines()[:MARKER_LINES]).lower()
        if any(marker in head for marker in SHARED_MARKERS):
            pairs.append((rel, SATELLITE_HEADERS / header.name))
    return pairs


def compare(player_root: Path, satellite_root: Path) -> list[str]:
    """One report string per mismatch (unified diff or a MISSING line); empty when all mirror."""
    problems: list[str] = []
    for player_rel, sat_rel in mirrored_pairs(player_root):
        player_file = player_root / player_rel
        sat_file = satellite_root / sat_rel
        if not player_file.is_file():
            problems.append(f"MISSING in player tree: {player_rel.as_posix()}")
            continue
        if not sat_file.is_file():
            problems.append(
                f"MISSING in satellite tree: {sat_rel.as_posix()} "
                f"(mirror of {player_rel.as_posix()})"
            )
            continue
        a = normalise(player_file.read_bytes())
        b = normalise(sat_file.read_bytes())
        if a == b:
            continue
        diff = difflib.unified_diff(
            a.splitlines(), b.splitlines(),
            fromfile=f"player/{player_rel.as_posix()}",
            tofile=f"satellite/{sat_rel.as_posix()}",
            lineterm="",
        )
        problems.append("\n".join(diff))
    return problems


def main(argv: list[str] | None = None) -> int:
    for stream in (sys.stdout, sys.stderr):
        try:  # a Ukrainian diff on a cp1251 console must not turn into a UnicodeEncodeError
            stream.reconfigure(errors="backslashreplace")
        except AttributeError:
            pass

    parser = argparse.ArgumentParser(
        description="Compare the link protocol document and the shared wire headers "
                    "with their copies in the satellite repository (CRLF/LF ignored).")
    parser.add_argument("--satellite", default=os.environ.get("P4_SATELLITE_ROOT") or None,
                        help="root of the modesp-bt-satellite checkout "
                             "(default: $P4_SATELLITE_ROOT)")
    parser.add_argument("--player", default=str(PLAYER_ROOT), help=argparse.SUPPRESS)
    args = parser.parse_args(argv)

    if not args.satellite:
        print("check_protocol_mirror: no satellite root - pass --satellite <path> or set "
              "P4_SATELLITE_ROOT", file=sys.stderr)
        return 2
    satellite = Path(args.satellite)
    if not (satellite / "docs" / "PROTOCOL.md").is_file():
        print(f"check_protocol_mirror: {satellite} is not a satellite checkout "
              f"(no docs/PROTOCOL.md)", file=sys.stderr)
        return 2
    player = Path(args.player)

    pairs = mirrored_pairs(player)
    problems = compare(player, satellite)
    for problem in problems:
        print(problem)
        print()
    if problems:
        print(f"check_protocol_mirror: {len(problems)} of {len(pairs)} mirrored file(s) differ "
              f"(player {player}, satellite {satellite})")
        return 1
    print(f"check_protocol_mirror: {len(pairs)} mirrored file(s) identical after CRLF/LF "
          f"normalisation (satellite {satellite})")
    for player_rel, sat_rel in pairs:
        print(f"  {player_rel.as_posix()}  ==  {sat_rel.as_posix()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
