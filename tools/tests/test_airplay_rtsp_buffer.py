"""Run the production RTSP receive-buffer framing with messages at and over its limits.

process_rtsp_buffer() NUL-terminates each message at buffer[total_len] while it is
dispatched. A message of exactly the buffer's capacity (4096, or the largest a grown buffer
completes) put that byte one past the allocation. The harness records every buffer
allocation and checks that the terminator lies inside it; where the host has
AddressSanitizer it also runs there.

A message over RTSP_MESSAGE_MAX - a cover a sender sent too big - closed the connection,
and the AirPlay session with it. Now its bytes are dropped as they arrive and the sender
gets its 200: the harness feeds such a message and a normal one after it in socket-sized
chunks, as the receive loop does, and checks that the buffer never grows for it, the 200
carries its CSeq, and the next message arrives whole.
Direct run: python tools/tests/test_airplay_rtsp_buffer.py
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

try:
    from .test_airplay_mode import PORT, compiler_command, function
except ImportError:
    from test_airplay_mode import PORT, compiler_command, function


PREFIX = r'''
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
typedef struct { int unused; } rtsp_conn_t;
static uint8_t *allocated;
static size_t allocated_size;
static void *heap_caps_malloc(size_t size, unsigned caps) {
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    allocated = malloc(size); allocated_size = size; return allocated;
}
static size_t dispatched, dispatches;
static int rtsp_dispatch(int socket, rtsp_conn_t *conn, const uint8_t *raw, size_t len) {
    (void)socket; (void)conn;
    /* The terminator is inside the buffer, and the message ends at it. */
    assert(raw == allocated && len < allocated_size && raw[len] == '\0');
    assert(!memcmp(raw, "SET_PARAMETER ", 14));
    dispatched = len; ++dispatches; return 0;
}
static int ok_cseq = -1, oks;
static size_t dispatches_at_ok;
static int rtsp_send_ok(int socket, rtsp_conn_t *conn, int cseq) {
    (void)socket; (void)conn;
    ok_cseq = cseq; ++oks; dispatches_at_ok = dispatches; return 0;
}
static const char *TAG = "rtsp_server";
#define ESP_LOGW(tag, ...) ((void)(tag))
'''

MAIN = r'''
static void message(uint8_t *buffer, size_t total, int cseq) {
    char header[160];
    int body = 0, n = 0;
    /* The Content-Length digits change the header length; settle it. */
    for (int i = 0; i < 3; ++i) {
        n = snprintf(header, sizeof(header),
                     "SET_PARAMETER rtsp://10.0.0.2/1 RTSP/1.0\r\nCSeq: %d\r\n"
                     "Content-Type: image/jpeg\r\n"
                     "Content-Length: %d\r\n\r\n", cseq, body);
        body = (int)total - n;
    }
    assert(n > 0 && (size_t)n + (size_t)body == total);
    memcpy(buffer, header, (size_t)n);
    memset(buffer + n, 'x', (size_t)body);
}
/* A message of `big` bytes (CSeq 9), then one of 300 (CSeq 10), fed in chunks of 1000 the
   way client_task's loop reads: grow to RTSP_BUFFER_LARGE when less than the headroom is
   left, and give up - close the connection - when a grown buffer is that full too. */
static void oversize_then_normal(size_t big) {
    const size_t small = 300, total = big + small;
    uint8_t *stream = malloc(total);
    assert(stream);
    message(stream, big, 9);
    message(stream + big, small, 10);
    client_slot_t slot = {.socket = 5};
    rtsp_conn_t conn; slot.conn = &conn;
    size_t capacity = RTSP_BUFFER_INITIAL, len = 0;
    uint8_t *buffer = grow_buffer(NULL, 0, capacity, 0);
    assert(buffer);
    for (size_t fed = 0; fed < total;) {
        if (len >= capacity - RTSP_RECV_HEADROOM) {
            assert(capacity < RTSP_BUFFER_LARGE);   /* the connection would close here */
            buffer = grow_buffer(buffer, capacity, RTSP_BUFFER_LARGE, len);
            assert(buffer);
            capacity = RTSP_BUFFER_LARGE;
        }
        size_t n = total - fed < 1000 ? total - fed : 1000;
        if (n > capacity - len) n = capacity - len;
        memcpy(buffer + len, stream + fed, n);
        len += n; fed += n;
        process_rtsp_buffer(&slot, buffer, &len);
    }
    /* Never held: the buffer did not grow, the 200 went out for CSeq 9 before the next
       message was dispatched, and that one arrived whole. */
    assert(capacity == RTSP_BUFFER_INITIAL);
    assert(oks == 1 && ok_cseq == 9 && dispatches_at_ok == 0);
    assert(dispatches == 1 && dispatched == small && len == 0 && slot.skip == 0);
    free(buffer);
    free(stream);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    const int which = atoi(argv[1]);
    if (which == 2) { oversize_then_normal(RTSP_MESSAGE_MAX + 1); return 0; }
    if (which == 3) { oversize_then_normal((size_t)3 * 1024 * 1024); return 0; }
    client_slot_t slot = {.socket = 5};
    rtsp_conn_t conn; slot.conn = &conn;
    size_t capacity = RTSP_BUFFER_INITIAL;
    uint8_t *buffer = grow_buffer(NULL, 0, capacity, 0); /* As client_task does. */
    assert(buffer);
    size_t total = capacity;
    if (which == 1) {
        buffer = grow_buffer(buffer, capacity, RTSP_BUFFER_LARGE, 0);
        assert(buffer);
        capacity = RTSP_BUFFER_LARGE;
        total = RTSP_MESSAGE_MAX;      /* the largest a grown buffer completes */
    }
    message(buffer, total, 9);
    size_t len = total;
    process_rtsp_buffer(&slot, buffer, &len);
    assert(dispatches == 1 && dispatched == total && len == 0 && oks == 0);
    free(buffer);
    return 0;
}
'''


def build(directory, name, extra):
    server = (PORT / "rtsp_server.c").read_text(encoding="utf-8")
    message = (PORT / "rtsp_message.c").read_text(encoding="utf-8")
    sizes = "\n".join(re.search(rf"^#define {n} .*$", server, re.MULTILINE).group(0)
                      for n in ("RTSP_BUFFER_INITIAL", "RTSP_BUFFER_LARGE",
                                "RTSP_RECV_HEADROOM", "RTSP_MESSAGE_MAX"))
    slot = server[server.index("typedef struct {\n  rtsp_conn_t *conn;"):
                  server.index("} client_slot_t;") + len("} client_slot_t;")]
    source = directory / (name + ".c")
    source.write_text("\n\n".join([
        PREFIX, sizes, slot,
        function(message, "const uint8_t *rtsp_find_header_end("),
        function(message, "int rtsp_parse_content_length("),
        function(message, "int rtsp_parse_cseq("),
        function(server, "static uint8_t *grow_buffer("),
        function(server, "static void process_rtsp_buffer("),
        MAIN]), encoding="utf-8")
    exe = directory / (name + (".exe" if os.name == "nt" else ""))
    command, env = compiler_command(source, exe)
    if extra and Path(command[0]).name.lower() == "cl.exe":
        return None, env
    if Path(command[0]).name.lower() == "cl.exe":
        command.append("/experimental:c11atomics")
    built = subprocess.run(command + extra, cwd=directory, env=env,
                           capture_output=True, text=True, timeout=120)
    if built.returncode:
        if extra:
            return None, env  # No sanitizer runtime here; the plain build still checks.
        raise AssertionError("RTSP buffer harness compilation failed:\n" +
                             built.stdout + built.stderr)
    return exe, env


class RtspBufferTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="airplay-rtsp-buffer-")
        cls.addClassCleanup(cls.temp.cleanup)
        directory = Path(cls.temp.name)
        plain, cls.env = build(directory, "plain", [])
        asan, _ = build(directory, "asan", ["-fsanitize=address", "-g"])
        cls.executables = [exe for exe in (plain, asan) if exe]

    def run_case(self, number):
        for exe in self.executables:
            result = subprocess.run([str(exe), str(number)], env=self.env,
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0,
                             f"{exe.name}: {result.stdout}{result.stderr}")

    def test_message_filling_the_initial_buffer(self): self.run_case(0)
    def test_largest_message_a_grown_buffer_completes(self): self.run_case(1)
    def test_message_one_byte_over_is_skipped_and_answered(self): self.run_case(2)
    def test_three_megabyte_cover_is_skipped_and_answered(self): self.run_case(3)


if __name__ == "__main__":
    unittest.main(verbosity=2)
