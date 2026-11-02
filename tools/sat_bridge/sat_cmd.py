"""sat_cmd.py PORT COMMAND: one command to tools/sat_bridge, and what the satellite answers.

    python sat_cmd.py /dev/ttyACM0 '#SAT-DL#'    the satellite into its ROM loader
    python sat_cmd.py /dev/ttyACM0 '#SAT-RUN#'   the satellite into its application

Opening the port may reset the S3 through the COM connector's auto-reset lines; the bridge comes up
in under two seconds, and this waits that long before it sends. Needs pyserial (ESP-IDF's Python
has it).
"""
import sys
import time

import serial


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ("#SAT-DL#", "#SAT-RUN#"):
        sys.exit(__doc__)
    port, cmd = sys.argv[1], sys.argv[2].encode()
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.2
    s.dtr = s.rts = False
    s.open()
    until = time.time() + 2.0
    while time.time() < until:
        s.read(4096)
    s.write(cmd)
    s.flush()
    got = bytearray()
    until = time.time() + 3.0
    while time.time() < until:
        got += s.read(4096)
    s.close()
    text = "".join(c if 32 <= ord(c) < 127 or c in "\r\n" else "." for c in got.decode("latin-1"))
    print(text.strip() or "(nothing came back: is the bridge flashed, and the board on this port?)")


if __name__ == "__main__":
    main()
