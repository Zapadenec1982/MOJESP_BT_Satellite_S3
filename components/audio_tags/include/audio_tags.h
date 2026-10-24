/**
 * @file audio_tags.h
 * @brief Track metadata out of the file itself: ID3v2, Vorbis comments, RIFF INFO.
 *
 * A file name is a poor title. It carries the track number, the ripper's punctuation and
 * whatever the file system allowed, and for anything downloaded it often carries the site
 * it came from as well. The tags inside the file are what the person who made it wrote.
 *
 * Reading is done through a Reader rather than a FILE*: the parser then has no I/O of its
 * own, which is what lets the whole of it run on the host against a buffer in memory. On
 * the device the same code reads the card.
 *
 * Artwork is LOCATED, not loaded. A cover is a few hundred kilobytes and the caller decides
 * when — and whether — to spend that; this reports where it sits and how big it is.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace audio_tags {

/// Random access to the file, however the caller has it open.
struct Reader {
    void*    ctx = nullptr;
    /// Reads up to `n` bytes at `off`; returns how many. Short reads mean end of file.
    size_t (*read)(void* ctx, uint64_t off, void* buf, size_t n) = nullptr;
    uint64_t size = 0;
};

enum class PicMime : uint8_t { NONE, JPEG, PNG, OTHER };

/// As wide as the file name a title would otherwise fall back to, and as the UI's field.
constexpr size_t TEXT_LEN = 96;

struct Tags {
    char     title[TEXT_LEN]  = "";
    char     artist[TEXT_LEN] = "";
    char     album[TEXT_LEN]  = "";
    uint16_t year     = 0;
    uint16_t track_no = 0;
    // Embedded artwork, as a position in this same file.
    uint64_t pic_off  = 0;
    uint32_t pic_len  = 0;
    PicMime  pic_mime = PicMime::NONE;

    bool has_text() const { return title[0] || artist[0] || album[0]; }
    bool has_pic()  const { return pic_len > 0 && pic_mime != PicMime::NONE; }
};

/// Fills `out` with whatever the file declares. Returns false when nothing was recognised;
/// `out` is left empty in that case and the caller falls back to the file name.
///
/// Everything it writes is UTF-8, whatever the tag was encoded in.
bool read(const Reader& r, Tags& out);

}  // namespace audio_tags
