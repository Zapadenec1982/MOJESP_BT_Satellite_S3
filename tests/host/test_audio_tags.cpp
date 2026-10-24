/**
 * @file test_audio_tags.cpp
 * @brief The tag parser, against tags built byte by byte.
 *
 * Every case here is a file assembled in memory, so the expected answer is not a guess
 * about what some encoder happened to write — it is the bytes this test put there. The
 * awkward cases are the ones real files are full of: UTF-16 with a BOM, a v2.2 tag with
 * three-character frame ids, a length that runs past the end of the block.
 */

#include "doctest.h"
#include "audio_tags.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;

void put(Bytes& b, const char* s) { while (*s) b.push_back((uint8_t)*s++); }
void put_be32(Bytes& b, uint32_t v) {
    b.push_back((uint8_t)(v >> 24)); b.push_back((uint8_t)(v >> 16));
    b.push_back((uint8_t)(v >> 8));  b.push_back((uint8_t)v);
}
void put_le32(Bytes& b, uint32_t v) {
    b.push_back((uint8_t)v);         b.push_back((uint8_t)(v >> 8));
    b.push_back((uint8_t)(v >> 16)); b.push_back((uint8_t)(v >> 24));
}
void put_syncsafe(Bytes& b, uint32_t v) {
    b.push_back((uint8_t)((v >> 21) & 0x7F)); b.push_back((uint8_t)((v >> 14) & 0x7F));
    b.push_back((uint8_t)((v >> 7)  & 0x7F)); b.push_back((uint8_t)(v & 0x7F));
}

size_t mem_read(void* ctx, uint64_t off, void* buf, size_t n) {
    const Bytes& b = *(const Bytes*)ctx;
    if (off >= b.size()) return 0;
    const size_t avail = b.size() - (size_t)off;
    const size_t take  = n < avail ? n : avail;
    memcpy(buf, b.data() + off, take);
    return take;
}

audio_tags::Reader reader_for(const Bytes& b) {
    audio_tags::Reader r;
    r.ctx  = (void*)&b;
    r.read = mem_read;
    r.size = b.size();
    return r;
}

/// An ID3v2.3 text frame: four-character id, size, two flag bytes, encoding, text.
void id3v23_text(Bytes& b, const char* id, uint8_t enc, const Bytes& text) {
    put(b, id);
    put_be32(b, (uint32_t)text.size() + 1);
    b.push_back(0); b.push_back(0);
    b.push_back(enc);
    b.insert(b.end(), text.begin(), text.end());
}

Bytes latin(const char* s) { Bytes t; put(t, s); return t; }

/// UTF-16LE with the byte order mark an encoder would write.
Bytes utf16le_bom(const std::u16string& s) {
    Bytes t;
    t.push_back(0xFF); t.push_back(0xFE);
    for (char16_t c : s) { t.push_back((uint8_t)(c & 0xFF)); t.push_back((uint8_t)(c >> 8)); }
    return t;
}

/// Wraps frames in an ID3v2 header of the given version.
Bytes id3_file(uint8_t ver, const Bytes& frames, size_t trailing = 64) {
    Bytes b;
    put(b, "ID3");
    b.push_back(ver); b.push_back(0); b.push_back(0);
    put_syncsafe(b, (uint32_t)frames.size());
    b.insert(b.end(), frames.begin(), frames.end());
    b.resize(b.size() + trailing, 0x55);      // where the audio would be
    return b;
}

}  // namespace

TEST_CASE("id3v2.3 latin-1 text is read") {
    Bytes f;
    id3v23_text(f, "TIT2", 0, latin("Slow Dance"));
    id3v23_text(f, "TPE1", 0, latin("Apelislin"));
    id3v23_text(f, "TALB", 0, latin("Nights"));
    id3v23_text(f, "TYER", 0, latin("2019"));
    id3v23_text(f, "TRCK", 0, latin("4/12"));
    const Bytes file = id3_file(3, f);

    audio_tags::Tags t;
    const auto r = reader_for(file);
    REQUIRE(audio_tags::read(r, t));
    CHECK(std::string(t.title)  == "Slow Dance");
    CHECK(std::string(t.artist) == "Apelislin");
    CHECK(std::string(t.album)  == "Nights");
    CHECK(t.year     == 2019);
    CHECK(t.track_no == 4);            // "4/12" — the number, not the pair
}

TEST_CASE("id3v2.3 latin-1 high bytes become utf-8") {
    // 0xC4 is "Ä" in ISO-8859-1, which is two bytes in UTF-8.
    Bytes text; text.push_back(0xC4); put(text, "ngstrm");
    Bytes f;
    id3v23_text(f, "TIT2", 0, text);
    const Bytes file = id3_file(3, f);

    audio_tags::Tags t;
    const auto r = reader_for(file);
    REQUIRE(audio_tags::read(r, t));
    CHECK(std::string(t.title) == "\xC3\x84" "ngstrm");
}

TEST_CASE("id3v2.3 utf-16 with a bom is read") {
    // Cyrillic, which is what makes this worth testing: two bytes a letter either way, and
    // the wrong byte order gives a string of plausible-looking nonsense rather than a crash.
    const std::u16string title = u"Ночь";      // "Ніч" in Russian spelling
    Bytes f;
    id3v23_text(f, "TIT2", 1, utf16le_bom(title));
    const Bytes file = id3_file(3, f);

    audio_tags::Tags t;
    const auto r = reader_for(file);
    REQUIRE(audio_tags::read(r, t));
    CHECK(std::string(t.title) == "\xD0\x9D\xD0\xBE\xD1\x87\xD1\x8C");
}

TEST_CASE("id3v2.4 sizes are synchsafe") {
    // The one difference that matters: a v2.4 frame size is seven bits per byte. Reading it
    // as a plain integer gives a frame far too long and the walk stops at the first frame.
    Bytes f;
    put(f, "TIT2");
    const Bytes text = latin("Second");
    put_syncsafe(f, (uint32_t)text.size() + 1);
    f.push_back(0); f.push_back(0);
    f.push_back(0);
    f.insert(f.end(), text.begin(), text.end());
    put(f, "TPE1");
    const Bytes artist = latin("Someone");
    put_syncsafe(f, (uint32_t)artist.size() + 1);
    f.push_back(0); f.push_back(0);
    f.push_back(0);
    f.insert(f.end(), artist.begin(), artist.end());

    audio_tags::Tags t;
    const Bytes file = id3_file(4, f);
    const auto r = reader_for(file);
    REQUIRE(audio_tags::read(r, t));
    CHECK(std::string(t.title)  == "Second");
    CHECK(std::string(t.artist) == "Someone");
}

TEST_CASE("id3v2.2 uses three-character frame ids") {
    Bytes f;
    put(f, "TT2");
    const Bytes text = latin("Old Tag");
    f.push_back(0); f.push_back(0); f.push_back((uint8_t)(text.size() + 1));
    f.push_back(0);
    f.insert(f.end(), text.begin(), text.end());

    audio_tags::Tags t;
    const Bytes file = id3_file(2, f);
    const auto r = reader_for(file);
    REQUIRE(audio_tags::read(r, t));
    CHECK(std::string(t.title) == "Old Tag");
}

TEST_CASE("apic is located, not copied") {
    Bytes apic;
    apic.push_back(0);                       // encoding: latin-1
    put(apic, "image/jpeg"); apic.push_back(0);
    apic.push_back(3);                       // front cover
    put(apic, "cover"); apic.push_back(0);   // description
    const size_t pic_in_frame = apic.size();
    for (int i = 0; i < 40; i++) apic.push_back((uint8_t)(0xA0 + i));

    Bytes f;
    put(f, "APIC");
    put_be32(f, (uint32_t)apic.size());
    f.push_back(0); f.push_back(0);
    const size_t frame_body = f.size();      // relative to the start of the frame area
    f.insert(f.end(), apic.begin(), apic.end());

    audio_tags::Tags t;
    const Bytes file = id3_file(3, f);
    const auto r = reader_for(file);
    REQUIRE(audio_tags::read(r, t));
    REQUIRE(t.has_pic());
    CHECK(t.pic_mime == audio_tags::PicMime::JPEG);
    CHECK(t.pic_len  == 40);
    CHECK(t.pic_off  == 10 + frame_body + pic_in_frame);
    // And the offset really points at the image: the parser's job is to be believed.
    CHECK(file[(size_t)t.pic_off] == 0xA0);
    CHECK(file[(size_t)t.pic_off + 39] == 0xA0 + 39);
}

TEST_CASE("a cover far past any buffer is still found") {
    // The case that made every embedded cover invisible. A real tag puts the text frames
    // first and a 300 KB picture after them, so a parser that pulls the tag into a fixed
    // buffer reads the text, fails to fit the APIC frame, and stops — reporting a track
    // with a title, an artist and no artwork, which looks like a file without a cover.
    constexpr size_t PIC = 300 * 1024;
    Bytes f;
    id3v23_text(f, "TIT2", 0, latin("Has A Cover"));
    id3v23_text(f, "TPE1", 0, latin("Someone"));

    Bytes apic;
    apic.push_back(0);
    put(apic, "image/jpeg"); apic.push_back(0);
    apic.push_back(3);
    apic.push_back(0);                       // empty description
    const size_t pic_in_frame = apic.size();
    apic.resize(apic.size() + PIC, 0xC3);

    put(f, "APIC");
    put_be32(f, (uint32_t)apic.size());
    f.push_back(0); f.push_back(0);
    const size_t frame_body = f.size();
    f.insert(f.end(), apic.begin(), apic.end());

    audio_tags::Tags t;
    const Bytes file = id3_file(3, f);
    const auto r = reader_for(file);
    REQUIRE(audio_tags::read(r, t));
    CHECK(std::string(t.title)  == "Has A Cover");
    CHECK(std::string(t.artist) == "Someone");
    REQUIRE(t.has_pic());
    CHECK(t.pic_mime == audio_tags::PicMime::JPEG);
    CHECK(t.pic_len  == PIC);
    CHECK(t.pic_off  == 10 + frame_body + pic_in_frame);
    CHECK(file[(size_t)t.pic_off] == 0xC3);
    CHECK(file[(size_t)t.pic_off + PIC - 1] == 0xC3);
}

TEST_CASE("a frame longer than the tag stops the walk instead of reading past it") {
    Bytes f;
    put(f, "TIT2");
    put_be32(f, 0x00FFFFFF);                 // a length no truncated file can honour
    f.push_back(0); f.push_back(0);
    f.push_back(0);
    put(f, "junk");

    audio_tags::Tags t;
    const Bytes file = id3_file(3, f);
    const auto r = reader_for(file);
    CHECK_FALSE(audio_tags::read(r, t));     // nothing recognised, and nothing read past
    CHECK(t.title[0] == '\0');
}

TEST_CASE("flac vorbis comments are read") {
    Bytes cm;
    put_le32(cm, 8); put(cm, "ref libF");    // vendor string
    const char* fields[] = {"TITLE=\xD0\x9D\xD1\x96\xD1\x87",   // "Ніч" in UTF-8
                            "artist=Lowercase Key",
                            "ALBUM=Album One",
                            "DATE=2021",
                            "TRACKNUMBER=7"};
    put_le32(cm, 5);
    for (const char* s : fields) {
        const uint32_t n = (uint32_t)strlen(s);
        put_le32(cm, n);
        put(cm, s);
    }

    Bytes b;
    put(b, "fLaC");
    b.push_back(4 | 0x80);                   // VORBIS_COMMENT, and the last block
    b.push_back((uint8_t)(cm.size() >> 16)); b.push_back((uint8_t)(cm.size() >> 8));
    b.push_back((uint8_t)cm.size());
    b.insert(b.end(), cm.begin(), cm.end());
    b.resize(b.size() + 32, 0x11);

    audio_tags::Tags t;
    const auto r = reader_for(b);
    REQUIRE(audio_tags::read(r, t));
    CHECK(std::string(t.title)  == "\xD0\x9D\xD1\x96\xD1\x87");
    CHECK(std::string(t.artist) == "Lowercase Key");   // the key is case-insensitive
    CHECK(std::string(t.album)  == "Album One");
    CHECK(t.year     == 2021);
    CHECK(t.track_no == 7);
}

TEST_CASE("flac picture block is located") {
    Bytes pic;
    put_be32(pic, 3);                        // front cover
    put_be32(pic, 10); put(pic, "image/jpeg");
    put_be32(pic, 4);  put(pic, "desc");
    put_be32(pic, 300); put_be32(pic, 300); put_be32(pic, 24); put_be32(pic, 0);
    put_be32(pic, 24);
    const size_t data_at = pic.size();
    for (int i = 0; i < 24; i++) pic.push_back((uint8_t)i);

    Bytes b;
    put(b, "fLaC");
    b.push_back(6 | 0x80);                   // PICTURE, last block
    b.push_back((uint8_t)(pic.size() >> 16)); b.push_back((uint8_t)(pic.size() >> 8));
    b.push_back((uint8_t)pic.size());
    const size_t block_at = b.size();
    b.insert(b.end(), pic.begin(), pic.end());

    audio_tags::Tags t;
    const auto r = reader_for(b);
    REQUIRE(audio_tags::read(r, t));
    REQUIRE(t.has_pic());
    CHECK(t.pic_mime == audio_tags::PicMime::JPEG);
    CHECK(t.pic_len  == 24);
    CHECK(t.pic_off  == block_at + data_at);
    CHECK(b[(size_t)t.pic_off] == 0);
    CHECK(b[(size_t)t.pic_off + 23] == 23);
}

TEST_CASE("riff info is read from a wav") {
    Bytes info;
    put(info, "INFO");
    put(info, "INAM"); put_le32(info, 6); put(info, "Title"); info.push_back(0);
    put(info, "IART"); put_le32(info, 4); put(info, "Art");  info.push_back(0);

    Bytes b;
    put(b, "RIFF"); put_le32(b, 0); put(b, "WAVE");
    put(b, "LIST"); put_le32(b, (uint32_t)info.size());
    b.insert(b.end(), info.begin(), info.end());
    put(b, "data"); put_le32(b, 4); put(b, "\1\2\3\4");
    // The declared RIFF size is the rest of the file, as a real writer would put it.
    const uint32_t riff = (uint32_t)(b.size() - 8);
    b[4] = (uint8_t)riff; b[5] = (uint8_t)(riff >> 8);
    b[6] = (uint8_t)(riff >> 16); b[7] = (uint8_t)(riff >> 24);

    audio_tags::Tags t;
    const auto r = reader_for(b);
    REQUIRE(audio_tags::read(r, t));
    CHECK(std::string(t.title)  == "Title");
    CHECK(std::string(t.artist) == "Art");
}

TEST_CASE("an untagged file reports nothing rather than guessing") {
    Bytes b;
    put(b, "RIFF"); put_le32(b, 36); put(b, "WAVE");
    put(b, "fmt "); put_le32(b, 16);
    b.resize(b.size() + 16, 0);
    put(b, "data"); put_le32(b, 4); put(b, "\1\2\3\4");

    audio_tags::Tags t;
    const auto r = reader_for(b);
    CHECK_FALSE(audio_tags::read(r, t));
    CHECK_FALSE(t.has_text());
    CHECK_FALSE(t.has_pic());
}

TEST_CASE("a file too short to hold a header is refused") {
    Bytes b = latin("ID3");
    audio_tags::Tags t;
    const auto r = reader_for(b);
    CHECK_FALSE(audio_tags::read(r, t));
}

TEST_CASE("a title longer than the field is cut on a character boundary") {
    // Sixty Cyrillic letters: 120 bytes of UTF-8 against a 96-byte field. Cut by length
    // alone it would end mid-character and draw as a replacement glyph forever.
    std::string long_title;
    for (int i = 0; i < 60; i++) long_title += "\xD0\x9D";
    Bytes text; put(text, long_title.c_str());

    Bytes f;
    id3v23_text(f, "TIT2", 3, text);         // encoding 3 = UTF-8
    const Bytes file = id3_file(3, f);

    audio_tags::Tags t;
    const auto r = reader_for(file);
    REQUIRE(audio_tags::read(r, t));
    const std::string got(t.title);
    CHECK(got.size() < audio_tags::TEXT_LEN);
    CHECK(got.size() % 2 == 0);              // whole two-byte characters only
    CHECK(got == long_title.substr(0, got.size()));
}
