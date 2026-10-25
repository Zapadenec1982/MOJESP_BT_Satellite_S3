"""
The link protocol lives in both repositories — docs/LINK_PROTOCOL.md and .uk.md plus the shared
wire headers here, docs/PROTOCOL.md and .uk.md plus components/satlink/ in modesp-bt-satellite —
and tools/check_protocol_mirror.py keeps the copies byte-identical (modulo CRLF/LF and the
documents' first line, the link to the other language).

The fabricated-tree tests always run and pin the script's behaviour. The last test runs
the real comparison against the satellite checkout named by P4_SATELLITE_ROOT and FAILS
on any mismatch; without the variable it skips, and tools/run_host_tests.py says so
(CI has no satellite checkout).
"""
import os
import subprocess
import sys
from pathlib import Path

import pytest

import check_protocol_mirror as mirror  # tools/ is on sys.path (conftest.py)

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools" / "check_protocol_mirror.py"

BODY_EN = "\n# Device–host link protocol\n\nThis is version **1.6**.\n"
BODY_UK = "\n# Протокол лінка, байт за байтом\n\nЦя версія **1.6**.\n"
DOC = "**English** · [Українська](PROTOCOL.uk.md)\n" + BODY_EN          # the satellite's copy
COVER = ("#pragma once\n"
         "/* Protocol 1.5: track-scoped cover reset and JPEG data. Shared with satellite. */\n"
         "#define COVER_RESET 0x17\n")
FORMAT = ("#pragma once\n"
          "/* Protocol 1.3. Keep this header identical in both firmware repositories. */\n"
          "#define FMT_PREPARE 0x16u\n")
P4_ONLY = "#pragma once\n// P4-side publisher/consumer state; the satellite has no copy.\n"

PLAYER_HDR = "components/sat_link/include/sat_link"
SAT_HDR = "components/satlink/include/satlink"


def write(root, rel, text, newline="\n"):
    path = root / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.replace("\n", newline).encode("utf-8"))
    return path


@pytest.fixture
def trees(tmp_path):
    """A player tree checked out CRLF (as on Windows) and an LF satellite tree that mirror."""
    player, sat = tmp_path / "player", tmp_path / "satellite"
    write(player, "docs/LINK_PROTOCOL.md", "**English** · [Українська](LINK_PROTOCOL.uk.md)\n" + BODY_EN, "\r\n")
    write(player, "docs/LINK_PROTOCOL.uk.md", "[English](LINK_PROTOCOL.md) · **Українська**\n" + BODY_UK, "\r\n")
    write(player, f"{PLAYER_HDR}/cover_wire.h", COVER, "\r\n")
    write(player, f"{PLAYER_HDR}/format_sync.h", FORMAT, "\r\n")
    write(player, f"{PLAYER_HDR}/stream_control.h", P4_ONLY, "\r\n")
    write(sat, "docs/PROTOCOL.md", DOC)
    write(sat, "docs/PROTOCOL.uk.md", "[English](PROTOCOL.md) · **Українська**\n" + BODY_UK)
    write(sat, f"{SAT_HDR}/cover_wire.h", COVER)
    write(sat, f"{SAT_HDR}/format_sync.h", FORMAT)
    return player, sat


def test_identical_modulo_line_endings_passes(trees):
    player, sat = trees
    assert mirror.compare(player, sat) == []


def test_only_the_declared_files_are_compared(trees):
    player, _ = trees
    names = {player_rel.name for player_rel, _ in mirror.mirrored_pairs(player)}
    assert names == {"LINK_PROTOCOL.md", "LINK_PROTOCOL.uk.md", "cover_wire.h", "format_sync.h"}


def test_a_documents_first_line_is_not_compared_but_the_rest_is(trees):
    player, sat = trees
    write(sat, "docs/PROTOCOL.uk.md", "[English](elsewhere.md) · **Українська**\n" + BODY_UK)
    assert mirror.compare(player, sat) == []          # only the language link differs
    write(sat, "docs/PROTOCOL.uk.md", "[English](PROTOCOL.md) · **Українська**\n" + BODY_UK + "x\n")
    problems = mirror.compare(player, sat)
    assert len(problems) == 1 and "PROTOCOL.uk.md" in problems[0]


def test_a_headers_first_line_is_compared(trees):
    player, sat = trees
    write(sat, f"{SAT_HDR}/format_sync.h", "// a different first line\n" + FORMAT.split("\n", 1)[1])
    assert len(mirror.compare(player, sat)) == 1


def test_content_difference_fails_with_unified_diff(trees):
    player, sat = trees
    write(sat, f"{SAT_HDR}/cover_wire.h", COVER.replace("1.5", "1.6"))
    problems = mirror.compare(player, sat)
    assert len(problems) == 1
    report = problems[0]
    assert f"--- player/{PLAYER_HDR}/cover_wire.h" in report
    assert f"+++ satellite/{SAT_HDR}/cover_wire.h" in report
    assert "-/* Protocol 1.5" in report and "+/* Protocol 1.6" in report


def test_whitespace_is_a_difference(trees):
    player, sat = trees
    write(sat, "docs/PROTOCOL.md", DOC.rstrip("\n"))  # lost the final newline
    assert len(mirror.compare(player, sat)) == 1


def test_missing_satellite_copy_fails(trees):
    player, sat = trees
    (sat / SAT_HDR / "format_sync.h").unlink()
    problems = mirror.compare(player, sat)
    assert len(problems) == 1
    assert "MISSING in satellite tree" in problems[0] and "format_sync.h" in problems[0]


def test_header_that_declares_itself_shared_is_picked_up(trees):
    player, sat = trees
    shared = "#pragma once\n/* Protocol 1.7 hints. Shared with satellite. */\n#define HINT 0x2a\n"
    write(player, f"{PLAYER_HDR}/hint_wire.h", shared, "\r\n")
    problems = mirror.compare(player, sat)
    assert len(problems) == 1 and "hint_wire.h" in problems[0]  # declared shared, no copy yet
    write(sat, f"{SAT_HDR}/hint_wire.h", shared)
    assert mirror.compare(player, sat) == []


def test_cli_exit_codes(trees):
    player, sat = trees
    env = {**os.environ, "P4_SATELLITE_ROOT": ""}

    def run(*extra):
        return subprocess.run([sys.executable, str(SCRIPT), "--player", str(player), *extra],
                              capture_output=True, text=True, env=env)

    assert run("--satellite", str(sat)).returncode == 0
    write(sat, "docs/PROTOCOL.md", DOC + "extra line\n")
    result = run("--satellite", str(sat))
    assert result.returncode == 1 and "+extra line" in result.stdout
    assert run().returncode == 2                            # no satellite root at all
    assert run("--satellite", str(player)).returncode == 2  # not a satellite checkout


def test_real_satellite_checkout():
    """The real comparison. FAILS on a mismatch; skips only when there is no satellite."""
    sat = os.environ.get("P4_SATELLITE_ROOT")
    if not sat:
        pytest.skip("P4_SATELLITE_ROOT is not set: no satellite checkout to compare the "
                    "protocol mirror against (CI has none yet)")
    result = subprocess.run([sys.executable, str(SCRIPT), "--satellite", sat],
                            capture_output=True, text=True)
    assert result.returncode == 0, (
        f"protocol mirror mismatch against {sat}:\n{result.stdout}\n{result.stderr}")
