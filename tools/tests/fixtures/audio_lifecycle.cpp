#include <cassert>
#include <atomic>
#include <cstdint>
#include <vector>
#include "pcm_ring.h"
#include "bt_clock_health.h"
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
constexpr bool LINE_IN_FITTED = true;   // the fixture's port has an ADC; AUX is exercised below
namespace modesp::audio {
struct AudioPortCaps {
    uint32_t min_rate_hz = 32000, max_rate_hz = 192000;
    uint16_t mclk_multiple = 256;
    bool has_rx = true;
};
}
struct Port {
    std::vector<uint32_t> calls;
    uint32_t accepted = 0;
    bool rx = true;
    modesp::audio::AudioPortCaps port_caps() { auto c=modesp::audio::AudioPortCaps{}; c.has_rx=rx; return c; }
    bool set_rate(uint32_t hz, uint16_t) { calls.push_back(hz); assert(calls.size()<10); return hz==accepted; }
    bool mclk = true;
    void set_line_input(bool on) { mclk = on; }
};
namespace sat {
struct Control { void invalidate_result() {} };
Control& stream_control() { static Control c; return c; }
}
void resampleReset(void*) {}
enum { SRC_OFF, SRC_TONE, SRC_AUX, SRC_BT, SRC_RADIO, SRC_FILE, SRC_AIRPLAY, SRC_USB };
audio_core::PcmRing::Owner ring_owner(int32_t src) {   // as audio_module.cpp's
    using R = audio_core::PcmRing;
    return src == SRC_FILE ? R::OWNER_FILE : src == SRC_RADIO ? R::OWNER_RADIO
         : src == SRC_AIRPLAY ? R::OWNER_AIRPLAY : src == SRC_USB ? R::OWNER_USB
         : R::OWNER_NONE;
}
struct AudioModule {
    static constexpr bool is_ring_source(int32_t s) {
        return s == SRC_FILE || s == SRC_RADIO || s == SRC_AIRPLAY || s == SRC_USB;
    }
    Port *main_, *bt_;
    struct Dsp { void reset() {} } dsp_;
    uint32_t out_rate_=96000, refused_rate_=0, session_rate_=0;
    static constexpr uint32_t PLAY_MAX_HZ = 96000;   // as audio_module.h
    uint16_t out_mclk_mult_=256;
    int source_=SRC_FILE;
    bool gate_closing_=true, priming_=false, bt_clock_follow_=true, bt_primed_=true;
    float sig_gain_=1, last_in_l_=1, last_in_r_=1;
    bool bt_trim_dead_=true, bt_clock_unhealthy_=true, bt_clock_recover_=true, bt_fmt_ok_=true;
    bool bt_diag_stopped_=true;     // apply_source() rearms the 60 s BT diagnostic (e0a49f8)
    int64_t bt_diag_start_us_=1;
    uint32_t bt_fmt_revision_=1, bt_asrc_blocks_=5, bt_fmt_hz_=48000, bt_fmt_bits_=16;
    std::atomic<size_t> bt_head_{12}, bt_tail_{3};
    void* asrc_=nullptr;
    double asrc_ratio_=1;
    audio_core::BtClockHealth bt_clock_health_;
    unsigned ring_sources_offered() { return 1<<SRC_AIRPLAY; }
    void pause_bt_reader() {}
    void reset_out_trim() {}
    bool bt_fmt_master_ = false;
    void release_link() {}
    bool park_bt_off_pll() { return true; }
    void adopt_clock(uint32_t hz, uint16_t multiple) {
        out_rate_=hz; out_mclk_mult_=multiple; audio_core::pcm_ring().set_out_rate(hz);
    }
    void retune(uint32_t hz, uint16_t multiple=0);
    void apply_source(int32_t src);
    bool source_refused(int32_t src);   // defined right after apply_source, in the same slice
};
int main() {
    Port port;
    AudioModule module{&port,&port};
    module.retune(22050);
    assert((port.calls==std::vector<uint32_t>{22050,44100}));
    assert(module.out_rate_==96000 && audio_core::pcm_ring().out_rate()==96000);
    assert(module.refused_rate_==22050);
    port.calls.clear(); port.accepted=44100;
    module.retune(22050);
    assert(port.calls.size()==2 && module.out_rate_==44100);
    assert(audio_core::pcm_ring().out_rate()==44100);
    port.calls.clear(); port.accepted=0;
    module.retune(48000); // fallback equals requested: only one attempt
    assert(port.calls.size()==1 && module.out_rate_==44100);
    port.calls.clear(); port.accepted=96000;
    module.retune(96000);
    assert(port.calls.size()==1 && module.out_rate_==96000);
    // The ceiling (PLAY_MAX_HZ): 192 kHz is never asked of the port, which could take it; the
    // clock goes to 96 and the producer is told 96. 176.4 goes to 88.2 the same way.
    port.calls.clear(); module.out_rate_=48000;
    module.retune(192000);
    assert((port.calls==std::vector<uint32_t>{96000}));
    assert(module.out_rate_==96000 && module.refused_rate_==192000);
    assert(audio_core::pcm_ring().out_rate()==96000);
    port.calls.clear(); port.accepted=88200;
    module.retune(176400);
    assert((port.calls==std::vector<uint32_t>{88200}) && module.out_rate_==88200);
    for(int src : {SRC_AUX,SRC_TONE,SRC_BT}) {
        module.source_=SRC_FILE; module.gate_closing_=true; module.sig_gain_=1;
        module.priming_=false;
        module.apply_source(src);
        assert(module.source_==src && !module.gate_closing_);
        assert(module.sig_gain_==0 && module.priming_);
        assert(module.last_in_l_==0 && module.last_in_r_==0);
    }
    module.source_=SRC_FILE; module.gate_closing_=true; port.rx=false;
    module.apply_source(SRC_AUX);
    assert(module.source_==SRC_FILE && module.gate_closing_);
    // A producer deselected without a flush leaves its tail queued: the next ring source must
    // not prime on it. A queue that is already the new source's own is left alone.
    auto& ring = audio_core::pcm_ring();
    float frames[2 * 8] = {};
    assert(ring.init(64) && ring.claim(audio_core::PcmRing::OWNER_RADIO));
    assert(ring.write(audio_core::PcmRing::OWNER_RADIO, frames, 8) == 8);
    ring.release(audio_core::PcmRing::OWNER_RADIO);
    module.source_=SRC_BT;
    module.apply_source(SRC_FILE);
    assert(ring.available()==0);
    assert(ring.claim(audio_core::PcmRing::OWNER_RADIO));
    assert(ring.write(audio_core::PcmRing::OWNER_RADIO, frames, 8) == 8);
    module.source_=SRC_BT;
    module.apply_source(SRC_RADIO);
    assert(ring.available()==8);
}
