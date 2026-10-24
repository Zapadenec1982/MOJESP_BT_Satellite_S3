#include "doctest.h"
#include "dsp_chain.h"
#include <cmath>
#include <initializer_list>

using audio_core::DspChain;

static double side_gain(DspChain& chain, unsigned rate, float hz) {
    double in_energy = 0, out_energy = 0;
    float block[DspChain::BLOCK * 2];
    for (unsigned start = 0; start < rate; start += DspChain::BLOCK) {
        for (unsigned i = 0; i < DspChain::BLOCK; ++i) {
            const float x = 0.1f * std::sin(6.283185307179586 * hz * (start + i) / rate);
            block[2*i] = x;
            block[2*i+1] = -x;
            if (start > rate / 4) in_energy += x * x;
        }
        chain.process(block, DspChain::BLOCK);
        if (start > rate / 4)
            for (unsigned i = 0; i < DspChain::BLOCK; ++i)
                out_energy += block[2*i] * block[2*i];
    }
    return std::sqrt(out_energy / in_energy);
}

TEST_CASE("mono bass keeps its corner in hertz across output retunes") {
    DspChain chain;
    chain.set_volume_percent(100);
    chain.set_mono_bass(200);
    // Deliberately do not modify global g_fs_hz: the chain's rate API is sufficient.
    for (unsigned rate : {44100u, 96000u, 48000u, 192000u, 32000u}) {
        chain.set_sample_rate(static_cast<float>(rate));
        chain.reset();
        CAPTURE(rate);
        CHECK(side_gain(chain, rate, 200) == doctest::Approx(0.7071).epsilon(0.025));
        CHECK(side_gain(chain, rate, 50) < 0.07);
        CHECK(side_gain(chain, rate, 1000) > 0.99);
    }
}

TEST_CASE("mono bass disabled survives retune and reset clears side history") {
    DspChain chain;
    chain.set_volume_percent(100);
    chain.set_mono_bass(200);
    CHECK(side_gain(chain, 48000, 200) < 0.75);
    chain.reset();
    float silence[DspChain::BLOCK * 2] = {};
    chain.process(silence, DspChain::BLOCK);
    for (float x : silence) CHECK(x == 0.0f);
    chain.set_mono_bass(0);
    chain.set_sample_rate(96000);
    CHECK(side_gain(chain, 96000, 50) == doctest::Approx(1.0).epsilon(0.001));
}
