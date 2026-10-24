#include "doctest.h"
#include "../../modules/dlna/include/dlna_transport.h"
#include <string>

using namespace dlna_core;

TEST_CASE("Seek time rejects byte offsets, overflow and malformed values") {
    int32_t seconds = -1;
    REQUIRE(time_seconds("1:02:03", seconds)); CHECK(seconds == 3723);
    REQUIRE(time_seconds("00:00:00", seconds)); CHECK(seconds == 0);
    REQUIRE(time_seconds("0:00:12.500", seconds)); CHECK(seconds == 12);
    for (const char* bad : {"42", "-1:00:00", "1:60:00", "1:00:60", "1::00", "1:00:00junk", "99999999999:00:00", "596524:00:00"})
        CHECK_FALSE(time_seconds(bad, seconds));
}

TEST_CASE("SOAP text escapes embedded DIDL and never reports an overflowing response length") {
    char out[128];
    const std::string metadata = "<title>A &amp; B</title>";
    const int n = xml_reply(metadata, out, sizeof(out));
    CHECK(std::string(out) == "&lt;title&gt;A &amp;amp; B&lt;/title&gt;");
    CHECK(n == int(strlen(out)));
    char decoded[128];
    REQUIRE(decode_text(out, decoded, sizeof(decoded)));
    CHECK(std::string(decoded) == metadata);
    CHECK(xml_reply(metadata, out, 8) == 0); CHECK(out[0] == 0);
}

TEST_CASE("DIDL fields decode text, alternate prefixes, numeric UTF-8 and artwork queries") {
    Track t;
    REQUIRE(t.set_metadata("<DIDL-Lite><item><d:title>A &amp; B &#x1F3B5;</d:title>"
        "<u:artist role='AlbumArtist'>Artist</u:artist><u:album><![CDATA[A < B]]></u:album>"
        "<u:albumArtURI profileID='JPEG_TN'>http://nas/art?a=1&amp;b=2</u:albumArtURI></item></DIDL-Lite>"));
    CHECK(std::string(t.title) == "A & B \xF0\x9F\x8E\xB5");
    CHECK(std::string(t.artist) == "Artist");
    CHECK(std::string(t.album) == "A < B");
    CHECK(std::string(t.art) == "http://nas/art?a=1&b=2");
}
TEST_CASE("DIDL parser ignores comments and attributes and rejects invalid text") {
    char out[32];
    CHECK(field("<!-- <title>wrong</title> --><item hint='x > y'><title>right</title></item>", "title", out, sizeof(out)));
    CHECK(std::string(out) == "right");
    CHECK_FALSE(field("<titleSort>wrong</titleSort>", "title", out, sizeof(out)));
    CHECK_FALSE(field("<!DOCTYPE x><title>wrong</title>", "title", out, sizeof(out)));
    for (const char* text : {"&unknown;", "&#0;", "&#xD800;", "&#x110000;", "&#999999999999999999999;", "&#x;", "&amp"}) {
        CHECK_FALSE(decode_text(text, out, sizeof(out)));
        CHECK(out[0] == 0);
    }
    CHECK_FALSE(decode_text("0123456789", out, 4));
    CHECK(out[0] == 0);
}
TEST_CASE("New metadata clears fields missing on the next item") {
    Track t;
    REQUIRE(t.set_metadata("<title>A</title><artist>B</artist><album>C</album><albumArtURI>http://a</albumArtURI>"));
    REQUIRE(t.set_metadata("<title>D</title><creator>E</creator>"));
    CHECK(std::string(t.title) == "D"); CHECK(std::string(t.artist) == "E");
    CHECK(t.album[0] == 0); CHECK(t.art[0] == 0);
    REQUIRE(t.set_uri("http://next"));
    CHECK(t.title[0] == 0); CHECK(t.metadata[0] == 0);
}
TEST_CASE("Transport rejects oversized identities without truncating or changing old state") {
    Track t;
    REQUIRE(t.set_uri("http://original"));
    CHECK_FALSE(t.set_uri(std::string(URI_CAP, 'a')));
    CHECK(std::string(t.uri) == "http://original");
    REQUIRE(t.set_metadata("<title>A</title>"));
    CHECK_FALSE(t.set_metadata(std::string(META_CAP, 'a')));
    CHECK(std::string(t.title) == "A");
}
TEST_CASE("FIFO preserves Stop Play Pause order and captures each Play identity") {
    Transport m; Command out;
    REQUIRE(m.current.set_uri("http://first"));
    REQUIRE(m.push(Action::Stop)); REQUIRE(m.push(Action::Play)); REQUIRE(m.push(Action::Pause));
    REQUIRE(m.current.set_uri("http://second")); REQUIRE(m.push(Action::Play));
    REQUIRE(m.pop(out)); CHECK(out.action == Action::Stop);
    REQUIRE(m.pop(out)); CHECK(out.action == Action::Play); CHECK(std::string(out.track.uri) == "http://first");
    REQUIRE(m.pop(out)); CHECK(out.action == Action::Pause);
    REQUIRE(m.pop(out)); CHECK(out.action == Action::Play); CHECK(std::string(out.track.uri) == "http://second");
    CHECK_FALSE(m.pop(out));
}
TEST_CASE("Next consumes the slot and Previous restores one track of history") {
    Transport m; Command out;
    REQUIRE(m.current.set_uri("http://first")); REQUIRE(m.next.set_uri("http://second"));
    REQUIRE(m.next.set_metadata("<title>Second</title>"));
    REQUIRE(m.advance()); REQUIRE(m.pop(out));
    CHECK(std::string(out.track.uri) == "http://second"); CHECK(std::string(out.track.title) == "Second");
    CHECK(m.next.uri[0] == 0); CHECK_FALSE(m.advance());
    REQUIRE(m.back()); REQUIRE(m.pop(out)); CHECK(std::string(out.track.uri) == "http://first");
    CHECK(std::string(m.next.uri) == "http://second");
    REQUIRE(m.back()); REQUIRE(m.pop(out)); CHECK(out.action == Action::Seek); CHECK(out.seconds == 0);
}
TEST_CASE("Queue saturation leaves the next-track slot intact and never overwrites commands") {
    Transport m; Command out;
    REQUIRE(m.current.set_uri("http://first")); REQUIRE(m.next.set_uri("http://second"));
    for (size_t i = 0; i < Transport::CAPACITY; ++i) REQUIRE(m.push(Action::Seek, int32_t(i)));
    CHECK_FALSE(m.push(Action::Stop)); CHECK_FALSE(m.advance());
    CHECK(std::string(m.current.uri) == "http://first"); CHECK(std::string(m.next.uri) == "http://second");
    for (size_t i = 0; i < Transport::CAPACITY; ++i) { REQUIRE(m.pop(out)); CHECK(out.seconds == int32_t(i)); }
    REQUIRE(m.advance()); REQUIRE(m.pop(out)); CHECK(std::string(out.track.uri) == "http://second");
}

TEST_CASE("Next preserves stopped and paused modes and restarts even an identical URI") {
    Transport m; Command out;
    REQUIRE(m.current.set_uri("http://same")); REQUIRE(m.next.set_uri("http://same"));
    REQUIRE(m.advance(Mode::Stopped)); REQUIRE(m.pop(out));
    CHECK(out.action == Action::Select); CHECK(out.restart);
    REQUIRE(m.back(Mode::Paused)); REQUIRE(m.pop(out));
    CHECK(out.action == Action::Select); CHECK(out.mode == Mode::Paused); CHECK(out.restart);
}

TEST_CASE("prepared renderer transition adopts the engine without replaying its URL") {
    dlna_core::Transport model;
    REQUIRE(model.current.set_uri("http://nas/first.wav"));
    REQUIRE(model.next.set_uri("http://nas/second.wav"));
    REQUIRE(model.advance(dlna_core::Mode::Playing, true));
    dlna_core::Command command;
    REQUIRE(model.pop(command));
    CHECK(command.action == dlna_core::Action::Adopt);
    CHECK(std::string(command.track.uri) == "http://nas/second.wav");
    CHECK(std::string(model.previous.uri) == "http://nas/first.wav");
    CHECK_FALSE(model.next.uri[0]);
    CHECK_FALSE(model.pop(command));
}

TEST_CASE("commands retain stream ownership during a prepared handoff") {
    CHECK(pending_handoff("old", "new", "old", "new", true));
    CHECK_FALSE(pending_handoff("old", "new", "old", "new", false));
    CHECK_FALSE(pending_handoff("old", "station", "old", "new", true));
    CHECK_FALSE(pending_handoff("other", "new", "old", "new", true));
    CHECK_FALSE(pending_handoff("old", "", "old", "", true));
    CHECK(pending_handoff("same", "same", "same", "same", true));
}
