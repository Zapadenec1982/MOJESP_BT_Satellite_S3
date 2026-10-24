"""DSP load at 48/96/192 kHz on the panel, the way PLAN_SOUND_UX S3 measured 192 kHz on 14.10.

A mono 1 kHz tone WAV at each rate is served from this PC and pushed to the panel's DLNA
renderer (no credentials: panel-dlna-push-test). For each rate: the stream starts, the volume
goes to a low test level, Bass/Treble go to each setting in turn (POST /api/settings, which
answers without credentials only while the customer keeps the web login off), and for `--secs`
seconds a fresh WS snapshot is taken every 0.5 s (audio.dsp_pct and audio.dsp_max_pct are
published without notify, so only a new connection's snapshot carries them). Every snapshot
is logged; consecutive identical windows are counted once.

Afterwards the panel is put back as it was found: volume, Bass, Treble, the DLNA stream
stopped, audio.source restored.

usage: bench.py <label> [--rates 48000,96000,192000] [--tones 0,6] [--secs 60] [--vol 4]
"""
import argparse
import base64
import http.server
import json
import math
import os
import signal
import socket
import statistics
import struct
import sys
import threading
import time
import urllib.request
import wave
from array import array

PANEL = "modesp-2c4c.local"
HERE = os.path.dirname(os.path.abspath(__file__))
WAVDIR = os.path.join(HERE, "wav")
PORT = 8000
KEYS = ["audio.dsp_pct", "audio.dsp_max_pct", "audio.cpu_pct", "audio.underruns", "audio.dsp_faults",
        "audio.loud_db", "audio.bass_db", "audio.treble_db", "audio.volume", "audio.vol_eff",
        "audio.eq_headroom_db", "audio.lim_gr", "audio.bass_gr", "audio.out_clamps", "audio.source",
        "audio.state", "audio.loudness", "audio.eq_enabled", "radio.state", "radio.format", "radio.mode",
        "bt.state", "system.uptime", "system.boot_reason", "system.heap_internal",
        "system.main_loop_max_ms", "system.main_loop_overruns", "audio.level_l", "audio.level_r"]


def lan_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect((socket.gethostbyname(PANEL), 80))
    ip = s.getsockname()[0]
    s.close()
    return ip


def make_wav(rate, secs, path, hz=1000.0, dbfs=-20.0):
    if os.path.exists(path) and os.path.getsize(path) > rate * 2 * secs:
        return
    amp = 32767.0 * 10 ** (dbfs / 20.0)
    w = wave.open(path + ".part", "wb")
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(rate)
    step = 2.0 * math.pi * hz / rate
    chunk = rate  # one second at a time
    n0 = 0
    for _ in range(secs):
        a = array("h", (int(round(amp * math.sin(step * (n0 + i)))) for i in range(chunk)))
        if sys.byteorder != "little":
            a.byteswap()
        w.writeframes(a.tobytes())
        n0 += chunk
    w.close()
    os.replace(path + ".part", path)


class Quiet(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=WAVDIR, **k)

    def log_message(self, fmt, *args):
        sys.stderr.write("http: " + (fmt % args) + "\n")


def serve():
    srv = http.server.ThreadingHTTPServer(("0.0.0.0", PORT), Quiet)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv


def snapshot(timeout=5.0):
    s = socket.create_connection((PANEL, 80), timeout=timeout)
    key = base64.b64encode(os.urandom(16)).decode()
    s.sendall((f"GET /ws HTTP/1.1\r\nHost: {PANEL}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
               f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
    head = b""
    while b"\r\n\r\n" not in head:
        c = s.recv(1)
        if not c:
            raise IOError("ws closed during handshake")
        head += c
    if b" 101 " not in head.split(b"\r\n")[0]:
        raise IOError(head[:80])

    def frame():
        h = s.recv(2, socket.MSG_WAITALL)
        op, n = h[0] & 0x0F, h[1] & 0x7F
        if n == 126:
            n = struct.unpack(">H", s.recv(2, socket.MSG_WAITALL))[0]
        elif n == 127:
            n = struct.unpack(">Q", s.recv(8, socket.MSG_WAITALL))[0]
        data = b""
        while len(data) < n:
            part = s.recv(n - len(data))
            if not part:
                raise IOError("ws closed mid-frame")
            data += part
        return op, data

    state = {}
    try:
        for _ in range(20):
            op, data = frame()
            if op != 1:
                continue
            try:
                msg = json.loads(data)
            except ValueError:
                continue
            snap = msg.get("state", msg) if isinstance(msg, dict) else {}
            if isinstance(snap, dict) and len(snap) > 20:
                state = snap
                break
    finally:
        s.close()
    return state


def soap(action, body):
    xml = ('<?xml version="1.0"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" '
           's:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/"><s:Body>'
           f'<u:{action} xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">'
           f'<InstanceID>0</InstanceID>{body}</u:{action}></s:Body></s:Envelope>').encode()
    req = urllib.request.Request(f"http://{PANEL}/AVTransport/Control", data=xml, method="POST", headers={
        "Content-Type": 'text/xml; charset="utf-8"',
        "SOAPAction": f'"urn:schemas-upnp-org:service:AVTransport:1#{action}"'})
    with urllib.request.urlopen(req, timeout=10) as r:
        return r.status


def settings(d):
    req = urllib.request.Request(f"http://{PANEL}/api/settings", data=json.dumps(d).encode(), method="POST",
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=10) as r:
        return r.status


def wait_for(pred, what, secs=30):
    t0 = time.time()
    st = {}
    while time.time() - t0 < secs:
        try:
            st = snapshot()
        except Exception as e:  # noqa: BLE001 - keep trying while the panel switches
            print("  snapshot failed:", e)
            time.sleep(1)
            continue
        if pred(st):
            return st
        time.sleep(0.5)
    raise RuntimeError(f"timed out waiting for {what}; last: " +
                       json.dumps({k: st.get(k) for k in ('audio.source', 'radio.state', 'radio.format')}))


def run(label, rate, step, secs, log):
    rows = []
    t_end = time.time() + secs
    last = None
    while time.time() < t_end:
        t = time.time()
        try:
            st = snapshot()
        except Exception as e:  # noqa: BLE001
            print("  snapshot failed:", e)
            time.sleep(0.5)
            continue
        row = {"t": round(t, 3)}
        row.update({k: st.get(k) for k in KEYS})
        log.write(json.dumps({"label": label, "rate": rate, "step": step, **row}, ensure_ascii=False) + "\n")
        log.flush()
        sig = (row["audio.dsp_pct"], row["audio.dsp_max_pct"], row["audio.cpu_pct"])
        if sig != last:
            rows.append(row)
            last = sig
        time.sleep(max(0.0, 0.5 - (time.time() - t)))
    return rows


def summary(rows):
    dsp = [r["audio.dsp_pct"] for r in rows if isinstance(r["audio.dsp_pct"], (int, float))]
    mx = [r["audio.dsp_max_pct"] for r in rows if isinstance(r["audio.dsp_max_pct"], (int, float))]
    cpu = [r["audio.cpu_pct"] for r in rows if isinstance(r["audio.cpu_pct"], (int, float))]
    und = [r["audio.underruns"] for r in rows if isinstance(r["audio.underruns"], (int, float))]
    up = [r["system.uptime"] for r in rows if isinstance(r["system.uptime"], (int, float))]
    q = statistics.quantiles(mx, n=10) if len(mx) >= 10 else [float("nan")] * 9
    return {
        "windows": len(rows),
        "dsp_avg": round(statistics.fmean(dsp), 2) if dsp else None,
        "dsp_min": round(min(dsp), 2) if dsp else None,
        "dsp_max": round(max(dsp), 2) if dsp else None,
        "blk_median": round(statistics.median(mx), 1) if mx else None,
        "blk_p10": round(q[0], 1), "blk_p90": round(q[8], 1),
        "blk_min": round(min(mx), 1) if mx else None,
        "blk_peak": round(max(mx), 1) if mx else None,
        "cpu_avg": round(statistics.fmean(cpu), 1) if cpu else None,
        "underruns": (und[-1] - und[0]) if len(und) > 1 else None,
        "rebooted": any(b < a for a, b in zip(up, up[1:])),
        "bt": sorted({r["bt.state"] for r in rows}),
        "loud_db": sorted({r["audio.loud_db"] for r in rows})[:3],
        "format": sorted({r["radio.format"] for r in rows}),
    }


def main():
    # A background job's SIGINT is ignored; SIGTERM must still put the panel back.
    signal.signal(signal.SIGTERM, lambda *a: sys.exit(143))
    ap = argparse.ArgumentParser()
    ap.add_argument("label")
    ap.add_argument("--rates", default="48000,96000,192000")
    ap.add_argument("--tones", default="0,6")
    ap.add_argument("--variants", default="", help='JSON list of settings dicts, instead of --tones')
    ap.add_argument("--secs", type=int, default=60)
    ap.add_argument("--vol", type=int, default=4)
    args = ap.parse_args()
    rates = [int(r) for r in args.rates.split(",")]
    if args.variants:
        steps = json.loads(args.variants)
    else:
        steps = [{"audio.bass_db": int(t), "audio.treble_db": int(t)} for t in args.tones.split(",")]
    os.makedirs(WAVDIR, exist_ok=True)
    wav_secs = len(steps) * (args.secs + 15) + 30
    for r in rates:
        make_wav(r, wav_secs, os.path.join(WAVDIR, f"tone{r // 1000}.wav"))
    ip = lan_ip()
    srv = serve()

    before = snapshot()
    touched = {"audio.volume", "audio.bass_db", "audio.treble_db"} | {k for st in steps for k in st}
    keep = {k: before.get(k) for k in sorted(touched)}
    keep_src = before.get("audio.source")
    print("found:", json.dumps(keep), "source", keep_src, "uptime", before.get("system.uptime"),
          "bt", before.get("bt.state"))
    stamp = time.strftime("%Y%m%d-%H%M%S")
    logpath = os.path.join(HERE, f"{args.label}-{stamp}.jsonl")
    results = []
    try:
        with open(logpath, "w") as log:
            log.write(json.dumps({"label": args.label, "found": keep, "source": keep_src, "before": before,
                                  "rates": rates, "steps": steps, "secs": args.secs, "vol": args.vol,
                                  "started": stamp}, ensure_ascii=False) + "\n")
            for r in rates:
                url = f"http://{ip}:{PORT}/tone{r // 1000}.wav"
                print(f"== {r} Hz: {url}")
                # A new listening session for every rate: the output clock is chosen by a
                # session's first track and then stays (audio_module.cpp adopt_clock), so a
                # second push in the same session is resampled to the first one's rate.
                soap("Stop", "")
                time.sleep(1.0)
                settings({"audio.source": 0})
                wait_for(lambda st: st.get("audio.source") == 0 and st.get("audio.state") == "off", "source off", 20)
                time.sleep(1.0)
                soap("SetAVTransportURI", f"<CurrentURI>{url}</CurrentURI><CurrentURIMetaData></CurrentURIMetaData>")
                soap("Play", "<Speed>1</Speed>")
                khz = f"{r / 1000:.1f} kHz"
                wait_for(lambda st: st.get("audio.source") == 5 and st.get("radio.state") == "playing"
                         and khz in str(st.get("radio.format")), f"{khz} playing", 40)
                settings({"audio.volume": args.vol})
                for step in steps:
                    # every step starts from what was found, then takes its own settings
                    want = {k: v for k, v in keep.items() if k != "audio.volume" and v is not None}
                    want.update(step)
                    settings(want)
                    wait_for(lambda st: all(st.get(k) == v for k, v in want.items()), f"step {step}", 10)
                    time.sleep(4.0)          # crossfades and the loudness glide settle
                    tag = json.dumps(step, sort_keys=True)
                    rows = run(args.label, r, tag, args.secs, log)
                    s = summary(rows)
                    s.update({"rate": r, "step": step})
                    results.append(s)
                    print("  ", json.dumps(s, ensure_ascii=False))
            log.write(json.dumps({"summary": results}, ensure_ascii=False) + "\n")
    finally:
        print("restoring", json.dumps(keep), "source", keep_src)
        try:
            settings({k: v for k, v in keep.items() if v is not None})
            soap("Stop", "")
            time.sleep(1.0)
            if keep_src is not None:
                settings({"audio.source": keep_src})
        except Exception as e:  # noqa: BLE001
            print("RESTORE FAILED:", e)
        time.sleep(1.0)
        after = snapshot()
        print("after:", json.dumps({k: after.get(k) for k in keep}), "source", after.get("audio.source"),
              "uptime", after.get("system.uptime"))
        srv.shutdown()
    print("log:", logpath)


if __name__ == "__main__":
    main()
