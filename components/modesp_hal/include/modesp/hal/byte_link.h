/**
 * @file byte_link.h
 * @brief Seam of the byte-link family — the wire to a co-processor.
 *
 * A module behind this seam owns the PROTOCOL: what is said, when, and what to do with the
 * answer. The driver owns the WIRE: which port, which pins, which baud rate, how to be told
 * that bytes arrived, and — the part no module can do for itself — how to know that bytes
 * were LOST. On this board the far end is the Bluetooth satellite and the protocol is
 * docs/LINK_PROTOCOL.md, carried by COBS frames that components/sat_link builds and parses.
 *
 * Framing is deliberately NOT here. A transport that dropped a byte cannot repair the frame
 * that byte was in; all it can do is say so, which is what Rx::Lost is for, and the framer
 * upstream resyncs on the next delimiter. That division is why the seam is this shape:
 * wait_rx() reports the loss, read() hands over bytes, and nothing here knows what a frame is.
 *
 * Module-bound like lcd_port.h and audio_sink.h: resolved by the owning module in on_bind()
 * through DriverRegistry::create_link(). No ESP-IDF type appears here, so the header builds
 * in tests/host.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace modesp::link {

/// What the transport knows about its own wire. Monotonic: a torn read costs one stale log
/// line and nothing else.
struct LinkStats {
    uint32_t rx_bytes = 0;
    uint32_t tx_bytes = 0;
    /// Times the transport dropped incoming bytes (FIFO or ring overflow). Not the number of
    /// bytes lost — the hardware does not say — but the number of times sync was broken.
    uint32_t overruns = 0;
};

/// What the receive line is electrically attached to, as the die sees it. A reading taken at
/// a connector says what the connector sees, not what the die sees.
enum class RxPin : uint8_t { Unknown, Floating, High, Low, Odd };

/// Outcome of a wait for incoming bytes.
///
/// Idle is NOT "there is nothing to read": the size in a wake-up is a hint about one
/// interrupt, not a statement about the buffer, so the caller drains with read() after Idle
/// and after Data alike. Only Lost is different, and it is different in both directions —
/// the remainder has already been thrown away, and the framer must be reset before the next
/// byte reaches it.
enum class Rx : uint8_t {
    Idle,   ///< the timeout passed without a wake-up; drain anyway
    Data,   ///< something arrived; call read() until it returns 0
    Lost,   ///< the transport dropped bytes — the frame in flight is gone, resync the framer
};

class IByteLink {
public:
    virtual ~IByteLink() = default;

    /// Claim the wire and make it ready to carry the protocol. false ⇒ no link, which is not
    /// fatal: a board without the accessory wired must still boot.
    virtual bool init() = 0;

    /// True when wait_rx() can actually block until bytes arrive. False ⇒ the caller must
    /// poll read(), because this transport has no way to wake anybody — and then it also
    /// cannot report Rx::Lost, because nothing is watching for the overflow.
    virtual bool can_wait() const = 0;

    /// Block up to timeout_ms for the wire to have something to say. A timeout must be used
    /// rather than an infinite wait: a caller that also needs to notice a stop request, or to
    /// tick a liveness counter, gets to do both on a silent link.
    virtual Rx wait_rx(uint32_t timeout_ms) = 0;

    /// Take up to `max` bytes that have already arrived. Never blocks. 0 = nothing left.
    virtual int read(uint8_t* out, size_t max) = 0;

    /// Put `len` bytes on the wire. Returns how many went out.
    virtual int write(const uint8_t* data, size_t len) = 0;

    /// Throw away everything received but not yet read.
    virtual void flush_rx() = 0;

    virtual LinkStats stats() const = 0;

    /// Ask the receive line itself what it is attached to. Costs a few milliseconds and hands
    /// the line straight back; the wire must be idle. Unknown when the transport cannot say.
    virtual RxPin probe_rx() = 0;

    /// One line naming the wire for the boot log: "UART1 460800 8N1 TX=26 RX=27".
    virtual const char* describe() const = 0;

    /// Driver type string, as the manifest spells it ("uart_link").
    virtual const char* name() const = 0;

    virtual bool is_healthy() const = 0;
};

/// What a probe_rx() reading means, in one clause. Shared so every link says it the same way.
inline const char* rx_pin_words(RxPin p) {
    switch (p) {
        case RxPin::High:     return "held high by something outside - a transmitter at rest";
        case RxPin::Low:      return "held LOW - on ground, or on an output driving low";
        case RxPin::Floating: return "attached to nothing - it follows our own pull both ways";
        case RxPin::Unknown:  return "not probeable on this transport";
        default:              return "reading inconsistently";
    }
}

} // namespace modesp::link
