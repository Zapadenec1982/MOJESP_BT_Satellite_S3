#pragma once
#include "cover_wire.h"
#include <cstring>

namespace sat {
// Caller owns PSRAM. No allocation or decoder work on the UART receive path.
class CoverAssembly {
public:
    uint32_t nonce=0, generation=0, total=0, used=0;
    bool failed=false, complete=false, streaming=false;
    uint32_t crc=0;
    void reset(uint32_t n=0, uint32_t g=0) {
        nonce=n; generation=g; total=used=crc=0; failed=complete=streaming=false;
    }
    bool append(const uint8_t* p, size_t len, uint8_t* dst, size_t capacity, bool allow_stream=false) {
        if (!p || len < 16 || !nonce || !generation || failed || complete) return false;
        // Late packets from another track must not poison the new assembly.
        if (cover_u32(p)!=nonce || cover_u32(p+4)!=generation) return false;
        const uint32_t size=cover_u32(p+8), off=cover_u32(p+12);
        const size_t count=len-16;
        if (!size && allow_stream) {
            if (!dst || (used && !streaming) || off!=used || !count ||
                count>COVER_PART_MAX || used>COVER_BYTES_MAX || count>COVER_BYTES_MAX-used ||
                used>capacity || count>capacity-used) { failed=true; return false; }
            streaming=true;
            std::memcpy(dst+used,p+16,count);
            crc=cover_crc32(crc,p+16,count); used+=(uint32_t)count;
            return true;
        }
        if (streaming || !dst || !size || size>COVER_BYTES_MAX || size>capacity || off!=used ||
            !count || count>COVER_PART_MAX || off>size || count>size-off ||
            (total && size!=total) || (count!=COVER_PART_MAX && off+count!=size)) {
            failed=true; return false;
        }
        total=size;
        std::memcpy(dst+used,p+16,count); used+=(uint32_t)count;
        complete=used==total;
        return true;
    }
    bool finish(const uint8_t* p, size_t len) {
        if (!p || len<8 || !nonce || !generation || failed || complete) return false;
        if (cover_u32(p)!=nonce || cover_u32(p+4)!=generation) return false;
        if (len!=16 || !streaming || used<3 || cover_u32(p+8)!=used || cover_u32(p+12)!=crc) {
            failed=true; return false;
        }
        total=used; complete=true; return true;
    }
};
}
