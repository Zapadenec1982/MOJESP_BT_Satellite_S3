"""bench.py's JSONL logs -> one CSV of every snapshot plus the settings each session started from.

usage: to_csv.py <out_dir> <image label> <log.jsonl>...
Writes <out_dir>/windows-<image>.csv and <out_dir>/settings-<image>.json (audio.*, the stream
and the panel's state at the start of each log; nothing about the network or the user).
"""
import csv
import json
import os
import sys

COLS = ["log", "t", "rate", "step", "audio.dsp_pct", "audio.dsp_max_pct", "audio.cpu_pct",
        "audio.underruns", "audio.loud_db", "audio.bass_db", "audio.treble_db", "audio.volume",
        "audio.eq_enabled", "audio.eq_headroom_db", "audio.bass_gr", "audio.lim_gr", "radio.state",
        "radio.format", "bt.state", "system.uptime", "system.heap_internal"]
KEEP = ("audio.", "radio.state", "radio.format", "radio.mode", "bt.state", "system.uptime",
        "system.boot_reason", "system.heap_internal", "system.heap_dma", "ui.screen", "_ota.version")


def main():
    out, image, logs = sys.argv[1], sys.argv[2], sys.argv[3:]
    os.makedirs(out, exist_ok=True)
    settings = {}
    with open(os.path.join(out, f"windows-{image}.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(COLS)
        for path in logs:
            name = os.path.basename(path)
            for line in open(path):
                d = json.loads(line)
                if "before" in d:
                    b = d["before"]
                    settings[name] = {
                        "found": d.get("found"), "source": d.get("source"), "steps": d.get("steps"),
                        "rates": d.get("rates"), "secs": d.get("secs"), "vol": d.get("vol"),
                        "state": {k: v for k, v in sorted(b.items()) if k.startswith(KEEP)},
                    }
                    continue
                if "summary" in d or "t" not in d:
                    continue
                step = d.get("step", d.get("tone"))
                row = [name, d["t"], d["rate"], step if isinstance(step, str) else json.dumps(step)]
                row += [d.get(k) for k in COLS[4:]]
                w.writerow(row)
    with open(os.path.join(out, f"settings-{image}.json"), "w") as f:
        json.dump(settings, f, ensure_ascii=False, indent=1)


if __name__ == "__main__":
    main()
