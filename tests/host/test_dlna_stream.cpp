#include "doctest.h"
#include "../../modules/radio/include/http_body.h"
#include "../../components/dlna_client/include/upnp_xml.h"
#include <string>
#include <algorithm>

struct Wire {
    std::string bytes;
    size_t at = 0, fragment = 4096;
    bool timeout = false, alternate = false;
    int operator()(char *out, size_t cap) {
        if (alternate) {
            timeout = !timeout;
            if (timeout)
                return 0;
        }
        if (at == bytes.size())
            return -1;
        size_t n = std::min({cap, fragment, bytes.size() - at});
        memcpy(out, bytes.data() + at, n);
        at += n;
        return (int)n;
    }
};
std::pair<std::string, int> drain(radio_core::HttpBody &body, Wire &wire) {
    std::string out;
    char buf[263];
    int r = 0;
    for (int i = 0; i < 10000; ++i) {
        r = body.read(buf, sizeof(buf), [&](char *b, size_t n) { return wire(b, n); });
        if (r < 0)
            break;
        if (r > 0)
            out.append(buf, r);
    }
    return {out, r};
}
TEST_CASE("finite HTTP ends at Content-Length without waiting for socket close") {
    radio_core::HttpBody body;
    body.reset(true, 526, false);
    Wire wire{std::string(600, 'x')};
    auto result = drain(body, wire);
    CHECK(result.first.size() == 526);
    CHECK(result.second == radio_core::HttpBody::END);
    CHECK(wire.at == 526);
}
TEST_CASE("premature FIN is an error, not a completed track") {
    radio_core::HttpBody body;
    body.reset(true, 20, false);
    Wire wire{"only ten!!"};
    auto result = drain(body, wire);
    CHECK(result.first == wire.bytes);
    CHECK(result.second == radio_core::HttpBody::ERROR);
}
TEST_CASE("chunked HTTP survives byte fragmentation and timeouts in every framing state") {
    radio_core::HttpBody body;
    body.reset(false, 0, true);
    Wire wire{"3;foo=bar\r\nabc\r\n4\r\ndefg\r\n0\r\nX-Final: yes\r\n\r\n", 0, 1, false, true};
    auto result = drain(body, wire);
    CHECK(result.first == "abcdefg");
    CHECK(result.second == radio_core::HttpBody::END);
}
TEST_CASE("invalid chunks cannot become successful EOF") {
    for (const char *bytes :
         {"z\r\n", "10000000000000000\r\n", "2\r\na", "1\r\naXX", "0\r\nX: unfinished"}) {
        CAPTURE(bytes);
        radio_core::HttpBody body;
        body.reset(false, 0, true);
        Wire wire{bytes};
        CHECK(drain(body, wire).second == radio_core::HttpBody::ERROR);
    }
}
TEST_CASE("unframed finite response ends only at FIN and reset clears completion") {
    radio_core::HttpBody body;
    body.reset(false, 0, false);
    Wire wire{"hello", 0, 1, false, true};
    CHECK(drain(body, wire).first == "hello");
    body.reset(true, 3, false);
    Wire next{"new"};
    CHECK(drain(body, next).first == "new");
}
TEST_CASE("UPnP device service scanner ignores fake elements in comments and CDATA") {
    const char *xml = "<root><!-- <service>fake</service> "
                      "--><x><![CDATA[<service>fake</service>]]></x><a:service "
                      "data='x>y'><a:serviceType>ContentDirectory</a:serviceType><controlURL>/"
                      "ctl?a=1&amp;b=2</controlURL></a:service></root>";
    size_t p = 0;
    dlna_core::Element e;
    REQUIRE(dlna_core::element(xml, "service", p, e));
    char value[128];
    REQUIRE(dlna_core::field(e.body, "controlURL", value, sizeof(value)));
    CHECK(std::string(value) == "/ctl?a=1&b=2");
    CHECK_FALSE(dlna_core::element(xml, "service", p, e));
}
TEST_CASE("DIDL attribute parser handles escaped IDs and rejects truncation") {
    char id[64];
    CHECK(dlna_core::attribute("id='a&amp;b' parentID=\"0\"", "id", id, sizeof(id)));
    CHECK(std::string(id) == "a&b");
    CHECK_FALSE(dlna_core::attribute("id='0123456789'", "id", id, 4));
    size_t p = 0;
    dlna_core::Element e;
    CHECK_FALSE(dlna_core::element("<item><x></item>", "item", p, e));
    p = 0;
    CHECK_FALSE(dlna_core::element("<!DOCTYPE x><item/>", "item", p, e));
}
TEST_CASE("UPnP relative URLs retain origin, port and escaped query") {
    char url[257];
    CHECK(dlna_core::resolve_url("http://nas:8200/device/description.xml", "../ctl", url,
                                 sizeof(url)));
    CHECK(std::string(url) == "http://nas:8200/ctl");
    CHECK(dlna_core::resolve_url("http://nas:8200/device.xml", "/art/1?a=2&b=3", url, sizeof(url)));
    CHECK(std::string(url) == "http://nas:8200/art/1?a=2&b=3");
    CHECK(dlna_core::resolve_url("http://nas", "song.mp3", url, sizeof(url)));
    CHECK(std::string(url) == "http://nas/song.mp3");
    CHECK_FALSE(dlna_core::resolve_url("http://nas", "http://evil/\r\nHeader:x", url, sizeof(url)));
}
TEST_CASE("ContentDirectory resource selection does not promise arbitrary media") {
    CHECK(dlna_core::playable("http-get:*:audio/mpeg:DLNA.ORG_PN=MP3"));
    CHECK(dlna_core::playable("http-get:*:audio/flac:*"));
    CHECK_FALSE(dlna_core::playable("http-get:*:video/mp4:*"));
    CHECK_FALSE(dlna_core::playable("http-get:*:audio/aac:*"));
    CHECK_FALSE(dlna_core::playable("http-get:*:audio/ogg:*"));
    CHECK_FALSE(dlna_core::playable("rtsp-rtp-udp:*:audio/mpeg:*"));
}
TEST_CASE("selected resource metadata is self contained and escaped once") {
    dlna_core::Track track;
    REQUIRE(track.set_uri("http://nas/music?id=1&part=2"));
    dlna_core::copy(track.title, "Title <live> & studio");
    dlna_core::copy(track.artist, "Artist");
    dlna_core::copy(track.art, "http://nas/art?a=1&b=2");
    REQUIRE(dlna_core::resource_metadata(track, "http-get:*:audio/mpeg:*"));
    const std::string metadata(track.metadata);
    CHECK(metadata.find("xmlns:dc=") != std::string::npos);
    CHECK(metadata.find("xmlns:upnp=") != std::string::npos);
    CHECK(metadata.find("Title &lt;live&gt; &amp; studio") != std::string::npos);
    char value[257];
    REQUIRE(dlna_core::field(metadata, "title", value, sizeof(value)));
    CHECK(std::string(value) == track.title);
    REQUIRE(dlna_core::field(metadata, "res", value, sizeof(value)));
    CHECK(std::string(value) == track.uri);
    REQUIRE(dlna_core::field(metadata, "albumArtURI", value, sizeof(value)));
    CHECK(std::string(value) == track.art);
}
