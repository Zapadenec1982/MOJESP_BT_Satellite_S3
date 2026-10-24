"""
The UDP probe's packets are written twice: in C, components/net_probe/include/net_probe/
probe_wire.h, and in Python, tools/net_probe.py. A field added to one and not the other shifts
every number after it in the report, and nothing at run time would say so — the PC would just
print the DMA figure where the drop counter should be. These tests read the header and hold
the Python layout to it: sizes, the enum values, and the order of the report's fields.
"""
import re
from pathlib import Path

import net_probe as probe  # tools/ is on sys.path (conftest.py)

ROOT = Path(__file__).resolve().parents[2]
WIRE_H = ROOT / "components" / "net_probe" / "include" / "net_probe" / "probe_wire.h"


def header_text():
    return WIRE_H.read_text(encoding="utf-8")


def asserted_size(text, struct_name):
    m = re.search(r"sizeof\(" + struct_name + r"\)\s*==\s*(\d+)", text)
    assert m, f"no size assertion for {struct_name} in {WIRE_H.name}"
    return int(m.group(1))


def enum_value(text, name):
    m = re.search(r"\b" + name + r"\s*=\s*(0x[0-9A-Fa-f]+|\d+)", text)
    assert m, f"{name} not found in {WIRE_H.name}"
    return int(m.group(1), 0)


def struct_body(text, struct_name):
    # One typedef only: the body may not run into the next "typedef", or a lazy match from the
    # first struct in the file would swallow every struct up to the one asked for.
    m = re.search(r"typedef struct[^{]*\{((?:(?!typedef).)*?)\}\s*" + struct_name + r";", text, re.S)
    assert m, f"{struct_name} not found in {WIRE_H.name}"
    return re.sub(r"//[^\n]*|/\*.*?\*/", "", m.group(1), flags=re.S)


def struct_members(text, struct_name):
    return re.findall(r"\b(?:uint8_t|uint16_t|uint32_t|uint64_t)\s+(\w+)\s*;",
                      struct_body(text, struct_name))


def test_sizes_match_the_c_assertions():
    text = header_text()
    assert probe.HDR.size == asserted_size(text, "probe_hdr_t") == 24
    assert probe.CTRL.size == asserted_size(text, "probe_ctrl_t") == 2
    assert probe.REPORT.size == asserted_size(text, "probe_report_t") == 92


def test_magic_types_flags_and_commands_match():
    text = header_text()
    assert probe.MAGIC == int(re.search(r"PROBE_MAGIC\s+(0x[0-9A-Fa-f]+)u", text).group(1), 16)
    assert (probe.T_DATA, probe.T_CTRL, probe.T_REPORT, probe.T_ECHO) == tuple(
        enum_value(text, n) for n in ("PROBE_DATA", "PROBE_CTRL", "PROBE_REPORT", "PROBE_ECHO"))
    assert probe.F_ECHO == enum_value(text, "PROBE_F_ECHO")
    assert (probe.CMD_START, probe.CMD_SET_PSRAM, probe.CMD_REPORT_NOW) == tuple(
        enum_value(text, n) for n in ("PROBE_CMD_START", "PROBE_CMD_SET_PSRAM", "PROBE_CMD_REPORT_NOW"))


def test_report_fields_are_in_the_c_order():
    assert probe.REPORT_FIELDS == struct_members(header_text(), "probe_report_t")


def test_report_format_widths_follow_the_c_types():
    # 21 uint32, then uint8 + uint8 + uint16, then uint32: the order the C struct declares.
    text = header_text()
    members = struct_members(text, "probe_report_t")
    types = re.findall(r"\b(uint8_t|uint16_t|uint32_t|uint64_t)\s+\w+\s*;",
                       struct_body(text, "probe_report_t"))
    fmt = {"uint8_t": "B", "uint16_t": "H", "uint32_t": "I", "uint64_t": "Q"}
    expanded = "".join(fmt[t] for t in types)
    python = re.sub(r"(\d+)(\w)", lambda m: m.group(2) * int(m.group(1)), probe.REPORT.format.lstrip("<"))
    assert len(members) == len(types)
    assert python == expanded
