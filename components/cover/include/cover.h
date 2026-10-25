/**
 * @file cover.h
 * @brief Album artwork, decoded once and kept as a small RGB565 tile.
 *
 * A cover arrives as a JPEG of a few hundred kilobytes and a thousand pixels a side, and
 * it is shown in a card of a hundred and fifty. Decoding it every time the screen redraws
 * would be absurd, and handing LVGL the full-size image would make it rescale a megapixel
 * on every frame the spectrum touches. So the work is done ONCE, when the track opens:
 * the P4's JPEG engine decodes it and the PPA scales it, both in hardware, and what the UI
 * gets is a plain array of pixels exactly the size of the card.
 *
 * Two slots, alternating. The UI may be drawing from one when the next track's artwork
 * arrives, and a buffer rewritten underneath a draw is a torn image; the producer always
 * fills the one that is not current.
 *
 * A board without a display keeps the picture as it ARRIVED instead (CONFIG_COVER_DECODE_TILE=n,
 * the ESP32-S3): nothing on it draws pixels, the browser decodes a JPEG or a PNG by itself, and
 * a chip with neither a JPEG engine nor a PPA would spend seconds and megabytes in the software
 * decoder for a tile nobody looks at. What such a build publishes is the file's bytes
 * (cover_bytes_get) and the picture's colour, worked out from an eighth-scale decode by the
 * ROM's TJpgDec; CoverView::px stays null and CoverView::bytes says a picture is there.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace player_core {

/// The side of the tile, in pixels — the cover card on the Now Playing screen.
constexpr int COVER_PX = 280;
/// Radio: what a station is playing, found by its ICY title (modules/radio/src/art.cpp).
/// Network stays DLNA's, which arrives with its own artwork.
enum class CoverSource { File, Bluetooth, Network, AirPlay, Radio };
constexpr int COVER_SOURCES = (int)CoverSource::Radio + 1;

struct CoverView {
    const uint16_t* px  = nullptr;   ///< RGB565, w*h, or null when there is no artwork
    int             w   = 0;
    int             h   = 0;
    uint32_t        gen = 0;         ///< bumps on every change, including to "none"
    uint32_t        rgb = 0;         ///< the picture's colour, RGB888 (cover_tone); 0: none
    /// A build that keeps the picture as its file (CONFIG_COVER_DECODE_TILE=n): how many bytes
    /// cover_bytes_get() would hand over. 0 when there is no artwork, and always 0 in a build
    /// that decodes tiles — there `px` is the picture.
    size_t          bytes = 0;
};

/// Whether a view holds a picture, in either kind of build: a tile, or the file's bytes.
inline bool cover_present(const CoverView& v) { return v.px != nullptr || v.bytes != 0; }

/// The picture of one source as the file that arrived, JPEG or PNG, for a client that decodes it
/// itself (GET /api/cover on a board without a display). The bytes stay valid until
/// cover_bytes_put(), whatever the producers publish meanwhile: a reference is held, not a copy
/// taken, because the reader sends them to a socket and a socket can take its time.
struct CoverBytes {
    const uint8_t* data = nullptr;
    size_t         len  = 0;
    bool           png  = false;     ///< PNG; a JPEG otherwise
    uint32_t       gen  = 0;         ///< the source's generation these bytes were published at
    void*          ref  = nullptr;   ///< what cover_bytes_put() releases
};

/// The current picture of `source` as bytes. False when it has none — and always in a build that
/// decodes tiles, which does not keep the file. Every true needs one cover_bytes_put().
bool cover_bytes_get(CoverSource source, CoverBytes& out);
void cover_bytes_put(CoverBytes& b);

/// The colour of a tile (RGB565, w×h), for the Player's background:
/// where the picture's colour is, black bars and black grounds ignored, a vivid colour
/// before a grey one. RGB888; 0 when there is none — an all-black picture, or no picture.
/// Pure (cover_tone.cpp), run once when a picture is published.
uint32_t cover_tone(const uint16_t* px, int w, int h);

/// The Player's background from a picture's colour (cover_tone): the same hue at the lightness
/// every palette's light text reads on, and never more vivid than a muted shade. The glass and
/// the web draw it fading into the surface (ui.cover_rgb). RGB888; 0 for 0 — no picture, no tint.
uint32_t cover_backdrop(uint32_t rgb);

/// Decodes `len` bytes of JPEG (or PNG via the software fallback) into the free slot — or, in a
/// build that keeps the file (CONFIG_COVER_DECODE_TILE=n), checks that they are a JPEG or a PNG
/// of a sane size, copies them and works out their colour.
///
/// Sources have independent tiles. Decoder work is serialized across producers.
/// A supplied generation cancels publication if clear() ran during decoding.
/// Returns false for an invalid image or a cancelled publication. Progressive JPEG
/// uses the bounded software fallback.
///
/// The software fallback (PNG, progressive JPEG) runs on this component's own task while the
/// caller waits, because it needs 8.5 KB of stack; the caller's stack only has to hold the
/// hardware path. See SOFT_STACK in cover.cpp.
bool cover_set_jpeg(const uint8_t* jpg, size_t len, CoverSource source = CoverSource::File,
                    uint32_t expected_generation = UINT32_MAX);

/// This track has no artwork. Publishes "none", so the card empties instead of keeping the
/// previous album's picture next to a different track.
void cover_clear(CoverSource source = CoverSource::File);

/// What the UI should draw. Safe to call from the LVGL task.
CoverView cover_view(CoverSource source = CoverSource::File);

/// The tile the Player shows while audio.source is `source` (3 Bluetooth, 4 the card, 5 the
/// radio's stream, 6 AirPlay); `push` when that stream is one a DLNA control point pushed
/// (radio.mode "push"). False for a source that has no artwork at all — off, the tone, AUX —
/// and `tile` is then File, which is what the Player compared against before.
///
/// One rule for the glass and the web: screen_now.cpp draws this tile and GET /api/cover sends
/// it, so the browser never shows a picture the panel does not.
inline bool cover_tile_for(int32_t source, bool push, CoverSource& tile) {
    switch (source) {
    case 3: tile = CoverSource::Bluetooth; return true;
    case 4: tile = CoverSource::File; return true;
    case 5: tile = push ? CoverSource::Network : CoverSource::Radio; return true;
    case 6: tile = CoverSource::AirPlay; return true;
    default: tile = CoverSource::File; return false;
    }
}

/// cover_tile_for() and the view of that tile, with the Player's one fallback: a stream pushed
/// over DLNA shows the art its control point sent, and the title's cover (Radio) when it sent
/// none — a station tuned from the catalog arrives that way. An empty view, gen 0, for a source
/// without artwork. `tile` says where the picture came from: generations are per tile.
CoverView cover_now(int32_t source, bool push, CoverSource& tile);

/// The Player's picture as one number, for the web (ui.cover): 0 for none, otherwise different
/// for every new picture and for every tile it may come from. Never negative.
inline int32_t cover_id(const CoverView& v, CoverSource tile) {
    if (!cover_present(v)) return 0;
    return (int32_t)((((v.gen & 0x07FFFFFFu) << 3) | ((uint32_t)tile & 7u)) + 1u);
}

}  // namespace player_core
