#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace audio_core {

struct WavPcmData {
    uint64_t offset = 0;
    uint64_t bytes = 0;
    uint32_t rate_hz = 0;
    uint16_t frame_bytes = 0;
    bool valid() const { return frame_bytes && rate_hz && bytes >= frame_bytes; }
};

using AudioReadAt = size_t (*)(void*, uint64_t, void*, size_t);

/// Locate uncompressed PCM in RIFF/WAVE without reading its payload. A data chunk may
/// follow arbitrary padded JUNK/LIST chunks; offsets are checked against both RIFF and file.
inline bool read_wav_pcm_data(void* ctx, AudioReadAt read, uint64_t file_bytes, WavPcmData& out) {
    out = {};
    auto u16 = [](const uint8_t* p) { return uint16_t(p[0] | uint16_t(p[1]) << 8); };
    auto u32 = [](const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 |
                                            uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; };
    uint8_t hdr[40];
    if (!read || file_bytes < 12 || read(ctx, 0, hdr, 12) != 12 ||
        std::memcmp(hdr, "RIFF", 4) || std::memcmp(hdr + 8, "WAVE", 4)) return false;
    const uint64_t end = uint64_t(u32(hdr + 4)) + 8;
    if (end < 12 || end > file_bytes) return false;
    WavPcmData data;
    bool have_format = false, have_data = false;
    for (uint64_t pos = 12; pos <= end && end - pos >= 8;) {
        if (read(ctx, pos, hdr, 8) != 8) return false;
        const uint64_t size = u32(hdr + 4), body = pos + 8;
        if (size > end - body) return false;
        if (!std::memcmp(hdr, "fmt ", 4)) {
            const size_t n = size < sizeof(hdr) ? (size_t)size : sizeof(hdr);
            if (n < 16 || read(ctx, body, hdr, n) != n) return false;
            uint16_t format = u16(hdr);
            if (format == 0xfffe) {
                static const uint8_t guid_tail[14] = {0,0,0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
                if (n < 40 || u16(hdr + 16) < 22 || std::memcmp(hdr + 26, guid_tail, 14)) return false;
                format = u16(hdr + 24);
            }
            const uint16_t channels = u16(hdr + 2), bits = u16(hdr + 14);
            data.rate_hz = u32(hdr + 4);
            data.frame_bytes = u16(hdr + 12);
            if ((format != 1 && format != 3) || !channels || !data.rate_hz ||
                !(bits == 8 || bits == 16 || bits == 24 || bits == 32) ||
                (format == 3 && bits != 32) || uint32_t(channels) * (bits / 8) != data.frame_bytes)
                return false;
            have_format = true;
        } else if (!std::memcmp(hdr, "data", 4) && !have_data) {
            data.offset = body;
            data.bytes = size;
            have_data = true;
        }
        if (have_format && have_data) {
            data.bytes -= data.bytes % data.frame_bytes;
            if (!data.valid()) return false;
            out = data;
            return true;
        }
        const uint64_t padded = size + (size & 1);
        if (padded > end - body) return false;
        pos = body + padded;
    }
    return false;
}

/// Clamp to a complete frame inside data, never to a container header or trailing tags.
inline uint64_t pcm_seek_byte(const WavPcmData& data, int32_t seconds) {
    if (!data.valid()) return 0;
    const uint64_t frames = data.bytes / data.frame_bytes;
    uint64_t frame = seconds > 0 ? uint64_t(seconds) * data.rate_hz : 0;
    if (frame >= frames) frame = frames - 1;
    return data.offset + frame * data.frame_bytes;
}

} // namespace audio_core
