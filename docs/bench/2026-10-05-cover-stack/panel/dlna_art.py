"""Hand the panel's DLNA renderer a cover URL without playing anything.

usage: dlna_art.py info
       dlna_art.py art <image url> [title]

SetAVTransportURI while the transport is not PLAYING makes the renderer *select* the track
(no playback) and pass its albumArtURI to dlna_client::artwork(): the dlna_client worker then
fetches the image and calls cover_set_jpeg(CoverSource::Network). The track URI points at a
closed port on this PC, so even a PLAYING transport could not start sound - but `art` refuses
to send then anyway.
"""
import html
import re
import sys
import urllib.request

PANEL = "192.168.1.216"
NS = "urn:schemas-upnp-org:service:AVTransport:1"
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def soap(action, args=""):
    body = ('<?xml version="1.0" encoding="utf-8"?>'
            '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" '
            's:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/"><s:Body>'
            f'<u:{action} xmlns:u="{NS}"><InstanceID>0</InstanceID>{args}</u:{action}>'
            '</s:Body></s:Envelope>')
    req = urllib.request.Request(f"http://{PANEL}/AVTransport/Control", data=body.encode(),
                                 headers={"Content-Type": 'text/xml; charset="utf-8"',
                                          "SOAPACTION": f'"{NS}#{action}"'})
    with opener.open(req, timeout=10) as r:
        return r.status, r.read().decode("utf-8", "replace")


def state():
    _, text = soap("GetTransportInfo")
    m = re.search(r"<CurrentTransportState>([^<]*)<", text)
    return m.group(1) if m else "?"


if sys.argv[1] == "info":
    print(state())
elif sys.argv[1] == "art":
    url = sys.argv[2]
    title = sys.argv[3] if len(sys.argv) > 3 else "cover-stack test"
    st = state()
    if st == "PLAYING":
        sys.exit("transport is PLAYING - not sending")
    track = "http://192.168.1.21:9/cover-stack-test.mp3"
    didl = ('<DIDL-Lite xmlns="urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/" '
            'xmlns:dc="http://purl.org/dc/elements/1.1/" '
            'xmlns:upnp="urn:schemas-upnp-org:metadata-1-0/upnp/">'
            '<item id="1" parentID="0" restricted="1">'
            f'<dc:title>{html.escape(title)}</dc:title><upnp:artist>bench</upnp:artist>'
            f'<upnp:album>cover stack</upnp:album><upnp:albumArtURI>{html.escape(url)}</upnp:albumArtURI>'
            '<upnp:class>object.item.audioItem.musicTrack</upnp:class>'
            f'<res protocolInfo="http-get:*:audio/mpeg:*">{track}</res></item></DIDL-Lite>')
    args = f"<CurrentURI>{track}</CurrentURI><CurrentURIMetaData>{html.escape(didl)}</CurrentURIMetaData>"
    status, text = soap("SetAVTransportURI", args)
    print(st, "->", status, "fault" if "Fault" in text else "ok", "->", state())
