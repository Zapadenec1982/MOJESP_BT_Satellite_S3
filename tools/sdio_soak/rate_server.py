#!/usr/bin/env python3
"""Rate-controlled HTTP source for tools/sdio_soak (docs/SOAK_TEST.md).

Serves one endless HTTP body at a bit rate that can be changed WHILE the panel is
pulling it, so a soak run can walk the bit-rate axis without a reconnect and without a
rebuild. That matters: reconnecting changes the thing being measured, because the first
seconds after a TCP open are not the steady state the link fails in.

The rate lives in a one-line file (``--rate-file``, default ``rate.txt``) that is re-read
once a second; write a new number into it and the stream speeds up or slows down in
place. ``0`` means "as fast as the socket takes it" — the throughput ceiling.

    python rate_server.py --port 8000          # then: echo 320 > rate.txt

The payload is an incompressible pseudo-random block repeated forever. Nothing checks it
on the far side: the soak tool counts bytes, and the loss figure is measured
independently with ICMP from this same machine (docs/SOAK_TEST.md).
"""

import argparse
import os
import random
import socket
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

BLOCK = bytes(random.Random(0xC6C6).getrandbits(8) for _ in range(64 * 1024))

_rate_lock = threading.Lock()
_rate_kbps = 0
_rate_file = "rate.txt"


def _rate_watcher(period_s: float = 1.0) -> None:
    """Re-read the rate file so the axis can move under a live connection."""
    global _rate_kbps
    last = None
    while True:
        try:
            with open(_rate_file, "r", encoding="ascii") as fh:
                value = int(fh.read().strip() or "0")
        except (OSError, ValueError):
            value = None
        if value is not None and value != last:
            with _rate_lock:
                _rate_kbps = value
            print(f"[rate] {value} kbit/s" if value else "[rate] unthrottled", flush=True)
            last = value
        time.sleep(period_s)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):  # quieter than the default one-line-per-request
        sys.stderr.write("%s %s\n" % (self.address_string(), fmt % args))

    def do_GET(self):  # noqa: N802 — BaseHTTPRequestHandler's spelling
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()
        self.request.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

        sent = 0
        t0 = time.monotonic()
        tick = t0
        try:
            while True:
                with _rate_lock:
                    kbps = _rate_kbps
                if kbps <= 0:
                    self._chunk(BLOCK)
                    sent += len(BLOCK)
                    continue
                # Pace in 50 ms slices: small enough that the panel sees a smooth stream,
                # large enough that the write syscall is not the thing being measured.
                slice_bytes = max(1, kbps * 1000 // 8 // 20)
                view = 0
                while view < slice_bytes:
                    n = min(len(BLOCK), slice_bytes - view)
                    self._chunk(BLOCK[:n])
                    view += n
                sent += view
                tick += 0.05
                delay = tick - time.monotonic()
                if delay > 0:
                    time.sleep(delay)
                elif delay < -1.0:
                    tick = time.monotonic()  # fell behind badly; re-base instead of sprinting
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            dt = time.monotonic() - t0
            print(f"[conn] closed after {sent/1048576:.1f} MB in {dt:.0f} s "
                  f"({sent*8/1000/max(dt,1e-9):.0f} kbit/s)", flush=True)

    def _chunk(self, data: bytes) -> None:
        self.wfile.write(b"%x\r\n" % len(data))
        self.wfile.write(data)
        self.wfile.write(b"\r\n")


def main() -> int:
    global _rate_file
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--rate-file", default="rate.txt")
    ap.add_argument("--rate", type=int, default=0, help="initial rate, kbit/s (0 = unthrottled)")
    args = ap.parse_args()

    _rate_file = args.rate_file
    if not os.path.exists(_rate_file):
        with open(_rate_file, "w", encoding="ascii") as fh:
            fh.write(str(args.rate))

    threading.Thread(target=_rate_watcher, daemon=True).start()
    srv = ThreadingHTTPServer(("0.0.0.0", args.port), Handler)
    print(f"serving on :{args.port}, rate from {_rate_file}", flush=True)
    srv.serve_forever()
    return 0


if __name__ == "__main__":
    sys.exit(main())
