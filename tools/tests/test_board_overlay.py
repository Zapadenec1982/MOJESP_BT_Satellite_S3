"""
test_board_overlay.py — project.json's per-board overlay (tools/generate_ui.py,
apply_board_overlay), which is what lets one tree build the P4 panel and the headless ESP32-S3.

The module list decides what compiles, what registers and which state keys exist, so a wrong
overlay is not a cosmetic difference: ui_lvgl on the S3 does not compile, ui_lvgl AND
ui_headless together declare the same keys twice and the generator refuses them, and neither
leaves the web without its cover and library routes.
"""

import json
import os
import sys

import pytest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from generate_ui import apply_board_overlay  # noqa: E402


@pytest.fixture(scope="module")
def project():
    with open(os.path.join(ROOT, "project.json"), encoding="utf-8") as f:
        return json.load(f)


def test_the_p4_panel_builds_the_list_as_written(project):
    out = apply_board_overlay(project, "p4_player")
    assert out["modules"] == project["modules"]
    assert "ui_lvgl" in out["modules"] and "ui_headless" not in out["modules"]


def test_the_s3_swaps_the_panel_ui_for_the_headless_one_in_its_place(project):
    out = apply_board_overlay(project, "s3_player")
    assert "ui_lvgl" not in out["modules"]
    assert out["modules"].index("ui_headless") == project["modules"].index("ui_lvgl")
    # Every other module stays, in order.
    rest = [m for m in project["modules"] if m != "ui_lvgl"]
    assert [m for m in out["modules"] if m != "ui_headless"] == rest
    # Its own name on the network; the rest of `system` is the project's.
    assert out["system"]["device_name"] != project["system"]["device_name"]
    assert out["system"]["mqtt_topic_root"] == project["system"]["mqtt_topic_root"]


def test_the_input_is_left_alone(project):
    before = json.dumps(project, sort_keys=True)
    apply_board_overlay(project, "s3_player")
    assert json.dumps(project, sort_keys=True) == before


def test_every_board_that_binds_a_module_gets_it(project):
    """A binding to a module the board's list does not have is a build error (generate_ui.py,
    validate_bindings); this says so before CMake does."""
    for board in sorted(os.listdir(os.path.join(ROOT, "boards"))):
        path = os.path.join(ROOT, "boards", board, "bindings.json")
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8") as f:
            bound = {b["module"] for b in json.load(f)["bindings"]}
        mods = set(apply_board_overlay(project, board)["modules"])
        assert bound <= mods, "%s binds %s, which its module list lacks" % (board, bound - mods)


def test_a_name_the_overlay_cannot_apply_stops_the_generator(project):
    bad = dict(project)
    bad["boards"] = {"x": {"exclude_modules": ["no_such_module"]}}
    with pytest.raises(SystemExit):
        apply_board_overlay(bad, "x")
    bad["boards"] = {"x": {"add_modules": [project["modules"][0]]}}
    with pytest.raises(SystemExit):
        apply_board_overlay(bad, "x")
