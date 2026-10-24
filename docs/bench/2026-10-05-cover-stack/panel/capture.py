"""Log the panel's console without resetting it.

usage: capture.py <port> <logfile> <max seconds>
DTR/RTS are set low on the closed port object before open (DTR is the P4's reset line);
the port is reopened if it disappears (re-enumeration after a flash). Create <logfile>.stop
to end early.
"""
import datetime
import os
import sys
import time

import serial

port, path, max_s = sys.argv[1], sys.argv[2], float(sys.argv[3])
stop = path + ".stop"
t_end = time.time() + max_s


def now():
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


with open(path, "a", encoding="utf-8", errors="replace") as out:
    while time.time() < t_end and not os.path.exists(stop):
        try:
            s = serial.Serial(port=None, baudrate=115200, timeout=0.2)
            s.dtr = False
            s.rts = False
            s.port = port
            s.open()
        except Exception:
            time.sleep(0.2)
            continue
        out.write(f"# {now()} opened {port}\n")
        out.flush()
        buf = b""
        try:
            while time.time() < t_end and not os.path.exists(stop):
                data = s.read(4096)
                if not data:
                    continue
                buf += data
                *lines, buf = buf.split(b"\n")
                ts = now()
                for line in lines:
                    out.write(f"{ts} {line.decode('utf-8', 'replace').rstrip()}\n")
                out.flush()
        except Exception as e:
            out.write(f"# {now()} port lost: {e}\n")
            out.flush()
        try:
            s.close()
        except Exception:
            pass
        time.sleep(0.3)
    out.write(f"# {now()} capture ended\n")
