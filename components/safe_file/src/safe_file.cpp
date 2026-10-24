/**
 * @file safe_file.cpp
 * @brief write -> read back -> validate -> rename. See safe_file.h for why each step exists.
 */

#include "safe_file.h"

#include <cstdio>
#include <cstring>

namespace safe_file {
namespace {

/// One LittleFS cache line here (CONFIG_LITTLEFS_CACHE_SIZE=512): a larger buffer buys
/// nothing and a copy must not need as much RAM as the file is big.
constexpr size_t CHUNK = 512;

/// "<path>.new" in a caller-free buffer. Returns false when the name would not fit, which
/// is the only reason a save can fail before touching the filesystem at all.
bool candidate_name(const char* path, char* out, size_t out_sz) {
    const size_t n = strlen(path) + strlen(SUFFIX);
    if (n + 1 > out_sz) return false;
    snprintf(out, out_sz, "%s%s", path, SUFFIX);
    return true;
}

/// FNV-1a. Not a security hash — a cheap way to say "the bytes that came back are the bytes
/// that went in" without holding the file twice in RAM.
inline uint32_t fnv1a(uint32_t h, const void* p, size_t n) {
    const unsigned char* b = static_cast<const unsigned char*>(p);
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
    return h;
}
constexpr uint32_t FNV_INIT = 2166136261u;

/// Read `path` back and check it is exactly `len` bytes hashing to `want`.
///
/// The reason this is not redundant with fwrite's return value: fwrite reports what it
/// accepted into the stdio buffer, not what LittleFS committed. A partition with no free
/// blocks left, or a block that fails to program, shows up here and nowhere earlier — and
/// the point of the whole sequence is that such a file never becomes the live one.
bool readback_matches(const char* path, size_t len, uint32_t want) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    char buf[CHUNK];
    size_t got = 0;
    uint32_t h = FNV_INIT;
    for (;;) {
        const size_t r = fread(buf, 1, sizeof(buf), f);
        if (r == 0) break;
        got += r;
        if (got > len) break;              // longer than expected: no need to read on
        h = fnv1a(h, buf, r);
    }
    const bool clean = (ferror(f) == 0);
    fclose(f);
    return clean && got == len && h == want;
}

/// Put `tmp` in `path`'s place.
///
/// LittleFS's rename replaces an existing target in a single metadata commit — that is the
/// atomic step the whole sequence rests on, and it is why the live file is NOT removed
/// first. The remove below is only for a filesystem whose rename refuses an existing
/// target; it opens a window with no live file, so it is the fallback and not the rule.
bool place(const char* tmp, const char* path) {
    if (rename(tmp, path) == 0) return true;
    remove(path);
    return rename(tmp, path) == 0;
}

} // namespace

bool exists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

void sweep(const char* path) {
    char tmp[192];
    if (candidate_name(path, tmp, sizeof(tmp))) remove(tmp);
}

bool save(const char* path, const void* data, size_t len, Validator validate, void* ctx) {
    char tmp[192];
    if (!candidate_name(path, tmp, sizeof(tmp))) return false;

    FILE* f = fopen(tmp, "wb");
    if (!f) return false;
    const size_t w = len ? fwrite(data, 1, len, f) : 0;
    // fflush before fclose so a write error is seen here rather than swallowed by fclose's
    // return value, which callers habitually ignore.
    const bool flushed = (fflush(f) == 0);
    fclose(f);
    if (w != len || !flushed) { remove(tmp); return false; }

    if (!readback_matches(tmp, len, fnv1a(FNV_INIT, data, len))) { remove(tmp); return false; }

    // Content that came from outside gets the caller's own reader run over it as well. For
    // content this firmware serialised itself the read-back above is the whole check —
    // re-parsing our own serialiser's output would only prove the serialiser, and that is
    // what the host tests are for.
    if (validate && !validate(tmp, ctx)) { remove(tmp); return false; }

    if (!place(tmp, path)) { remove(tmp); return false; }
    return true;
}

bool copy(const char* src, const char* dst, Validator validate, void* ctx) {
    FILE* in = fopen(src, "rb");
    if (!in) return false;

    char tmp[192];
    if (!candidate_name(dst, tmp, sizeof(tmp))) { fclose(in); return false; }

    FILE* out = fopen(tmp, "wb");
    if (!out) { fclose(in); return false; }

    char buf[CHUNK];
    size_t total = 0;
    uint32_t h = FNV_INIT;
    bool ok = true;
    for (;;) {
        const size_t r = fread(buf, 1, sizeof(buf), in);
        if (r == 0) { ok = (ferror(in) == 0); break; }
        total += r;
        if (total > MAX_COPY_BYTES) { ok = false; break; }
        if (fwrite(buf, 1, r, out) != r) { ok = false; break; }
        h = fnv1a(h, buf, r);
    }
    if (fflush(out) != 0) ok = false;
    fclose(out);
    fclose(in);

    if (!ok) { remove(tmp); return false; }
    if (!readback_matches(tmp, total, h)) { remove(tmp); return false; }
    if (validate && !validate(tmp, ctx)) { remove(tmp); return false; }
    if (!place(tmp, dst)) { remove(tmp); return false; }
    return true;
}

} // namespace safe_file
