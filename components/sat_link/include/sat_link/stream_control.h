#pragma once
#include "format_sync.h"
#include <atomic>
namespace sat {
struct StreamRequest { fmt_request_t format{}; uint32_t revision=0; };
// UART is the single publisher, audio the consumer. Readers never spin if they
// preempt a publication: they try again next block. seq_cst orders all fields.
class StreamControl {
public:
    bool snapshot(StreamRequest& out) const {
        const uint32_t before=sequence_.load();
        if(before&1u) return false;
        out.format.nonce=nonce_.load(); out.format.generation=generation_.load();
        out.format.rate_hz=rate_.load(); const uint32_t shape=shape_.load();
        out.format.bits=(uint8_t)shape; out.format.channels=(uint8_t)(shape>>8);
        out.revision=before;
        return sequence_.load()==before;
    }
    bool publish(const fmt_request_t& r) {
        StreamRequest old; // publisher cannot be preempted by another publisher
        if(!snapshot(old)) return false;
        if(fmt_same(&old.format,&r)) return true;
        if(r.nonce && old.format.nonce==r.nonce) {
            if(r.generation<=old.format.generation) return false;
        }
        sequence_.fetch_add(1);
        nonce_.store(r.nonce); generation_.store(r.generation); rate_.store(r.rate_hz);
        shape_.store((uint32_t)r.bits | ((uint32_t)r.channels<<8));
        done_.store(0); sequence_.fetch_add(1);
        return true;
    }
    void clear() { publish({}); }
    void invalidate_result() { done_.store(0); }
    void complete(uint32_t revision,bool success) { done_.store(revision | (success ? 0u : 1u)); }
    bool result(uint32_t revision,uint8_t& status) const {
        const uint32_t d=done_.load();
        if(!revision || (d & ~1u)!=revision) return false;
        status=(d&1u) ? FMT_REFUSED : FMT_OK; return true;
    }
    // Protocol 1.9: the satellite's latest HELLO says its I2S is a slave, so the audio
    // task may clock the link. Set and cleared by the UART side only; the audio task
    // reads it at every block and re-adopts the format when it changes.
    void set_host_clocks(bool on) { host_clocks_.store(on); }
    bool host_clocks() const { return host_clocks_.load(); }
private:
    std::atomic<uint32_t> sequence_{0},nonce_{0},generation_{0},rate_{0},shape_{0},done_{0};
    std::atomic<bool> host_clocks_{false};
};
StreamControl& stream_control();
}
