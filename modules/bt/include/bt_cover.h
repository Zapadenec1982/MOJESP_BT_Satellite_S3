#pragma once
#include <stddef.h>
#include <stdint.h>
namespace bt_cover {
bool init();
void clear();
void reset(const uint8_t* p, size_t len);
void data(const uint8_t* p, size_t len, uint32_t now, bool allow_stream=false);
void end(const uint8_t* p, size_t len);
void tick(uint32_t now);

/// Protocol 1.7. True once, with the pair whose transfer produced no usable image, when it
/// is worth asking the satellite to send that image again.
///
/// Taking it spends it, and spends one of the image's repeats, so ask only when the frame
/// will actually be sent — check the peer's capability first. A request nobody takes is
/// dropped by the next COVER_RESET and costs nothing until then.
///
/// The chain is bounded here rather than at the caller — COVER_RETRY_MAX repeats for one
/// image, reset when an image is published or when a RESET arrives that was not the answer
/// to our own asking — because this is the only place that can tell those two resets apart.
bool take_retry(uint32_t& nonce, uint32_t& generation);
}
