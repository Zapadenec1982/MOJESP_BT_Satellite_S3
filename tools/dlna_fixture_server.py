"""Small local MediaServer fixture for bounded DLNA integration checks (not a soak).

Run explicitly with --host <PC LAN IP>. Serves generated quiet WAV files, one PNG,
a 25-item ContentDirectory with a nested folder, and SSDP discovery responses.
Only generated fixtures are exposed; no filesystem directory is served.
"""
import argparse
import base64
import io
import json
import socket
import struct
import threading
import wave
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from xml.etree import ElementTree as ET
from xml.sax.saxutils import escape


def wav(seconds, rate=16000):
    buf = io.BytesIO()
    with wave.open(buf, "wb") as f:
        f.setnchannels(2)
        f.setsampwidth(2)
        f.setframerate(rate)
        f.writeframes(bytes(seconds * rate * 4))
    return buf.getvalue()


SHORT, LONG = wav(2), wav(20)
RATE44, RATE48 = wav(4, 44100), wav(4, 48000)
PNG = base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", type=int, default=18080)
    args = parser.parse_args()
    base = f"http://{args.host}:{args.port}"
    service = "urn:schemas-upnp-org:service:ContentDirectory:1"
    uid = "uuid:85a9bf4f-d857-4058-9bfa-46587831f345"
    events, requests = [], []
    records_lock = threading.Lock()
    description = f'''<?xml version="1.0"?><root xmlns="urn:schemas-upnp-org:device-1-0"><specVersion><major>1</major><minor>0</minor></specVersion><device><deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType><friendlyName>DLNA acceptance fixture</friendlyName><manufacturer>ModESP tests</manufacturer><modelName>Fixture</modelName><UDN>{uid}</UDN><serviceList><service><serviceType>{service}</serviceType><serviceId>urn:upnp-org:serviceId:ContentDirectory</serviceId><SCPDURL>/content.xml</SCPDURL><controlURL>/content/control</controlURL><eventSubURL>/content/event</eventSubURL></service></serviceList></device></root>'''.encode()

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def send(self, data, mime, status=200, content_range=None):
            self.send_response(status)
            self.send_header("Content-Type", mime)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Connection", "close")
            if content_range:
                self.send_header("Content-Range", content_range)
            self.end_headers()
            try:
                self.wfile.write(data)
            except (ConnectionError, BrokenPipeError):
                pass
            self.close_connection = True

        def do_GET(self):
            if self.path in ("/events", "/requests"):
                with records_lock:
                    data = json.dumps(events if self.path == "/events" else requests).encode()
                return self.send(data, "application/json")
            if self.path == "/description.xml":
                return self.send(description, "text/xml")
            if self.path == "/art.png":
                return self.send(PNG, "image/png")
            if self.path.startswith("/track/"):
                data = (RATE44 if self.path == "/track/rate44.wav" else
                        RATE48 if self.path == "/track/rate48.wav" else
                        LONG if "long" in self.path else SHORT)
                requested = self.headers.get("Range", "")
                with records_lock:
                    requests.append({"path": self.path, "range": requested})
                    del requests[:-64]
                if requested.startswith("bytes="):
                    try:
                        first, last = requested[6:].split("-", 1)
                        first = int(first)
                        last = min(int(last) if last else len(data) - 1, len(data) - 1)
                        if first < 0 or first > last:
                            raise ValueError
                    except ValueError:
                        return self.send(b"", "text/plain", 416)
                    return self.send(data[first:last + 1], "audio/wav", 206, f"bytes {first}-{last}/{len(data)}")
                return self.send(data, "audio/wav")
            return self.send(b"Not found", "text/plain", 404)

        def do_NOTIFY(self):
            length = int(self.headers.get("Content-Length", "0"))
            if self.path != "/events" or not 0 < length < 16384:
                return self.send(b"Bad request", "text/plain", 400)
            body = self.rfile.read(length).decode("utf-8", errors="strict")
            with records_lock:
                events.append({"sid": self.headers.get("SID"),
                               "seq": self.headers.get("SEQ"), "body": body})
                del events[:-32]
            return self.send(b"", "text/plain")

        def do_POST(self):
            length = int(self.headers.get("Content-Length", "0"))
            if self.path != "/content/control" or not 0 < length < 16384:
                return self.send(b"Bad request", "text/plain", 400)
            try:
                root = ET.fromstring(self.rfile.read(length))
                fields = {e.tag.split("}")[-1]: e.text or "" for e in root.iter()}
                object_id = fields["ObjectID"]
                offset = max(0, int(fields["StartingIndex"]))
                count = min(12, max(1, int(fields["RequestedCount"])))
            except (ET.ParseError, KeyError, ValueError):
                return self.send(b"Bad request", "text/plain", 400)
            entries = []
            if object_id == "0":
                entries.append('<container id="folder&amp;1" parentID="0" restricted="1"><dc:title>Nested &amp; folder</dc:title><upnp:class>object.container.storageFolder</upnp:class></container>')
            for i in range(25 if object_id == "0" else 3):
                title = f"Track {i + 1:02d} & fixture"
                resource = f"{base}/track/{'long' if i == 0 else i}.wav"
                entries.append(f'<item id="t{i}" parentID="{escape(object_id)}" restricted="1"><dc:title>{escape(title)}</dc:title><upnp:class>object.item.audioItem.musicTrack</upnp:class><upnp:artist>Fixture Artist</upnp:artist><upnp:album>DLNA checks</upnp:album><upnp:albumArtURI>{base}/art.png</upnp:albumArtURI><res protocolInfo="http-get:*:audio/wav:*">{resource}</res></item>')
            selected = entries[offset:offset + count]
            didl = '<DIDL-Lite xmlns="urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:upnp="urn:schemas-upnp-org:metadata-1-0/upnp/">' + "".join(selected) + "</DIDL-Lite>"
            response = f'<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body><u:BrowseResponse xmlns:u="{service}"><Result>{escape(didl)}</Result><NumberReturned>{len(selected)}</NumberReturned><TotalMatches>{len(entries)}</TotalMatches><UpdateID>1</UpdateID></u:BrowseResponse></s:Body></s:Envelope>'
            self.send(response.encode(), "text/xml; charset=utf-8")

    def discovery():
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("", 1900))
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
                        socket.inet_aton("239.255.255.250") + socket.inet_aton(args.host))
        while True:
            data, peer = sock.recvfrom(4096)
            if b"M-SEARCH" not in data or b"MediaServer" not in data:
                continue
            response = f"HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=120\r\nEXT:\r\nLOCATION: {base}/description.xml\r\nSERVER: ModESP/1.0 UPnP/1.0 Fixture/1.0\r\nST: urn:schemas-upnp-org:device:MediaServer:1\r\nUSN: {uid}::urn:schemas-upnp-org:device:MediaServer:1\r\n\r\n"
            sock.sendto(response.encode(), peer)

    threading.Thread(target=discovery, daemon=True).start()
    print(f"Fixture ready: {base}/description.xml", flush=True)
    ThreadingHTTPServer((args.host, args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
