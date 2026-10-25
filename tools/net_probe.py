#!/usr/bin/env python3
"""net_probe.py: measure the P4 <-> C6 link the way an AirPlay 1 receiver would see it.

Talks to the panel's UDP probe (components/net_probe; flash the NETTEST image first).
Sends a steady stream shaped like AirPlay 1 audio — 352 stereo
16-bit frames per packet, 125.3 packets/s, ~1.4 KB each — asks for a small echo every so often
(the upstream traffic a receiver sends: resend and timing requests), and prints what the panel
reports once a second: loss, the longest run of lost packets, the longest silence, jitter,
the internal DMA memory, and esp_hosted's frame-copy counters.

    python tools/net_probe.py 192.168.1.216                     # 2 minutes, AirPlay-shaped
    python tools/net_probe.py 192.168.1.216 --ab 60 --duration 480
        # PSRAM / internal / PSRAM / internal for a minute each: where esp_hosted's frame
        # copies live is the one variable that changes between the phases
    python tools/net_probe.py 192.168.1.216 --burst 32 --psram off
        # 32 packets back to back every 32 periods: the burst a big TCP window produces

Standard library only. Wire layout: components/net_probe/include/net_probe/probe_wire.h
(tools/tests/test_net_probe_wire.py keeps the two in step).
"""

import argparse
import csv
import random
import socket
import statistics
import struct
import sys
import threading
import time

MAGIC = 0x504E3450          # "P4NP"
T_DATA, T_CTRL, T_REPORT, T_ECHO = 1, 2, 3, 4
F_ECHO = 0x01
CMD_START, CMD_SET_PSRAM, CMD_REPORT_NOW = 1, 2, 3

HDR = struct.Struct("<IBBHIIQ")
CTRL = struct.Struct("<BB")
REPORT_FIELDS = (
    "received duplicates late stale lost highest max_run max_gap_us jitter_us "
    "dma_free dma_min dma_largest internal_free "
    "frames_psram frames_internal frames_failed_rx frames_failed_tx "
    "heap_failed heap_failed_dma last_failed_size last_failed_caps "
    "hosted_mem_active frames_in_psram reserved uptime_s"
).split()
REPORT = struct.Struct("<21IBBHI")

AIRPLAY_RATE = 44100 / 352  # packets per second
AIRPLAY_SIZE = 1420         # 12-byte RTP header + 1408 bytes of 16-bit stereo audio

# What decides the verdict. RAOP (AirPlay 1) buffers ~2 s and asks again for what is missing,
# so scattered loss is survivable and a long silence is not; philippe44's receiver discards
# timing replies slower than 100 ms.
LOSS_OK_PCT, LOSS_RESEND_PCT = 2.0, 10.0
GAP_OK_MS, GAP_LIMIT_MS = 1000, 1500
RTT_OK_MS = 100


def now_us():
    return time.perf_counter_ns() // 1000


class Link:
    def __init__(self, host, port):
        self.addr = (socket.gethostbyname(host), port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        if hasattr(socket, "SIO_UDP_CONNRESET"):
            # Windows turns an ICMP "port unreachable" into an exception on the next recv.
            self.sock.ioctl(socket.SIO_UDP_CONNRESET, False)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
        self.sock.bind(("", 0))
        self.sock.settimeout(0.2)
        self.lock = threading.Lock()
        self.report = None
        self.reports = []
        self.echoes = []        # (session, seq, rtt_us)
        self.running = True
        threading.Thread(target=self._rx, daemon=True).start()

    def _rx(self):
        while self.running:
            try:
                data, _ = self.sock.recvfrom(2048)
            except (socket.timeout, OSError):
                continue
            if len(data) < HDR.size:
                continue
            magic, typ, _flags, _hlen, session, seq, t_us = HDR.unpack_from(data)
            if magic != MAGIC:
                continue
            if typ == T_REPORT and len(data) >= HDR.size + REPORT.size:
                r = dict(zip(REPORT_FIELDS, REPORT.unpack_from(data, HDR.size)))
                r.update(session=session, report_no=seq, at=time.monotonic())
                with self.lock:
                    self.report = r
                    self.reports.append(r)
            elif typ == T_ECHO:
                rtt = now_us() - t_us
                with self.lock:
                    self.echoes.append((session, seq, rtt))

    def send(self, typ, session, seq, flags=0, payload=b""):
        self.sock.sendto(HDR.pack(MAGIC, typ, flags, HDR.size, session, seq, now_us()) + payload,
                         self.addr)

    def ctrl(self, session, cmd, arg=0):
        self.send(T_CTRL, session, 0, payload=CTRL.pack(cmd, arg))

    def latest(self):
        with self.lock:
            return dict(self.report) if self.report else None

    def wait_report(self, pred, timeout):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            r = self.latest()
            if r is not None and pred(r):
                return r
            time.sleep(0.02)
        return None

    def echoes_of(self, session):
        with self.lock:
            return [rtt for s, _q, rtt in self.echoes if s == session]


def pct(values, q):
    if not values:
        return None
    v = sorted(values)
    return v[min(len(v) - 1, int(q * len(v)))]


def fmt_ms(us):
    return "-" if us is None else f"{us / 1000:.0f}"


def start_session(link, psram):
    session = random.getrandbits(32) or 1
    for _ in range(10):
        link.ctrl(session, CMD_START)
        if link.wait_report(lambda r: r["session"] == session, 1.0):
            break
    else:
        sys.exit("The panel did not answer. Is the NETTEST image flashed, "
                 "the IP right, and nothing on this PC blocking UDP?")
    if psram is not None:
        want = 1 if psram else 0
        for _ in range(10):
            link.ctrl(session, CMD_SET_PSRAM, want)
            r = link.wait_report(lambda r: r["session"] == session and r["frames_in_psram"] == want, 1.0)
            if r:
                break
        else:
            print("  ! the panel did not confirm the PSRAM switch (hosted_mem inactive?)")
    return session


def run_phase(link, name, seconds, args, psram, writer):
    session = start_session(link, psram)
    base = link.latest()
    print(f"\n== {name}: {seconds:.0f} s, {args.rate:.1f} pkt/s x {args.size} B, burst {args.burst}, "
          f"frame copies in {'PSRAM' if base['frames_in_psram'] else 'internal RAM'} "
          f"(hosted_mem {'on' if base['hosted_mem_active'] else 'OFF'})")
    print("   t    sent    rx  lost  run  gap_ms jit_ms | echo p50/p90/max ms  | "
          "DMA free/min/big   | copies psram/int  drop rx/tx  dma_fail")

    filler = bytes(max(0, args.size - HDR.size))
    period = args.burst / args.rate
    t0 = time.perf_counter()
    next_t, next_print = t0, t0 + args.print_every
    seq = echo_requests = 0

    while True:
        now = time.perf_counter()
        if now - t0 >= seconds:
            break
        if now < next_t:
            if next_t - now > 0.002:
                time.sleep(next_t - now - 0.0015)
            continue
        for _ in range(args.burst):
            flags = F_ECHO if args.echo_every and seq % args.echo_every == 0 else 0
            echo_requests += bool(flags)
            link.send(T_DATA, session, seq, flags, filler)
            seq += 1
        next_t += period
        if now >= next_print:
            next_print += args.print_every
            show(link, session, now - t0, seq, base, writer, name)

    time.sleep(0.5)                          # stragglers
    t_end = time.monotonic()
    link.ctrl(session, CMD_REPORT_NOW)
    r = link.wait_report(lambda r: r["session"] == session and r["at"] > t_end, 3.0) or link.latest()
    show(link, session, time.perf_counter() - t0, seq, base, writer, name)
    rtts = link.echoes_of(session)
    return summarise(name, seq, echo_requests, rtts, base, r)


def show(link, session, elapsed, sent, base, writer, phase):
    r = link.latest()
    if not r or r["session"] != session:
        return
    rtts = link.echoes_of(session)
    print(f"{elapsed:5.0f} {sent:7d} {r['received']:6d} {r['lost']:5d} {r['max_run']:4d} "
          f"{r['max_gap_us'] / 1000:7.0f} {r['jitter_us'] / 1000:6.1f} | "
          f"{fmt_ms(pct(rtts, .5)):>4}/{fmt_ms(pct(rtts, .9)):>4}/{fmt_ms(max(rtts) if rtts else None):>5}      | "
          f"{r['dma_free']:6d}/{r['dma_min']:6d}/{r['dma_largest']:6d} | "
          f"{r['frames_psram'] - base['frames_psram']:7d}/{r['frames_internal'] - base['frames_internal']:<6d} "
          f"{r['frames_failed_rx'] - base['frames_failed_rx']:4d}/{r['frames_failed_tx'] - base['frames_failed_tx']:<4d} "
          f"{r['heap_failed_dma'] - base['heap_failed_dma']:6d}")
    if writer:
        writer.writerow({"phase": phase, "elapsed_s": round(elapsed, 1), "sent": sent,
                         **{k: r[k] for k in REPORT_FIELDS}})


def summarise(name, sent, echo_requests, rtts, base, r):
    received = r["received"] if r else 0
    loss = 100.0 * (sent - received) / sent if sent else 0.0
    return {
        "phase": name, "sent": sent, "received": received, "loss_pct": loss,
        "max_run": r["max_run"] if r else None,
        "max_gap_ms": r["max_gap_us"] / 1000 if r else None,
        "jitter_ms": r["jitter_us"] / 1000 if r else None,
        "echo_loss_pct": 100.0 * (echo_requests - len(rtts)) / echo_requests if echo_requests else 0.0,
        "rtt_p50": pct(rtts, .5), "rtt_p90": pct(rtts, .9), "rtt_max": max(rtts) if rtts else None,
        "dma_min": r["dma_min"] if r else None,
        "in_psram": bool(r["frames_in_psram"]) if r else None,
        "drop_rx": r["frames_failed_rx"] - base["frames_failed_rx"] if r else None,
        "drop_tx": r["frames_failed_tx"] - base["frames_failed_tx"] if r else None,
        "dma_fail": r["heap_failed_dma"] - base["heap_failed_dma"] if r else None,
    }


def verdict(s):
    notes = []
    if s["loss_pct"] <= LOSS_OK_PCT:
        notes.append("loss OK")
    elif s["loss_pct"] <= LOSS_RESEND_PCT:
        notes.append("loss needs RAOP resends")
    else:
        notes.append("LOSS TOO HIGH")
    gap = s["max_gap_ms"] or 0
    notes.append("gap OK" if gap <= GAP_OK_MS else "gap marginal" if gap <= GAP_LIMIT_MS else "GAP TOO LONG")
    p90 = s["rtt_p90"]
    notes.append("timing OK" if p90 is not None and p90 / 1000 <= RTT_OK_MS else "TIMING RTT SLOW")
    if s["drop_rx"] or s["drop_tx"]:
        notes.append(f"esp_hosted dropped {s['drop_rx']}/{s['drop_tx']} frames")
    return ", ".join(notes)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("host", help="the panel's IP or mDNS name")
    ap.add_argument("--port", type=int, default=5005)
    ap.add_argument("--rate", type=float, default=AIRPLAY_RATE, help="packets per second")
    ap.add_argument("--size", type=int, default=AIRPLAY_SIZE, help="UDP payload bytes per packet")
    ap.add_argument("--duration", type=float, default=120, help="seconds, all phases together")
    ap.add_argument("--burst", type=int, default=1, help="packets sent back to back per slot")
    ap.add_argument("--echo-every", type=int, default=32, help="ask for an echo every N packets (0: never)")
    ap.add_argument("--psram", choices=["on", "off", "keep"], default="keep",
                    help="where esp_hosted's frame copies go for a single-phase run")
    ap.add_argument("--ab", type=float, default=0,
                    help="alternate PSRAM / internal every N seconds for the whole duration")
    ap.add_argument("--print-every", type=float, default=5)
    ap.add_argument("--csv", help="write every report here")
    args = ap.parse_args()
    if not (1 <= args.burst <= 256) or args.size < HDR.size or args.size > 1472:
        sys.exit("--burst 1..256, --size 24..1472")

    link = Link(args.host, args.port)
    writer = None
    csv_file = open(args.csv, "w", newline="", encoding="utf-8") if args.csv else None
    if csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=["phase", "elapsed_s", "sent"] + REPORT_FIELDS)
        writer.writeheader()

    phases = []
    if args.ab > 0:
        n = max(2, int(args.duration // args.ab))
        phases = [(f"{i + 1}:{'PSRAM' if i % 2 == 0 else 'internal'}", args.ab, i % 2 == 0) for i in range(n)]
    else:
        psram = {"on": True, "off": False, "keep": None}[args.psram]
        phases = [("run", args.duration, psram)]

    results = []
    try:
        for name, seconds, psram in phases:
            results.append(run_phase(link, name, seconds, args, psram, writer))
    except KeyboardInterrupt:
        print("\ninterrupted")
    finally:
        link.running = False
        if csv_file:
            csv_file.close()

    print("\nphase          sent   loss%  run  gap_ms  jit_ms  echo_loss%  rtt p50/p90/max ms  "
          "DMA_min  drop rx/tx  dma_fail  verdict")
    for s in results:
        print(f"{s['phase']:<12} {s['sent']:6d} {s['loss_pct']:6.2f} {s['max_run'] or 0:4d} "
              f"{s['max_gap_ms'] or 0:7.0f} {s['jitter_ms'] or 0:7.1f} {s['echo_loss_pct']:10.1f}  "
              f"{fmt_ms(s['rtt_p50']):>4}/{fmt_ms(s['rtt_p90']):>4}/{fmt_ms(s['rtt_max']):>5}        "
              f"{s['dma_min'] or 0:7d} {s['drop_rx'] or 0:5d}/{s['drop_tx'] or 0:<4d} {s['dma_fail'] or 0:8d}  "
              f"{verdict(s)}")


if __name__ == "__main__":
    main()
