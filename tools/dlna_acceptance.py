"""Bounded checks against a running panel and dlna_fixture_server (never a soak).

This plays generated silent fixtures and changes only volatile DLNA/audio requests.
Requires explicit --panel and --fixture URLs. Does not flash, reset or change volume.
"""
import argparse
import atexit
import json
import time
import urllib.error
import urllib.request
from urllib.parse import urljoin
from xml.etree import ElementTree as ET
from xml.sax.saxutils import escape


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--panel", required=True)
    parser.add_argument("--fixture", required=True)
    parser.add_argument("--renderer-only", action="store_true", help="Skip authenticated local browser commands")
    parser.add_argument("--rate-switches", action="store_true", help="Also exercise 44.1/48 kHz Next and EOF transitions")
    args = parser.parse_args()
    panel, fixture = args.panel.rstrip("/"), args.fixture.rstrip("/")
    # LAN device requests must not go through the workstation's HTTP proxy.
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def request(path, data=None, headers=None, method=None):
        req = urllib.request.Request(urljoin(panel + "/", path), data=data,
                                     headers=headers or {}, method=method)
        try:
            with opener.open(req, timeout=8) as response:
                return response.status, response.read(65536)
        except urllib.error.HTTPError as response:
            return response.code, response.read(65536)

    def get(path):
        status, body = request(path)
        assert status == 200, (status, body)
        return json.loads(body)

    def wait(read, predicate, timeout=15):
        until = time.monotonic() + timeout
        last = None
        while time.monotonic() < until:
            last = read()
            if predicate(last):
                return last
            time.sleep(0.2)
        raise AssertionError(f"Timed out, last response: {last}")

    status, body = request("/rootDesc.xml")
    assert status == 200
    root = ET.fromstring(body)
    control = event_url = None
    for service in root.iter():
        if service.tag.split("}")[-1] != "service":
            continue
        fields = {e.tag.split("}")[-1]: e.text for e in service}
        if "AVTransport" in fields.get("serviceType", ""):
            control = fields["controlURL"]
            event_url = fields["eventSubURL"]
    assert control, "No AVTransport service advertised"

    def soap(action, fields=None, error=None, instance=0):
        fields = {"InstanceID": str(instance), **(fields or {})}
        xml = '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body>'
        xml += f'<u:{action} xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">'
        xml += "".join(f"<{k}>{escape(str(v))}</{k}>" for k, v in fields.items())
        xml += f"</u:{action}></s:Body></s:Envelope>"
        status, body = request(control, xml.encode(), {
            "Content-Type": "text/xml; charset=utf-8",
            "SOAPAction": f'"urn:schemas-upnp-org:service:AVTransport:1#{action}"',
        })
        parsed = ET.fromstring(body)
        result = {e.tag.split("}")[-1]: e.text or "" for e in parsed.iter()}
        if error:
            assert status == 500 and result.get("errorCode") == str(error), (status, result)
        else:
            assert status == 200, (status, result)
        return result

    subscription = None

    def cleanup():
        try:
            soap("Stop")
        except Exception:
            pass
        if subscription:
            try:
                request(event_url, headers={"SID": subscription}, method="UNSUBSCRIBE")
            except Exception:
                pass

    atexit.register(cleanup)

    def browser():
        return get("/api/dlna/browser")

    def command(code, row=0, view=None):
        view = view or browser()
        packed = (view["revision"] << 12) | (row << 4) | code
        status, body = request("/api/settings", json.dumps({"dlna.browse_cmd": packed}).encode(),
                               {"Content-Type": "application/json"})
        assert status == 200, (status, body)
        return view

    def browse_command(code, row=0):
        old = command(code, row)
        return wait(browser, lambda v: v["revision"] != old["revision"] and not v["busy"])

    soap("GetTransportInfo", instance=1, error=718)
    soap("Play", {"Speed": "2"}, error=717)
    soap("Seek", {"Unit": "TRACK_NR", "Target": "1"}, error=710)
    print("PASS: SOAP faults", flush=True)

    subscribe = urllib.request.Request(urljoin(panel + "/", event_url), method="SUBSCRIBE",
        headers={"CALLBACK": "<" + fixture + "/events>", "NT": "upnp:event", "TIMEOUT": "Second-180"})
    with opener.open(subscribe, timeout=8) as response:
        assert response.status == 200
        subscription = response.headers.get("SID")
        assert subscription

    if not args.renderer_only:
        found = browse_command(1)
        server = next(i for i, row in enumerate(found["rows"]) if row["title"] == "DLNA acceptance fixture")
        listing = browse_command(2, server)
        assert not listing["servers"] and listing["count"] == 12 and listing["next"], listing
        folder = browse_command(2, 0)
        assert folder["count"] == 3 and "folder" in folder["title"], folder
        listing = browse_command(3)
        assert listing["count"] == 12
        second = browse_command(4)
        assert second["previous"] and second["count"] == 12
        listing = browse_command(5)
        assert not listing["previous"]
        print("PASS: discovery, nested Browse, pagination and Back", flush=True)

        # A stale tap must never select whatever replaced the old row at that index.
        before = soap("GetMediaInfo")["CurrentURI"]
        command(2, 2, second)
        time.sleep(0.3)
        assert soap("GetMediaInfo")["CurrentURI"] == before
        print("PASS: stale row rejection", flush=True)

        command(2, 2, listing)  # Track 02, two-second silent WAV.
        wait(lambda: soap("GetTransportInfo"), lambda v: v.get("CurrentTransportState") == "PLAYING")
        command(6, 3)  # Track 03 is handed to renderer as its next URI.
        wait(lambda: soap("GetMediaInfo"), lambda v: v.get("NextURI") == fixture + "/track/2.wav", 3)
        wait(lambda: soap("GetMediaInfo"), lambda v: v.get("CurrentURI") == fixture + "/track/2.wav", 10)
        wait(lambda: soap("GetTransportInfo"), lambda v: v.get("CurrentTransportState") == "STOPPED", 10)
        print("PASS: local play, next URI, automatic advancement and natural EOF", flush=True)

    else:
        soap("SetAVTransportURI", {"CurrentURI": fixture + "/track/long.wav", "CurrentURIMetaData": ""})
        soap("SetNextAVTransportURI", {"NextURI": fixture + "/track/2.wav", "NextURIMetaData": ""})
        assert soap("GetMediaInfo")["NextURI"] == fixture + "/track/2.wav"
        soap("Play", {"Speed": "1"})
        wait(lambda: soap("GetTransportInfo"), lambda v: v.get("CurrentTransportState") == "PLAYING")
        soap("Next")
        wait(lambda: soap("GetMediaInfo"), lambda v: v.get("CurrentURI") == fixture + "/track/2.wav")
        wait(lambda: soap("GetTransportInfo"), lambda v: v.get("CurrentTransportState") == "STOPPED", 12)
        soap("Previous")
        wait(lambda: soap("GetMediaInfo"), lambda v: v.get("CurrentURI") == fixture + "/track/long.wav")
        soap("SetAVTransportURI", {"CurrentURI": fixture + "/track/1.wav", "CurrentURIMetaData": ""})
        soap("SetNextAVTransportURI", {"NextURI": fixture + "/track/2.wav", "NextURIMetaData": ""})
        soap("Play", {"Speed": "1"})
        wait(lambda: soap("GetMediaInfo"), lambda v: v.get("CurrentURI") == fixture + "/track/2.wav", 15)
        wait(lambda: soap("GetTransportInfo"), lambda v: v.get("CurrentTransportState") == "STOPPED", 12)
        print("PASS: SetNext, Next, Previous, automatic advancement and natural EOF", flush=True)

    metadata = '<DIDL-Lite xmlns:dc="http://purl.org/dc/elements/1.1/"><item><dc:title>Seek &amp; pause fixture</dc:title></item></DIDL-Lite>'
    soap("SetAVTransportURI", {"CurrentURI": fixture + "/track/long.wav", "CurrentURIMetaData": metadata})
    soap("Play", {"Speed": "1"})
    wait(lambda: soap("GetTransportInfo"), lambda v: v.get("CurrentTransportState") == "PLAYING")
    soap("Pause")
    wait(lambda: soap("GetTransportInfo"), lambda v: v.get("CurrentTransportState") == "PAUSED_PLAYBACK")
    soap("Seek", {"Unit": "REL_TIME", "Target": "0:00:05"})
    time.sleep(0.7)
    soap("Play", {"Speed": "1"})
    position = wait(lambda: soap("GetPositionInfo"), lambda v: int(v.get("RelTime", "0:00:00").split(":")[-1]) >= 5)
    assert "Seek &amp; pause fixture" in position["TrackMetaData"]
    soap("Stop")
    wait(lambda: soap("GetTransportInfo"), lambda v: v.get("CurrentTransportState") == "STOPPED")
    print("PASS: metadata, Pause, ranged Seek, Resume and Stop", flush=True)
    records = get(fixture + "/requests")
    assert any(r["range"].startswith("bytes=") and r["range"] != "bytes=0-" for r in records), records
    events = get(fixture + "/events")
    own_events = [e for e in events if e["sid"] == subscription]
    assert own_events, "No GENA callback received"
    for event in own_events:
        envelope = ET.fromstring(event["body"])
        for node in envelope.iter():
            if node.tag.split("}")[-1] == "LastChange" and node.text:
                ET.fromstring(node.text)
    print("PASS: actual ranged GET and parseable GENA events", flush=True)
    if args.rate_switches:
        for first, second in (("rate44", "rate48"), ("rate48", "rate44")):
            current, following = fixture + f"/track/{first}.wav", fixture + f"/track/{second}.wav"
            soap("SetAVTransportURI", {"CurrentURI": current, "CurrentURIMetaData": ""})
            soap("Play", {"Speed": "1"})
            wait(lambda: soap("GetPositionInfo"), lambda v: v.get("TrackURI") == current and int(v.get("RelTime", "0:00:00").split(":")[-1]) >= 1)
            soap("SetNextAVTransportURI", {"NextURI": following, "NextURIMetaData": ""})
            soap("Next")
            wait(lambda: soap("GetPositionInfo"), lambda v: v.get("TrackURI") == following and int(v.get("RelTime", "0:00:00").split(":")[-1]) >= 1)
            soap("Stop")
            wait(lambda: soap("GetTransportInfo"), lambda v: v.get("CurrentTransportState") == "STOPPED")
        soap("SetAVTransportURI", {"CurrentURI": fixture + "/track/rate44.wav", "CurrentURIMetaData": ""})
        soap("SetNextAVTransportURI", {"NextURI": fixture + "/track/rate48.wav", "NextURIMetaData": ""})
        soap("Play", {"Speed": "1"})
        wait(lambda: soap("GetPositionInfo"), lambda v: v.get("TrackURI") == fixture + "/track/rate48.wav" and int(v.get("RelTime", "0:00:00").split(":")[-1]) >= 1)
        wait(lambda: soap("GetTransportInfo"), lambda v: v.get("CurrentTransportState") == "STOPPED")
        print("PASS: manual Next in both rate directions and natural 44.1 -> 48 kHz", flush=True)
    print("Bounded DLNA acceptance complete; no soak run.", flush=True)


if __name__ == "__main__":
    main()
