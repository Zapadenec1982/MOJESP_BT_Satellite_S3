"""
test_partitions.py — the flash layout, and the copies of it that must not drift.

Three projects carry the same table: the player (partitions_p4_16mb.csv), tools/c6_updater
and tools/sdio_soak. Each of them, when flashed, WRITES that table to the panel. A stale
copy therefore does not merely disagree — it replaces the live layout with its own. Since
22.09.2026 that has teeth: a copy still holding the single-`data` layout would take the
`userdata` partition away, and with it the EQ, the station list, the resume point and the
subscribed devices that were moved there precisely so a flash could not reach them.

The layout itself is checked too, because the one thing a partition table must never do is
overlap, and the one thing this table must never do again is put user files where the build
output goes.
"""

import os

import pytest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

MAIN = os.path.join(ROOT, "partitions_p4_16mb.csv")
COPIES = [
    os.path.join(ROOT, "tools", "c6_updater", "partitions.csv"),
    os.path.join(ROOT, "tools", "sdio_soak", "partitions.csv"),
]

FLASH_SIZE = 16 * 1024 * 1024
TABLE_OFFSET = 0x10000       # CONFIG_PARTITION_TABLE_OFFSET
TABLE_SIZE = 0x1000          # one flash page holds the table itself


def rows(path):
    """The data rows, as (name, type, subtype, offset, size). Comments and blanks dropped."""
    out = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            cols = [c.strip() for c in line.split(",")]
            assert len(cols) >= 5, "%s: malformed row %r" % (path, line)
            out.append((cols[0], cols[1], cols[2], int(cols[3], 0), int(cols[4], 0)))
    return out


@pytest.fixture(scope="module")
def table():
    return rows(MAIN)


def test_every_copy_of_the_table_has_the_same_rows():
    want = rows(MAIN)
    for copy in COPIES:
        assert rows(copy) == want, (
            "%s has drifted from %s. Flashing that project would write ITS table to the "
            "panel and replace the live layout — including removing 'userdata' if this copy "
            "predates the split." % (copy, MAIN))


def test_nothing_overlaps_and_nothing_runs_off_the_end(table):
    end_of_previous = TABLE_OFFSET + TABLE_SIZE
    for name, _type, _sub, offset, size in table:
        assert offset >= end_of_previous, \
            "%s starts at 0x%X, inside the partition before it (ends 0x%X)" % (
                name, offset, end_of_previous)
        end_of_previous = offset + size
    assert end_of_previous <= FLASH_SIZE, \
        "the table runs past the end of a %d MB flash" % (FLASH_SIZE // 1024 // 1024)


def test_the_user_partition_exists_and_is_separate_from_the_build_output(table):
    by_name = {r[0]: r for r in table}
    assert "data" in by_name, "the build output partition is gone"
    assert "userdata" in by_name, (
        "'userdata' is missing. Runtime user files would go back into 'data', which "
        "`idf.py flash` rewrites whole — the loss of 18.09.2026 and 22.09.2026.")

    data = by_name["data"]
    user = by_name["userdata"]
    assert data[3] + data[4] <= user[3] or user[3] + user[4] <= data[3], \
        "data and userdata overlap"
    # LittleFS needs a handful of 4096-byte blocks before it can hold anything at all, and
    # the spare blocks are the wear budget of a file rewritten on every EQ edit.
    assert user[4] >= 64 * 1024, "userdata is too small to wear-level a rewritten file"
    assert user[4] % 4096 == 0, "userdata is not a whole number of LittleFS blocks"


def test_both_ota_slots_are_the_same_size_so_an_image_can_go_in_either(table):
    slots = [r for r in table if r[1] == "app"]
    assert len(slots) == 2, "rollback needs exactly two app slots"
    assert slots[0][4] == slots[1][4]
    for s in slots:
        assert s[3] % 0x10000 == 0, "%s is not on a 64 KB boundary" % s[0]


def test_the_co_processor_image_still_has_room_for_the_c6_firmware(table):
    slave = [r for r in table if r[0] == "slave_fw"]
    assert slave, "slave_fw is gone; tools/c6_updater reads the C6 image out of it"
    # The 3.0.7 CP image measured 1 118 624 bytes on 15.09.2026 (docs/C6_UPDATE.md).
    assert slave[0][4] >= 2 * 1024 * 1024


# ── The ESP32-S3 board's table (boards/s3_player) ──────────────────────────────────────────────
#
# Its own file, because two rows do not belong on it: no slave_fw (Wi-Fi is the S3's own radio)
# and the table at the default 0x8000 (the S3 bootloader starts at 0x0). Everything else is the
# same rule as above — above all, user files never where the build output goes.

S3_MAIN = os.path.join(ROOT, "partitions_s3_16mb.csv")
S3_TABLE_OFFSET = 0x8000


@pytest.fixture(scope="module")
def s3_table():
    return rows(S3_MAIN)


def test_s3_nothing_overlaps_and_nothing_runs_off_the_end(s3_table):
    end_of_previous = S3_TABLE_OFFSET + TABLE_SIZE
    for name, _type, _sub, offset, size in s3_table:
        assert offset >= end_of_previous, \
            "%s starts at 0x%X, inside the partition before it (ends 0x%X)" % (
                name, offset, end_of_previous)
        end_of_previous = offset + size
    assert end_of_previous <= FLASH_SIZE


def test_s3_the_user_partition_exists_and_is_separate_from_the_build_output(s3_table):
    by_name = {r[0]: r for r in s3_table}
    assert "data" in by_name and "userdata" in by_name
    data, user = by_name["data"], by_name["userdata"]
    assert data[3] + data[4] <= user[3] or user[3] + user[4] <= data[3], \
        "data and userdata overlap"
    assert user[4] >= 64 * 1024 and user[4] % 4096 == 0


def test_s3_two_equal_ota_slots_on_64k_boundaries(s3_table):
    slots = [r for r in s3_table if r[1] == "app"]
    assert len(slots) == 2 and slots[0][4] == slots[1][4]
    for s in slots:
        assert s[3] % 0x10000 == 0, "%s is not on a 64 KB boundary" % s[0]


def test_s3_carries_no_co_processor_image(s3_table):
    assert not [r for r in s3_table if r[0] == "slave_fw"], \
        "slave_fw is the P4's (the C6 image); the S3 has no co-processor"


def test_each_board_names_its_own_table():
    """The table a board builds with is its sdkconfig.board's, and that file must exist."""
    import re
    for board, want in (("p4_player", "partitions_p4_16mb.csv"), ("s3_player", "partitions_s3_16mb.csv")):
        with open(os.path.join(ROOT, "boards", board, "sdkconfig.board"), encoding="utf-8") as f:
            names = re.findall(r'^CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="([^"]+)"', f.read(), re.M)
        assert names == [want], "%s builds with %r" % (board, names)
        assert os.path.exists(os.path.join(ROOT, want))

