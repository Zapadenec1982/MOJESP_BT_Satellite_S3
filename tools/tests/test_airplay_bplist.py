"""Run the bounds-checked bplist parser (port/) against well-formed and hostile plists.

Well-formed bodies shaped like what senders POST (SETUP, TEARDOWN, SETRATEANCHORTIME)
must give exactly the results of the parser as published (upstream/). Malformed trailers,
indices, offsets and counts must be refused, under AddressSanitizer/UBSan where the host
compiler has them, and in a 32-bit build where it can make one: that is where a count
times a size wraps size_t.
Direct run: python tools/tests/test_airplay_bplist.py
"""
import os
from pathlib import Path
import plistlib
import struct
import subprocess
import tempfile
import unittest

try:
    from .test_airplay_mode import PORT, UP, compiler_command, production
except ImportError:
    from test_airplay_mode import PORT, UP, compiler_command, production


PREFIX = r'''
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plist.h"
static size_t host_strlcpy(char *out, const char *in, size_t n) {
    size_t len = strlen(in);
    if (n) { size_t c = len < n - 1 ? len : n - 1; memcpy(out, in, c); out[c] = 0; }
    return len;
}
#define strlcpy host_strlcpy
'''

MAIN = r'''
static unsigned sum(const uint8_t *d, size_t n) {
    unsigned s = 0;
    for (size_t i = 0; i < n; ++i) s = s * 31u + d[i];
    return s;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *f = fopen(argv[1], "rb"); assert(f);
    fseek(f, 0, SEEK_END); size_t n = (size_t)ftell(f); rewind(f);
    uint8_t *p = malloc(n); /* Exactly sized: a read past it is a sanitizer report. */
    assert(p && fread(p, 1, n, f) == n); fclose(f);
    size_t count = 0;
    bool ok = bplist_get_streams_count(p, n, &count);
    printf("streams all %d %zu\n", ok, count);
    for (size_t i = 0; i <= count && i < 10; ++i) {
        int64_t type = 0; size_t ek = 0, ei = 0, sh = 0;
        ok = bplist_get_stream_info(p, n, i, &type, &ek, &ei, &sh);
        printf("info %zu %d %" PRId64 " %zu %zu %zu\n", i, ok, type, ek, ei, sh);
        bplist_kv_info_t kv[16]; size_t kc = 0;
        ok = bplist_get_stream_kv_info(p, n, i, kv, 16, &kc);
        printf("kv %zu %d %zu", i, ok, kc);
        for (size_t k = 0; k < kc; ++k)
            printf(" %s:%u:%" PRId64 ":%zu", kv[k].key, kv[k].value_type,
                   kv[k].int_value, kv[k].value_len);
        printf("\n");
    }
    static const char *ints[] = {"type", "ct", "sr", "spf", "rate", "rtpTime", "et",
                                 "networkTimeSecs", "networkTimeFrac", "latencyMin"};
    for (size_t i = 0; i < sizeof(ints) / sizeof(ints[0]); ++i) {
        int64_t v = 0; ok = bplist_find_int(p, n, ints[i], &v);
        printf("int %s %d %" PRId64 "\n", ints[i], ok, ok ? v : 0);
    }
    double d = 0; ok = bplist_find_real(p, n, "rate", &d);
    printf("real rate %d %.6f\n", ok, ok ? d : 0.0);
    static const char *strs[] = {"timingProtocol", "deviceID", "sessionUUID"};
    for (size_t i = 0; i < sizeof(strs) / sizeof(strs[0]); ++i) {
        char s[64] = ""; ok = bplist_find_string(p, n, strs[i], s, sizeof(s));
        printf("string %s %d %s\n", strs[i], ok, ok ? s : "");
    }
    static const char *datas[] = {"ekey", "eiv", "shk"};
    for (size_t i = 0; i < 3; ++i) {
        uint8_t buf[512]; size_t len = 0;
        ok = bplist_find_data(p, n, datas[i], buf, sizeof(buf), &len);
        printf("data %s %d %zu %08x\n", datas[i], ok, ok ? len : 0, ok ? sum(buf, len) : 0);
        len = 0; ok = bplist_find_data_deep(p, n, datas[i], buf, sizeof(buf), &len);
        printf("deep %s %d %zu %08x\n", datas[i], ok, ok ? len : 0, ok ? sum(buf, len) : 0);
    }
    for (int64_t t = 96; t <= 103; t += 7) {
        uint8_t ek[64], ei[16], sh[32]; size_t ekl = 0, eil = 0, shl = 0;
        ok = bplist_find_stream_crypto(p, n, t, ek, sizeof(ek), &ekl, ei, sizeof(ei), &eil,
                                       sh, sizeof(sh), &shl);
        printf("crypto %" PRId64 " %d %zu %zu %zu %08x\n", t, ok, ekl, eil, shl,
               sum(ek, ekl) ^ sum(ei, eil) ^ sum(sh, shl));
    }
    free(p);
    return 0;
}
'''

SANITIZE = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-g"]


def build(directory, name, parser, extra):
    source = directory / (name + ".c")
    source.write_text(PREFIX + production(parser) + MAIN, encoding="utf-8")
    exe = directory / (name + (".exe" if os.name == "nt" else ""))
    command, env = compiler_command(source, exe)
    if Path(command[0]).name.lower() == "cl.exe":
        if extra:
            return None, env  # Sanitizer and -m32 builds are gcc/clang only.
    else:
        command += extra
    built = subprocess.run(command, env=env, cwd=directory,
                           capture_output=True, text=True, timeout=120)
    if built.returncode:
        if extra:
            return None, env  # This host cannot build the variant; the plain one still runs.
        raise AssertionError("bplist harness compilation failed:\n" + built.stdout + built.stderr)
    return exe, env


def plist_file(directory, data):
    path = directory / f"p{len(list(directory.glob('p*.bin')))}.bin"
    path.write_bytes(data)
    return path


def trailer(data):
    return dict(zip(("offset_size", "ref_size", "count", "top", "table"),
                    struct.unpack(">6xBBQQQ", data[-32:])))


def retrail(data, **changes):
    fields = trailer(data)
    fields.update(changes)
    return data[:-32] + struct.pack(">6xBBQQQ", fields["offset_size"], fields["ref_size"],
                                    fields["count"], fields["top"], fields["table"])


def offsets(data):
    t = trailer(data)
    size = t["offset_size"]
    return [int.from_bytes(data[t["table"] + i * size:t["table"] + (i + 1) * size], "big")
            for i in range(t["count"])]


def object_index(data, encoded):
    """Index of the object whose bytes start with `encoded`."""
    return next(i for i, at in enumerate(offsets(data)) if data[at:at + len(encoded)] == encoded)


def set_offset(data, index, value):
    t = trailer(data)
    at = t["table"] + index * t["offset_size"]
    return data[:at] + value.to_bytes(t["offset_size"], "big") + data[at + t["offset_size"]:]


def manual(objects, top=0):
    """A plist laid out by hand, one-byte offsets and refs."""
    body, at = b"bplist00", []
    for obj in objects:
        at.append(len(body))
        body += obj
    table = len(body)
    body += bytes(at)
    return body + struct.pack(">6xBBQQQ", 1, 1, len(objects), top, table)


def binary(value):
    return plistlib.dumps(value, fmt=plistlib.FMT_BINARY)


STREAM_96 = {"type": 96, "ct": 2, "sr": 44100, "spf": 352, "shk": bytes(range(32)),
             "controlPort": 50123, "latencyMin": 11025, "latencyMax": 88200,
             "audioFormat": 0x40000, "isMedia": True, "streamConnectionID": 0x123456789AB,
             "supportsDynamicStreamID": True, "audioMode": "default"}
STREAM_103 = {"type": 103, "ct": 4, "sr": 44100, "spf": 1024, "shk": bytes(range(1, 33)),
              "audioFormat": 0x1000000, "clientID": "4C1E2F7A-0000-4000-8000-0123456789AB"}

VALID = {
    "initial_setup": {"ekey": bytes(range(72)), "eiv": bytes(range(16)), "et": 32,
                      "timingProtocol": "PTP", "deviceID": "AA:BB:CC:DD:EE:FF",
                      "sessionUUID": "0F1E2D3C-4B5A-6978-8796-A5B4C3D2E1F0",
                      "timingPeerInfo": {"Addresses": ["192.168.1.20", "fe80::1"],
                                         "ID": "7A2B"},
                      "timingPeerList": [{"Addresses": ["192.168.1.21"], "ID": "7A2C",
                                          "SupportsClockPortMatchingOverride": True}],
                      "macAddress": "AA:BB:CC:DD:EE:FF", "model": "iPhone14,2",
                      "osBuildVersion": "22A3354", "sourceVersion": "870.14.1"},
    "stream_setup_realtime": {"streams": [STREAM_96]},
    "stream_setup_buffered": {"streams": [STREAM_103]},
    "two_streams": {"streams": [STREAM_96, STREAM_103]},
    "teardown_streams": {"streams": [{"type": 96}]},
    "teardown_full": {},
    "rate_anchor": {"rate": 1, "rtpTime": 0x8899AABB, "networkTimeSecs": 1760000000,
                    "networkTimeFrac": (1 << 63) + 12345, "networkTimeTimelineID": 77,
                    "networkTimeFlags": 0},
    "rate_real": {"rate": 1.0, "rtpTime": 3},
    # Over 255 bytes and 255 objects: two-byte offsets and refs.
    "large": {"streams": [STREAM_96], "ekey": bytes(300), "eiv": bytes(16),
              "list": list(range(1000, 1300))},
}


class AirplayBplistTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="airplay-bplist-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.dir = Path(cls.temp.name)
        port, upstream = PORT / "bplist_parser.c", UP / "plist/bplist_parser.c"
        cls.port, cls.env = build(cls.dir, "port", port, [])
        cls.upstream, _ = build(cls.dir, "upstream", upstream, [])
        # Hosts without sanitizer runtimes (MinGW, MSVC) run the plain build only.
        cls.variants = [exe for exe in (
            build(cls.dir, "port_asan", port, SANITIZE)[0],
            build(cls.dir, "port_asan32", port, SANITIZE + ["-m32"])[0],
            build(cls.dir, "port_32", port, ["-m32"])[0]) if exe] + [cls.port]

    def run_exe(self, exe, data):
        result = subprocess.run([str(exe), str(plist_file(self.dir, data))], env=self.env,
                                capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, f"{exe.name}: {result.stdout}{result.stderr}")
        return result.stdout

    def parse(self, data):
        """Results of every build of the port parser; they must all agree."""
        outputs = {exe.name: self.run_exe(exe, data) for exe in self.variants}
        first = outputs[self.port.name]
        for name, out in outputs.items():
            self.assertEqual(out, first, name)
        return {tuple(line.split()[:2]): line.split()[2:] for line in first.splitlines()}

    def test_well_formed_bodies_parse_as_upstream_does(self):
        for name, value in VALID.items():
            with self.subTest(name):
                data = binary(value)
                expected = self.run_exe(self.upstream, data)
                self.assertEqual(self.run_exe(self.port, data), expected)
                self.parse(data)

    def test_well_formed_results_are_the_expected_ones(self):
        out = self.parse(binary(VALID["stream_setup_realtime"]))
        self.assertEqual(out[("streams", "all")], ["1", "1"])
        self.assertEqual(out[("info", "0")], ["1", "96", "0", "0", "32"])
        self.assertIn("ct:1:2:0", " ".join(out[("kv", "0")]))
        self.assertEqual(out[("deep", "shk")][:2], ["1", "32"])
        out = self.parse(binary(VALID["initial_setup"]))
        self.assertEqual(out[("deep", "ekey")][:2], ["1", "72"])
        self.assertEqual(out[("string", "timingProtocol")], ["1", "PTP"])
        out = self.parse(binary(VALID["two_streams"]))
        self.assertEqual(out[("info", "1")][:2], ["1", "103"])

    def refused(self, data, *keys):
        out = self.parse(data)
        for key in keys:
            self.assertEqual(out[key][0], "0", key)
        return out

    def test_malformed_trailers_are_refused(self):
        base = binary(VALID["stream_setup_realtime"])
        size = len(base)
        for name, changes in {"top_object_past_table": {"top": (1 << 64) - 1},
                              "top_object_at_count": {"top": trailer(base)["count"]},
                              "huge_object_count": {"count": 0x7FFFFFFF},
                              "no_objects": {"count": 0},
                              "table_into_trailer": {"table": size - 33},
                              "table_in_header": {"table": 0},
                              "zero_ref_size": {"ref_size": 0}}.items():
            with self.subTest(name):
                out = self.refused(retrail(base, **changes), ("streams", "all"),
                                   ("deep", "shk"), ("crypto", "96"))
                self.assertEqual(out[("info", "0")][0], "0")

    def test_out_of_range_references_are_refused(self):
        base = binary(VALID["teardown_streams"])
        self.assertEqual(offsets(base)[trailer(base)["top"]], 8)
        streams = object_index(base, b"\xa1")  # The one-element streams array,
        stream = base[offsets(base)[streams] + 1]  # and the stream dict it names.
        cases = {
            # Root dict key refs follow its one-byte marker at offset 8.
            "key_index_past_table": base[:9] + b"\xff" + base[10:],
            "array_offset_past_buffer": set_offset(base, streams, 0xFF),
            "array_offset_into_table": set_offset(base, streams, trailer(base)["table"]),
            "stream_offset_past_buffer": set_offset(base, stream, 0xFF),
        }
        for name, data in cases.items():
            with self.subTest(name):
                out = self.parse(data)
                self.assertEqual(out[("info", "0")][0], "0")
                self.assertEqual(out[("crypto", "96")][0], "0")

    def test_oversized_counts_and_lengths_are_refused(self):
        streams = b"\x57streams"
        cases = {
            # dict_size * 2 * ref_size is 2^32: zero in a 32-bit size_t.
            "dict_count_wrapping_size_t": manual(
                [b"\xdf\x13" + (0x80000000).to_bytes(8, "big") + b"\x01\x01", b"\x54type"]),
            "streams_count_past_buffer": manual(
                [b"\xd1\x01\x02", streams, b"\xaf\x12" + (0x7FFFFFFF).to_bytes(4, "big") + b"\x03",
                 b"\xd0"]),
        }
        for name, data in cases.items():
            with self.subTest(name):
                self.refused(data, ("streams", "all"), ("info", "0"))
        # Callers walk streams by the count, so the count alone carries the cap.
        self.refused(binary({"streams": [{"type": 96}] * 9}), ("streams", "all"))
        self.assertEqual(self.parse(binary({"streams": [{"type": 96}] * 8}))[("streams", "all")],
                         ["1", "8"])
        # A data length near 2^64 wraps pos + len in a 64-bit size_t too.
        huge = manual([b"\xd1\x01\x02", streams, b"\xa1\x03", b"\xd2\x04\x05\x06\x07",
                       b"\x54type", b"\x54ekey", b"\x10\x60",
                       b"\x4f\x13" + (2**64 - 16).to_bytes(8, "big") + b"\x00"])
        out = self.parse(huge)
        self.assertEqual(out[("info", "0")], ["1", "96", "0", "0", "0"])
        self.assertEqual(out[("deep", "ekey")][0], "0")

    def test_self_referencing_dict_does_not_spin(self):
        # Fourteen values all naming the dict itself: 14^10 visits within the depth limit.
        loop = manual([b"\xde" + b"\x01" * 14 + b"\x00" * 14, b"\x51a"])
        self.refused(loop, ("deep", "ekey"), ("deep", "eiv"), ("deep", "shk"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
