#include "sat_link/sat_link.h"

#include <string.h>

namespace sat {

// ── CRC-16/CCITT-FALSE ─────────────────────────────────────────────────────────────────────
// Bit-by-bit rather than a 512-byte table: the link carries a few hundred bytes a second,
// and flash on this board is worth more than the microseconds a table would save.

uint16_t crc16(const uint8_t* data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

// ── COBS ───────────────────────────────────────────────────────────────────────────────────

size_t cobs_encode(const uint8_t* src, size_t len, uint8_t* dst)
{
    size_t  r = 0, w = 1, code_at = 0;
    uint8_t code = 1;
    bool    open = true;   // is a group still waiting for its code byte?

    while (r < len) {
        if (src[r] == 0) {
            dst[code_at] = code;
            code_at      = w++;
            code         = 1;
            r++;
        } else {
            dst[w++] = src[r++];
            if (++code == 0xFF) {
                dst[code_at] = code;
                // A full group that closes exactly at the end of the input opens nothing
                // after it. Reserving a slot here anyway appended a spurious 0x01 at every
                // multiple of 254 bytes — decodable by a strict decoder, one byte longer
                // than the ceil(n/254) overhead the contract states, and mis-decoded by any
                // peer that appends the implied zero without checking for end of input.
                // That is a common enough shape to be a real interoperability hazard, and
                // the satellite sends 1035-byte cover chunks.
                if (r == len) { open = false; break; }
                code_at = w++;
                code    = 1;
            }
        }
    }
    if (open) dst[code_at] = code;
    return w;
}

size_t cobs_decode(const uint8_t* src, size_t len, uint8_t* dst, size_t dst_cap)
{
    size_t r = 0, w = 0;

    while (r < len) {
        const uint8_t code = src[r];
        if (code == 0)       return 0;   // a delimiter has no business inside a frame
        if (r + code > len)  return 0;   // the group runs off the end
        r++;
        for (uint8_t i = 1; i < code; i++) {
            if (w >= dst_cap) return 0;
            dst[w++] = src[r++];
        }
        // A group shorter than 0xFF stood in for a zero — unless it was the last group,
        // where the zero is the delimiter the caller already stripped.
        if (code != 0xFF && r < len) {
            if (w >= dst_cap) return 0;
            dst[w++] = 0;
        }
    }
    return w;
}

// ── Reader ─────────────────────────────────────────────────────────────────────────────────

void Reader::reset()
{
    n_    = 0;
    over_ = false;
    // The sequence reference goes with the framing state: reset() means "we have just
    // thrown away what was on the wire", after which the next `seq` to arrive says nothing
    // about how many frames went missing. The fault counters are cumulative and survive,
    // exactly as bad_crc_ and its neighbours do.
    have_seq_ = false;
}

size_t Reader::feed(const uint8_t* data, size_t n, Fn fn, void* ctx)
{
    size_t out = 0;

    for (size_t i = 0; i < n; i++) {
        const uint8_t b = data[i];

        if (b != 0) {
            if (n_ < MAX_WIRE) wire_[n_++] = b;
            else               over_ = true;   // keep eating: the next delimiter re-syncs us
            continue;
        }

        if (over_)     too_big_++;
        else if (n_)   deliver(fn, ctx, out);
        // else: an empty run between two delimiters, which the contract says to ignore.

        n_    = 0;
        over_ = false;
    }
    return out;
}

void Reader::deliver(Fn fn, void* ctx, size_t& count)
{
    const size_t m = cobs_decode(wire_, n_, frame_, MAX_FRAME);
    if (m < 4) {                     // malformed, or shorter than type + seq + crc16
        bad_cobs_++;
        return;
    }

    const uint16_t want = (uint16_t)(frame_[m - 2] | ((uint16_t)frame_[m - 1] << 8));
    if (crc16(frame_, m - 2) != want) {
        bad_crc_++;
        return;
    }

    Frame f;
    f.type    = frame_[0];
    f.seq     = frame_[1];
    f.payload = frame_ + 2;
    f.len     = m - 4;

    // `seq` has been parsed since 1.0 and compared by nobody. It wraps at 256 by
    // construction, so the unsigned difference IS the gap, and one step is the normal case.
    // Counted before the callback so the number is already right if the callback logs it.
    // This mirrors the satellite's own accounting byte for byte (its components/satlink/
    // frame.c), which matters: the two sides must agree on what "lost" means, or the pair
    // of counters cannot be compared across the link.
    if (have_seq_) {
        const uint8_t gap = (uint8_t)(f.seq - last_seq_);
        if (gap > 1) seq_gaps_ += (uint32_t)(gap - 1);
    }
    have_seq_ = true;
    last_seq_ = f.seq;

    if (fn) fn(ctx, f);
    count++;
}

// ── Cursor ─────────────────────────────────────────────────────────────────────────────────

uint8_t Cursor::u8()
{
    if (!ok_ || i_ + 1 > n_) { ok_ = false; return 0; }
    return p_[i_++];
}

uint16_t Cursor::u16()
{
    if (!ok_ || i_ + 2 > n_) { ok_ = false; return 0; }
    const uint16_t v = (uint16_t)(p_[i_] | ((uint16_t)p_[i_ + 1] << 8));
    i_ += 2;
    return v;
}

uint32_t Cursor::u32()
{
    if (!ok_ || i_ + 4 > n_) { ok_ = false; return 0; }
    const uint32_t v = (uint32_t)p_[i_]
                     | ((uint32_t)p_[i_ + 1] << 8)
                     | ((uint32_t)p_[i_ + 2] << 16)
                     | ((uint32_t)p_[i_ + 3] << 24);
    i_ += 4;
    return v;
}

Str Cursor::str()
{
    Str s;
    const uint16_t len = u16();
    if (!ok_) return s;
    if (i_ + len > n_) { ok_ = false; return s; }
    s.p   = (const char*)(p_ + i_);
    s.len = len;
    i_ += len;
    return s;
}

bool Cursor::bytes(const uint8_t*& out, size_t n)
{
    if (!ok_ || i_ + n > n_) { ok_ = false; return false; }
    out = p_ + i_;
    i_ += n;
    return true;
}

// ── Packer ─────────────────────────────────────────────────────────────────────────────────

void Packer::u8(uint8_t v)
{
    if (!ok_ || i_ + 1 > cap_) { ok_ = false; return; }
    p_[i_++] = v;
}

void Packer::u16(uint16_t v)
{
    if (!ok_ || i_ + 2 > cap_) { ok_ = false; return; }
    p_[i_++] = (uint8_t)(v & 0xFF);
    p_[i_++] = (uint8_t)(v >> 8);
}

void Packer::u32(uint32_t v)
{
    if (!ok_ || i_ + 4 > cap_) { ok_ = false; return; }
    p_[i_++] = (uint8_t)(v & 0xFF);
    p_[i_++] = (uint8_t)((v >> 8) & 0xFF);
    p_[i_++] = (uint8_t)((v >> 16) & 0xFF);
    p_[i_++] = (uint8_t)((v >> 24) & 0xFF);
}

void Packer::bytes(const uint8_t* s, size_t n)
{
    if (!ok_ || i_ + n > cap_) { ok_ = false; return; }
    memcpy(p_ + i_, s, n);
    i_ += n;
}

void Packer::str(const char* s, size_t n)
{
    if (n > 0xFFFF) { ok_ = false; return; }
    u16((uint16_t)n);
    if (n) bytes((const uint8_t*)s, n);
}

void Packer::str(const char* s)
{
    str(s, s ? strlen(s) : 0);
}

// ── Writer ─────────────────────────────────────────────────────────────────────────────────

size_t Writer::frame(uint8_t type, const uint8_t* payload, size_t len)
{
    if (len > MAX_PAYLOAD) return 0;

    body_[0] = type;
    body_[1] = seq_;
    if (len) memcpy(body_ + 2, payload, len);

    const uint16_t c = crc16(body_, 2 + len);
    body_[2 + len]     = (uint8_t)(c & 0xFF);
    body_[2 + len + 1] = (uint8_t)(c >> 8);

    // Both delimiters go on the wire. Back-to-back frames therefore show the receiver an
    // empty run between them, which the contract tells it to ignore — and in exchange a
    // receiver that joins mid-stream has a leading delimiter to latch onto.
    wire_[0]     = 0;
    const size_t n = cobs_encode(body_, len + 4, wire_ + 1);
    wire_[1 + n] = 0;

    seq_++;
    return n + 2;
}

// ── Parsing ────────────────────────────────────────────────────────────────────────────────

bool parse(const Frame& f, Hello& o)
{
    if (f.type != MSG_HELLO) return false;
    Cursor c(f.payload, f.len);
    o.major = c.u8();
    o.minor = c.u8();
    o.caps  = c.u32();
    o.build = c.str();
    return c.ok();
}

bool parse(const Frame& f, BtState& o)
{
    if (f.type != MSG_BT_STATE) return false;
    Cursor c(f.payload, f.len);
    o.state = c.u8();
    o.codec = c.u8();
    c.bytes(o.addr, 6);
    o.name = c.str();
    return c.ok();
}

bool parse(const Frame& f, TrackMeta& o)
{
    if (f.type != MSG_TRACK_META) return false;
    Cursor c(f.payload, f.len);
    o.title  = c.str();
    o.artist = c.str();
    o.album  = c.str();
    return c.ok();
}

bool parse(const Frame& f, TrackPos& o)
{
    if (f.type != MSG_TRACK_POS) return false;
    Cursor c(f.payload, f.len);
    o.state      = c.u8();
    o.elapsed_ms = c.u32();
    o.total_ms   = c.u32();
    // The contract puts this cleanup on the satellite, which met a phone that answered
    // -1000 for the length. Repeating it here costs two comparisons and means an older
    // satellite build cannot put a 49-day track on the screen.
    //
    // BOTH fields, not only the length. A phone that gets one of them wrong has no reason
    // to get the other right, and the position is the one the progress bar is drawn from:
    // a nonsense length hides the bar, a nonsense position moves it to the far end and
    // writes "999:59" under it. The rule is the same and so is the bound.
    if (o.total_ms   > POS_MAX_MS) o.total_ms   = 0;
    if (o.elapsed_ms > POS_MAX_MS) o.elapsed_ms = 0;
    return c.ok();
}

bool parse(const Frame& f, StreamFmt& o)
{
    if (f.type != MSG_STREAM_FMT) return false;
    Cursor c(f.payload, f.len);
    o.rate_hz  = c.u32();
    o.bits     = c.u8();
    o.channels = c.u8();
    return c.ok();
}

bool parse(const Frame& f, Volume& o)
{
    if (f.type != MSG_VOLUME_STATE) return false;
    Cursor c(f.payload, f.len);
    o.volume = c.u8();
    o.source = c.u8();
    return c.ok();
}

bool parse(const Frame& f, Ping& o)
{
    if (f.type != MSG_PING) return false;
    Cursor c(f.payload, f.len);
    o.tick_ms = c.u32();
    return c.ok();
}

bool parse(const Frame& f, Pong& o)
{
    if (f.type != MSG_PONG) return false;
    Cursor c(f.payload, f.len);
    o.echo_ms = c.u32();
    o.own_ms  = c.u32();
    return c.ok();
}

bool parse(const Frame& f, LogLine& o)
{
    if (f.type != MSG_LOG) return false;
    Cursor c(f.payload, f.len);
    o.level = c.u8();
    o.text  = c.str();
    return c.ok();
}

bool parse(const Frame& f, CoverChunk& o)
{
    if (f.type != MSG_COVER) return false;
    Cursor c(f.payload, f.len);
    o.chunk     = c.u16();
    o.total     = c.u16();
    o.mime      = c.u8();
    o.total_len = c.u32();
    o.len       = c.u16();
    c.bytes(o.data, o.len);
    return c.ok();
}

bool parse(const Frame& f, CoverRetry& o)
{
    if (f.type != MSG_COVER_RETRY) return false;
    Cursor c(f.payload, f.len);
    o.nonce      = c.u32();
    o.generation = c.u32();
    // Both nonzero is the contract's own wording, and it is what makes an all-zero frame
    // — the shape a truncated or padded one takes — fail here instead of naming a pair.
    return c.ok() && f.len == 8 && o.nonce && o.generation;
}

// ── 1.8 ──

bool parse(const Frame& f, PairRequest& o)
{
    if (f.type != MSG_PAIR_REQUEST) return false;
    Cursor c(f.payload, f.len);
    c.bytes(o.addr, 6);
    o.kind    = c.u8();
    o.passkey = c.u32();
    o.name    = c.str();
    // A kind this version does not know asks for something nobody can show; a passkey past
    // six digits is not one. Either way the frame is not a request this side can act on.
    return c.ok() && o.kind <= PAIR_PASSKEY && o.passkey <= 999999u;
}

bool parse(const Frame& f, PairResult& o)
{
    if (f.type != MSG_PAIR_RESULT) return false;
    Cursor c(f.payload, f.len);
    c.bytes(o.addr, 6);
    o.status = c.u8();
    // The contract reserves 4 and up and has the host show them as 3.
    if (o.status > PAIR_FAILED) o.status = PAIR_FAILED;
    return c.ok();
}

bool parse(const Frame& f, BtConfig& o)
{
    if (f.type != MSG_BT_CONFIG) return false;
    Cursor c(f.payload, f.len);
    o.radio            = c.u8();
    o.discoverable     = c.u8();
    o.auto_reconnect   = c.u8();
    o.codecs_supported = c.u8();
    o.codecs_enabled   = c.u8();
    o.name             = c.str();
    return c.ok();
}

bool parse(const Frame& f, PairReply& o)
{
    if (f.type != MSG_PAIR_REPLY) return false;
    Cursor c(f.payload, f.len);
    c.bytes(o.addr, 6);
    o.accept = c.u8();
    return c.ok() && f.len == 7;
}

bool parse(const Frame& f, BtConfigSet& o)
{
    if (f.type != MSG_BT_CONFIG_SET) return false;
    Cursor c(f.payload, f.len);
    o.radio          = c.u8();
    o.discoverable   = c.u8();
    o.auto_reconnect = c.u8();
    o.codecs_enabled = c.u8();
    o.name           = c.str();
    return c.ok();
}

bool parse(const Frame& f, BondRemove& o)
{
    if (f.type != MSG_BOND_REMOVE) return false;
    Cursor c(f.payload, f.len);
    c.bytes(o.addr, 6);
    return c.ok() && f.len == 6;
}

bool parse_bond_list(const Frame& f, Bond* out, size_t cap, size_t& count, uint8_t& total)
{
    count = 0;
    total = 0;
    if (f.type != MSG_BOND_LIST) return false;

    Cursor c(f.payload, f.len);
    total = c.u8();
    const uint8_t n = c.u8();
    for (uint8_t i = 0; i < n; i++) {
        Bond b;
        c.bytes(b.addr, 6);
        b.flags = c.u8();
        b.name  = c.str();
        if (!c.ok()) return false;
        if (i < cap) out[i] = b;
    }
    count = n;
    return c.ok();
}

// ── Building ───────────────────────────────────────────────────────────────────────────────

size_t build_hello(Writer& w, uint32_t caps, const char* build)
{
    uint8_t buf[80];
    Packer  p(buf, sizeof buf);
    p.u8(PROTO_MAJOR);
    p.u8(PROTO_MINOR);
    p.u32(caps);
    p.str(build);
    return p.ok() ? w.frame(MSG_HELLO, p.data(), p.size()) : 0;
}

size_t build_ping(Writer& w, uint32_t tick_ms)
{
    uint8_t buf[4];
    Packer  p(buf, sizeof buf);
    p.u32(tick_ms);
    return p.ok() ? w.frame(MSG_PING, p.data(), p.size()) : 0;
}

size_t build_pong(Writer& w, uint32_t echo_ms, uint32_t own_ms)
{
    uint8_t buf[8];
    Packer  p(buf, sizeof buf);
    p.u32(echo_ms);
    p.u32(own_ms);
    return p.ok() ? w.frame(MSG_PONG, p.data(), p.size()) : 0;
}

size_t build_transport(Writer& w, uint8_t cmd)
{
    return w.frame(MSG_CMD_TRANSPORT, &cmd, 1);
}

size_t build_volume(Writer& w, uint8_t volume)
{
    return w.frame(MSG_CMD_VOLUME, &volume, 1);
}

size_t build_radio(Writer& w, bool on, bool discoverable, bool disconnect)
{
    const uint8_t buf[3] = {
        (uint8_t)(on ? 1 : 0),
        (uint8_t)(discoverable ? 1 : 0),
        (uint8_t)(disconnect ? 1 : 0),
    };
    return w.frame(MSG_CMD_RADIO, buf, sizeof buf);
}

size_t build_cover_retry(Writer& w, uint32_t nonce, uint32_t generation)
{
    // A zero in either field names no pair, and the satellite would drop the frame anyway.
    // Refusing to build it keeps the sequence number for a frame somebody can act on.
    if (!nonce || !generation) return 0;
    uint8_t buf[8];
    Packer  p(buf, sizeof buf);
    p.u32(nonce);
    p.u32(generation);
    return p.ok() ? w.frame(MSG_COVER_RETRY, p.data(), p.size()) : 0;
}

// ── 1.8 ──

size_t build_pair_reply(Writer& w, const uint8_t addr[6], bool accept)
{
    if (!addr) return 0;
    uint8_t buf[7];
    Packer  p(buf, sizeof buf);
    p.bytes(addr, 6);
    p.u8(accept ? 1 : 0);
    return p.ok() ? w.frame(MSG_PAIR_REPLY, p.data(), p.size()) : 0;
}

size_t build_bt_config_set(Writer& w, bool radio, bool discoverable, bool auto_reconnect,
                           uint8_t codecs_enabled, const char* name)
{
    const size_t n = name ? strlen(name) : 0;
    if (n > BT_NAME_MAX) return 0;
    uint8_t buf[4 + 2 + BT_NAME_MAX];
    Packer  p(buf, sizeof buf);
    p.u8(radio ? 1 : 0);
    p.u8(discoverable ? 1 : 0);
    p.u8(auto_reconnect ? 1 : 0);
    p.u8(codecs_enabled);
    p.str(name ? name : "", n);
    return p.ok() ? w.frame(MSG_BT_CONFIG_SET, p.data(), p.size()) : 0;
}

size_t build_bond_remove(Writer& w, const uint8_t* addr)
{
    static const uint8_t ALL[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return w.frame(MSG_BOND_REMOVE, addr ? addr : ALL, 6);
}

size_t build_bt_refresh(Writer& w)
{
    const uint8_t none = 0;
    return w.frame(MSG_BT_REFRESH, &none, 0);
}

}  // namespace sat

#include "sat_link/stream_control.h"
namespace sat { StreamControl& stream_control() { static StreamControl control; return control; } }
