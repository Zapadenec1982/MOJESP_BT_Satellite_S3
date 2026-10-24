#!/usr/bin/env python3
"""Bounded onboard microphone diagnostics; USB serial opened WITHOUT resetting P4.

capture arms for the next SD/DLNA playback gate opening, waits, and downloads WAV.
--trigger-after N captures ambient sound after N seconds (microphone bring-up).
dump retries a frozen download. status/cancel do not change playback or volume.
Requires pyserial, installed in the project's ESP-IDF Python environment.
"""
import argparse
import json
from pathlib import Path
import re
import time


def fields(line):
    return dict(re.findall(r"(\w+)=([^\s]+)", line))


class Download:
    """Strict sequenced WAV transport; reject gaps, truncation and corruption."""
    def __init__(self):
        self.meta = None
        self.data = bytearray()

    def feed(self, line):
        if "@MIC_BEGIN " in line:
            self.meta = fields(line)
            self.data.clear()
            if not 44 <= int(self.meta["bytes"]) <= 192044:
                raise ValueError("invalid capture size")
        elif "@MIC_DATA " in line:
            if self.meta is None:
                raise ValueError("data without header")
            parts = line.split("@MIC_DATA ", 1)[1].split()
            if len(parts) != 2 or int(parts[0]) != len(self.data):
                raise ValueError("missing or out-of-order capture chunk")
            self.data.extend(bytes.fromhex(parts[1]))
            if len(self.data) > int(self.meta["bytes"]):
                raise ValueError("capture exceeds declared size")
        elif "@MIC_END " in line:
            if self.meta is None or len(self.data) != int(self.meta["bytes"]):
                raise ValueError("truncated capture")
            checksum = 2166136261
            for value in self.data:
                checksum = ((checksum ^ value) * 16777619) & 0xffffffff
            if checksum != int(fields(line)["fnv1a"], 16):
                raise ValueError("capture checksum mismatch")
            if self.data[:4] != b"RIFF" or self.data[8:12] != b"WAVE":
                raise ValueError("capture is not WAV")
            return True
        return False


def main():
    import serial
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["capture", "dump", "status", "cancel", "gain"])
    parser.add_argument("--port", default="COM12")
    parser.add_argument("--output", type=Path, default=Path("build/mic-capture"))
    parser.add_argument("--trigger-after", type=float)
    parser.add_argument("--gain", type=int, choices=range(0, 31, 3), default=18)
    args = parser.parse_args()
    if args.trigger_after is not None and not 0.5 <= args.trigger_after <= 35:
        parser.error("--trigger-after must be 0.5..35 seconds after ARMED")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    uart_path = args.output.with_suffix(".uart.log")
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = args.port, 115200, 0.1
    ser.write_timeout = 2
    ser.dtr = False
    ser.rts = False
    ser.open()
    download = Download()
    completed = False
    try:
        ser.write(("mic arm\n" if args.command == "capture" else
                   f"mic gain {args.gain}\n" if args.command == "gain" else
                   f"mic {args.command}\n").encode())
        deadline = time.monotonic() + (180 if args.command in ("capture", "dump") else 5)
        trigger_at = None
        requested_dump = args.command == "dump"
        pending = bytearray()
        with uart_path.open("w", encoding="utf-8") as log:
            while time.monotonic() < deadline:
                if trigger_at is not None and time.monotonic() >= trigger_at:
                    ser.write(b"mic trigger\n")
                    trigger_at = None
                pending.extend(ser.read(4096))
                while b"\n" in pending:
                    raw, _, pending = pending.partition(b"\n")
                    line = raw.decode("utf-8", errors="replace").rstrip()
                    log.write(line + "\n")
                    log.flush()
                    if "@MIC_DATA" not in line:
                        print(line, flush=True)
                    if "ARMED:" in line and args.trigger_after is not None:
                        trigger_at = time.monotonic() + args.trigger_after
                    if "capture invalid:" in line or "initialization failed;" in line or "capture allocation failed" in line:
                        raise RuntimeError("device rejected capture; see UART log")
                    if "@MIC_STATUS" in line:
                        state = fields(line).get("state")
                        if args.command in ("status", "cancel", "gain"):
                            completed = True
                            return
                        if state == "ready" and not requested_dump:
                            ser.write(b"mic dump\n")
                            requested_dump = True
                        elif args.command == "dump" and state != "ready":
                            raise RuntimeError("no frozen capture to download")
                    if download.feed(line):
                        wav_path = args.output.with_suffix(".wav")
                        if wav_path.exists():
                            raise FileExistsError(f"refusing to overwrite {wav_path}")
                        wav_path.write_bytes(download.data)
                        meta = dict(download.meta)
                        meta["trigger_alignment"] = "next 10ms microphone DMA boundary; acoustic delay not corrected"
                        args.output.with_suffix(".json").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")
                        print(f"Verified WAV: {wav_path.resolve()}", flush=True)
                        completed = True
                        return
            raise TimeoutError("no completed response within bounded capture/download window")
    finally:
        if args.command == "capture" and not completed:
            try:
                ser.write(b"mic cancel\n")
            except serial.SerialException:
                pass  # Keep the original failure; a wedged USB RX must not hang cleanup.
        ser.close()


if __name__ == "__main__":
    main()
