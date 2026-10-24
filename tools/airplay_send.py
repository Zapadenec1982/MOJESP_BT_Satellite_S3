#!/usr/bin/env python3
"""airplay_send.py: play a test signal to the panel the way an AirPlay 1 (RAOP) sender does.

For when there is no iPhone on the bench. This speaks the RTSP iTunes and TuneBlade speak —
OPTIONS, ANNOUNCE, SETUP, RECORD, SET_PARAMETER, FLUSH, TEARDOWN — and streams realtime audio
over UDP the way they do: 352 stereo frames a packet at 44.1 kHz, as ALAC (uncompressed
"escape" frames, which every ALAC decoder must accept) or as raw L16; a sync packet on the
control port once a second; answers to the receiver's NTP timing requests; and retransmits of
whatever the receiver asks for again.

That reaches most of the receiver: RTSP, the realtime stream and its timing engine, the
decoder, the PCM ring, the audio.source switch, metadata, volume and the now-playing card.
What it does not reach is the AirPlay 2 half — HAP pairing, PTP, the buffered AAC stream an
iPhone uses for Apple Music. Only a real sender exercises that.

    python tools/airplay_send.py 192.168.1.216                     # 30 s of a 440 Hz tone
    python tools/airplay_send.py 192.168.1.216 --seconds 600       # a soak
    python tools/airplay_send.py 192.168.1.216 --codec pcm         # L16 instead of ALAC
    python tools/airplay_send.py 192.168.1.216 --volume -15        # also moves audio.volume
    python tools/airplay_send.py 192.168.1.216 --pause-at 10 --pause-for 5
    python tools/airplay_send.py 192.168.1.216 --cover art.jpg          # artwork too

Playing makes AirPlay the panel's source (the receiver switches audio.source to itself), so
whatever it was playing stops — that is the behaviour under test, not a side effect of it.

Standard library only. The receiver's half of every packet is in components/airplay2/upstream:
rtsp/rtsp_handlers.c, audio/audio_stream_realtime.c, network/ntp_clock.c.
"""

import argparse
import math
import random
import re
import socket
import struct
import sys
import threading
import time
from array import array

RATE = 44100
FRAMES = 352                  # per packet: ALAC's frame, and what every RAOP sender uses
NTP_EPOCH = 2208988800        # 1900 -> 1970, in seconds
USER_AGENT = "iTunes/12.12 (Windows; Microsoft Windows 10 x64) AppleWebKit/7610.4.3.1.8"


def ntp_now():
    """The sender's clock, NTP format. The receiver learns it through the timing exchange
    and anchors every frame to it through the sync packets, so the two must agree — any
    epoch would do, as long as it is this one everywhere."""
    t = time.time_ns()
    secs, rem = divmod(t, 1_000_000_000)
    return ((secs + NTP_EPOCH) << 32) | ((rem << 32) // 1_000_000_000)


# ── audio ────────────────────────────────────────────────────────────────────────────────────

class Tone:
    """A sine, the same on both channels, generated a packet at a time so that it has no seam
    anywhere — a looped buffer would put a step or a dip wherever it wraps, and that would be
    heard as the receiver's fault. 30 ms fades at the very start and end only, so the signal
    itself begins and ends without a click: any click heard is the receiver's."""

    def __init__(self, freq, total_frames, level_db=-12.0):
        self.step = 2 * math.pi * freq / RATE
        self.amp = 32767 * 10 ** (level_db / 20)
        self.total = total_frames
        self.fade = int(0.03 * RATE)
        self.pos = 0

    def next(self, n=FRAMES):
        out = array("h", bytes(4 * n))
        for i in range(n):
            k = self.pos + i
            g = min(1.0, k / self.fade, max(0.0, (self.total - k) / self.fade))
            v = int(self.amp * g * math.sin(self.step * k))
            out[2 * i] = v
            out[2 * i + 1] = v
        self.pos += n
        return out


def alac_escape_frame(samples):
    """One ALAC frame carrying `samples` (interleaved int16 stereo, FRAMES frames) uncompressed.

    Apple's decoder layout for a channel pair: tag ID_CPE (3 bits), instance (4), 12 unused
    bits, partial-frame flag (1), bytes shifted (2), escape flag (1) — 23 bits — then, with
    escape set, every sample as 16 raw bits, left then right, and ID_END (3 bits) after the
    element. 23 is not a multiple of 8, so the samples are not byte-aligned; building the
    whole frame as one integer is simpler than shifting bytes by hand.
    """
    assert len(samples) == 2 * FRAMES
    be = array("h", samples)
    if sys.byteorder == "little":
        be.byteswap()
    header = (1 << 20) | 1                     # 001 0000 000000000000 0 00 1
    nbits = 23 + 16 * 2 * FRAMES + 3
    value = (header << (16 * 2 * FRAMES + 3)) | (int.from_bytes(be.tobytes(), "big") << 3) | 7
    pad = (-nbits) % 8
    return (value << pad).to_bytes((nbits + pad) // 8, "big")


def l16_payload(samples):
    be = array("h", samples)
    if sys.byteorder == "little":
        be.byteswap()
    return be.tobytes()


# ── RTSP ─────────────────────────────────────────────────────────────────────────────────────

class Rtsp:
    def __init__(self, host, port=7000):
        self.sock = socket.create_connection((host, port), timeout=10)
        self.host = host
        self.local_ip = self.sock.getsockname()[0]
        self.cseq = 0
        self.session = None
        self.sid = str(random.randint(1 << 30, (1 << 31) - 1))
        self.url = f"rtsp://{self.local_ip}/{self.sid}"
        self.dacp_id = "%016X" % random.getrandbits(64)
        self.active_remote = str(random.getrandbits(32))
        self.buf = b""

    def request(self, method, url=None, headers=None, body=b"", ctype=None):
        self.cseq += 1
        lines = [f"{method} {url or self.url} RTSP/1.0", f"CSeq: {self.cseq}",
                 f"User-Agent: {USER_AGENT}", f"DACP-ID: {self.dacp_id}",
                 f"Active-Remote: {self.active_remote}", f"Client-Instance: {self.dacp_id}"]
        if self.session:
            lines.append(f"Session: {self.session}")
        for k, v in (headers or {}).items():
            lines.append(f"{k}: {v}")
        if body:
            lines.append(f"Content-Type: {ctype}")
            lines.append(f"Content-Length: {len(body)}")
        self.sock.sendall(("\r\n".join(lines) + "\r\n\r\n").encode() + body)
        return self._response()

    def _response(self):
        while b"\r\n\r\n" not in self.buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("receiver closed the RTSP connection")
            self.buf += chunk
        head, _, rest = self.buf.partition(b"\r\n\r\n")
        text = head.decode("utf-8", "replace")
        m = re.search(r"^Content-Length:\s*(\d+)", text, re.I | re.M)
        need = int(m.group(1)) if m else 0
        while len(rest) < need:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("receiver closed the RTSP connection")
            rest += chunk
        body, self.buf = rest[:need], rest[need:]
        status = int(text.split(" ", 2)[1])
        hdrs = {}
        for line in text.split("\r\n")[1:]:
            k, _, v = line.partition(":")
            hdrs[k.strip().lower()] = v.strip()
        if "session" in hdrs:
            self.session = hdrs["session"].split(";")[0]
        return status, hdrs, body

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def dmap(tag, payload):
    return tag.encode() + struct.pack("!I", len(payload)) + payload


# ── the session ──────────────────────────────────────────────────────────────────────────────

class Sender:
    def __init__(self, args):
        self.a = args
        self.running = True
        self.lock = threading.Lock()
        self.history = {}                        # seq -> RTP packet, for retransmits
        self.stats = {"timing": 0, "resend_asks": 0, "resent": 0, "missing": 0}
        self.ctrl = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.ctrl.bind(("", 0))
        self.timing = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.timing.bind(("", 0))
        self.data = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        for s in (self.ctrl, self.timing):
            s.settimeout(0.2)
        self.seq = random.randint(0, 0xFFFF)
        self.ts = random.randint(0, 0xFFFFFFFF)
        self.ssrc = random.getrandbits(32)
        self.latency = int(args.latency_ms * RATE / 1000)

    # The receiver asks for our clock every few seconds (ntp_clock.c) and needs the answer to
    # place anything at all: without it every frame is either early forever or late.
    def timing_loop(self):
        while self.running:
            try:
                req, addr = self.timing.recvfrom(128)
            except socket.timeout:
                continue
            except OSError:
                return
            if len(req) < 32 or (req[1] & 0x7F) != 0x52:
                continue
            received = ntp_now()
            resp = bytearray(32)
            resp[0], resp[1] = 0x80, 0xD3
            resp[2:4] = req[2:4]
            resp[8:16] = req[24:32]              # origin: the request's transmit time, echoed
            struct.pack_into("!QQ", resp, 16, received, ntp_now())
            self.timing.sendto(resp, addr)
            self.stats["timing"] += 1

    # 0x80 0xD5 <seq> <first> <count>: send those again, wrapped as 0x80 0xD6 <seq> <rtp>.
    def control_loop(self):
        while self.running:
            try:
                req, addr = self.ctrl.recvfrom(128)
            except socket.timeout:
                continue
            except OSError:
                return
            if len(req) < 8 or (req[1] & 0x7F) != 0x55:
                continue
            first, count = struct.unpack_from("!HH", req, 4)
            self.stats["resend_asks"] += 1
            for i in range(count):
                s = (first + i) & 0xFFFF
                with self.lock:
                    pkt = self.history.get(s)
                if pkt is None:
                    self.stats["missing"] += 1
                    continue
                self.ctrl.sendto(b"\x80\xd6" + struct.pack("!H", s) + pkt,
                                 (self.a.host, self.server_ctrl))
                self.stats["resent"] += 1

    def sync(self, first):
        # [4] the frame playing now (the next one sent, minus the latency), [8] our clock now,
        # [16] the next frame to be sent. The receiver anchors on [4] and [8] (0x54 in
        # audio_stream_realtime.c).
        pkt = struct.pack("!BBHIQI", 0x90 if first else 0x80, 0xD4, 0x0007,
                          (self.ts - self.latency) & 0xFFFFFFFF, ntp_now(), self.ts & 0xFFFFFFFF)
        self.ctrl.sendto(pkt, (self.a.host, self.server_ctrl))

    def packet(self, samples, first):
        payload = alac_escape_frame(samples) if self.a.codec == "alac" else l16_payload(samples)
        pkt = struct.pack("!BBHII", 0x80, 0xE0 if first else 0x60, self.seq, self.ts, self.ssrc) + payload
        with self.lock:
            self.history[self.seq] = pkt
            self.history.pop((self.seq - 1024) & 0xFFFF, None)
        self.data.sendto(pkt, (self.a.host, self.server_data))
        self.seq = (self.seq + 1) & 0xFFFF
        self.ts = (self.ts + FRAMES) & 0xFFFFFFFF

    def stream(self, src, seconds):
        """Send `seconds` of `src` in real time, paced to the sample clock, sync once a second."""
        total = int(seconds * RATE) // FRAMES
        t0 = time.perf_counter()
        next_sync = 0.0
        for n in range(total):
            if not self.running:
                return
            delay = t0 + n * FRAMES / RATE - time.perf_counter()
            if delay > 0:
                time.sleep(delay)
            if time.perf_counter() - t0 >= next_sync:
                self.sync(first=(n == 0))
                next_sync += 1.0
            self.packet(src.next(), first=(n == 0))

    def run(self):
        a = self.a
        r = Rtsp(a.host)
        self.rtsp = r
        print(f"RTSP to {a.host}:7000 from {r.local_ip}, codec {a.codec}, "
              f"latency {a.latency_ms} ms")

        st, h, _ = r.request("OPTIONS", "*")
        print(f"  OPTIONS   {st}  {h.get('public', '')[:70]}")

        if a.codec == "alac":
            rtpmap, fmtp = "AppleLossless", f"a=fmtp:96 {FRAMES} 0 16 40 10 14 2 255 0 0 {RATE}\r\n"
        else:
            rtpmap, fmtp = f"L16/{RATE}/2", ""
        sdp = (f"v=0\r\no=iTunes {r.sid} 0 IN IP4 {r.local_ip}\r\ns=iTunes\r\n"
               f"c=IN IP4 {a.host}\r\nt=0 0\r\nm=audio 0 RTP/AVP 96\r\n"
               f"a=rtpmap:96 {rtpmap}\r\n{fmtp}").encode()
        st, _, _ = r.request("ANNOUNCE", body=sdp, ctype="application/sdp")
        print(f"  ANNOUNCE  {st}")
        if st != 200:
            return 1

        transport = (f"RTP/AVP/UDP;unicast;interleaved=0-1;mode=record;"
                     f"control_port={self.ctrl.getsockname()[1]};"
                     f"timing_port={self.timing.getsockname()[1]}")
        threading.Thread(target=self.timing_loop, daemon=True).start()
        st, h, _ = r.request("SETUP", headers={"Transport": transport})
        tr = h.get("transport", "")
        print(f"  SETUP     {st}  {tr}")
        if st != 200:
            return 1
        ports = dict(re.findall(r"(server_port|control_port|timing_port)=(\d+)", tr))
        self.server_data = int(ports["server_port"])
        self.server_ctrl = int(ports["control_port"])
        threading.Thread(target=self.control_loop, daemon=True).start()

        st, h, _ = r.request("RECORD", headers={"Range": "npt=0-",
                                                "RTP-Info": f"seq={self.seq};rtptime={self.ts}"})
        print(f"  RECORD    {st}  Audio-Latency {h.get('audio-latency', '?')}")
        if st != 200:
            return 1

        if a.volume is not None:
            st, _, _ = r.request("SET_PARAMETER", body=f"volume: {a.volume:.6f}\r\n".encode(),
                                 ctype="text/parameters")
            print(f"  volume    {st}  {a.volume} dB")
        meta = dmap("mlit", dmap("minm", a.title.encode()) + dmap("asar", a.artist.encode())
                    + dmap("asal", a.album.encode()))
        st, _, _ = r.request("SET_PARAMETER", body=meta, ctype="application/x-dmap-tagged",
                             headers={"RTP-Info": f"rtptime={self.ts}"})
        print(f"  metadata  {st}  {a.title} / {a.artist} / {a.album}")
        start, end = self.ts, (self.ts + int(a.seconds * RATE)) & 0xFFFFFFFF
        st, _, _ = r.request("SET_PARAMETER",
                             body=f"progress: {start}/{self.ts}/{end}\r\n".encode(),
                             ctype="text/parameters")
        print(f"  progress  {st}  {a.seconds:.0f} s")
        if a.cover:
            # As iTunes sends artwork: a SET_PARAMETER whose body is the image itself.
            with open(a.cover, "rb") as f:
                img = f.read()
            ctype = "image/png" if img[:8] == b"\x89PNG\r\n\x1a\n" else "image/jpeg"
            st, _, _ = r.request("SET_PARAMETER", body=img, ctype=ctype,
                                 headers={"RTP-Info": f"rtptime={self.ts}"})
            print(f"  cover     {st}  {ctype}, {len(img)} bytes")

        src = Tone(a.tone, int(a.seconds * RATE))
        t0 = time.time()
        try:
            if a.pause_at and a.pause_at < a.seconds:
                self.stream(src, a.pause_at)
                st, _, _ = r.request("FLUSH", headers={"RTP-Info": f"seq={self.seq};rtptime={self.ts}"})
                print(f"  FLUSH     {st}  (pause {a.pause_for:.0f} s)")
                time.sleep(a.pause_for)
                st, _, _ = r.request("RECORD", headers={"Range": "npt=0-",
                                                        "RTP-Info": f"seq={self.seq};rtptime={self.ts}"})
                print(f"  RECORD    {st}  (resume)")
                self.stream(src, a.seconds - a.pause_at)
            else:
                self.stream(src, a.seconds)
        except KeyboardInterrupt:
            print("  interrupted")
        played = time.time() - t0
        try:
            st, _, _ = r.request("TEARDOWN")
            print(f"  TEARDOWN  {st}")
        except (OSError, ConnectionError) as e:
            print(f"  TEARDOWN  failed: {e}")
        self.running = False
        r.close()
        s = self.stats
        print(f"sent {played:.1f} s; timing requests answered {s['timing']}, "
              f"resend asks {s['resend_asks']} ({s['resent']} packets resent, "
              f"{s['missing']} no longer held)")
        # The receiver asks for our clock every 3 s; hearing nothing means it never locked, and
        # then nothing it received could have been scheduled.
        if s["timing"] == 0:
            print("VERDICT: the receiver never asked for the time — its NTP client did not start")
            return 2
        return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("host")
    ap.add_argument("--seconds", type=float, default=30.0)
    ap.add_argument("--codec", choices=("alac", "pcm"), default="alac")
    ap.add_argument("--tone", type=float, default=440.0, help="Hz")
    ap.add_argument("--volume", type=float, default=None,
                    help="dB, -30..0 (-144 mutes); omitted, the panel's volume is left alone")
    ap.add_argument("--latency-ms", type=float, default=2000.0,
                    help="how far ahead of playout the audio is sent, as iTunes does")
    ap.add_argument("--title", default="Тест AirPlay")
    ap.add_argument("--artist", default="airplay_send.py")
    ap.add_argument("--album", default="P4 bench")
    ap.add_argument("--cover", default=None, help="a JPEG or PNG to send as the artwork")
    ap.add_argument("--pause-at", type=float, default=0.0, help="FLUSH after this many seconds")
    ap.add_argument("--pause-for", type=float, default=5.0)
    return Sender(ap.parse_args()).run()


if __name__ == "__main__":
    sys.exit(main())
