#include "doctest.h"
#include "startup_probe.h"
#include <vector>

TEST_CASE("Startup probe distinguishes leading silence from channel holes across blocks") {
    audio_core::StartupProbe probe;
    std::vector<float> pcm(80 * 2, 0.0f);
    for (int i = 20; i < 80; ++i) { pcm[2*i] = 0.25f; pcm[2*i+1] = -0.25f; }
    for (int i = 35; i < 60; ++i) pcm[2*i] = 0.0f;
    probe.observe(pcm.data(), 45);
    probe.observe(pcm.data()+90, 35);
    CHECK(probe.frames == 80);
    CHECK(probe.ch[0].leading == 20);
    CHECK(probe.ch[1].leading == 20);
    CHECK(probe.ch[0].holes == 1);
    CHECK(probe.ch[0].longest == 25);
    CHECK(probe.ch[1].holes == 0);
    CHECK(probe.ch[0].step == doctest::Approx(0.25f));
}
TEST_CASE("Startup probe measures a transient after the initial fade window") {
    audio_core::StartupProbe probe;
    std::vector<float> pcm(48000 * 2, 0.001f);
    pcm[24000 * 2] = 0.9f;
    for (size_t i=0;i<48000;i+=240) probe.observe(pcm.data()+2*i,240);
    CHECK(probe.ch[0].peak == doctest::Approx(0.9f));
    CHECK(probe.ch[0].step == doctest::Approx(0.899f));
    CHECK(probe.ch[1].peak == doctest::Approx(0.001f));
    CHECK(probe.ch[0].holes == 0);
}

#include "audio_pipe.h"
TEST_CASE("Startup fade survives codec leading silence across multiple chunks") {
    size_t remaining = 480;
    std::vector<float> silence(1024*2, 0.0f);
    audio_core::fade_start(silence.data(),1024,remaining,480);
    CHECK(remaining == 480);
    std::vector<float> next(1024*2,0.0f);
    for (size_t i=262;i<1024;++i) {next[2*i]=0.5f;next[2*i+1]=-0.25f;}
    audio_core::fade_start(next.data(),1024,remaining,480);
    CHECK(next[262*2] == 0.0f);
    CHECK(next[262*2+1] == 0.0f);
    CHECK(next[263*2] > 0.0f);
    CHECK(next[263*2] < 0.00001f);
    CHECK(next[742*2] == 0.5f);
    CHECK(next[742*2+1] == -0.25f);
    CHECK(remaining == 0);
}
TEST_CASE("Startup fade triggers on either channel and preserves internal zero crossings") {
    size_t remaining=4;
    float pcm[]={0,1, 0,0, 1,1, 1,1, 1,1};
    audio_core::fade_start(pcm,5,remaining,4);
    CHECK(pcm[1] == 0.0f);
    CHECK(pcm[4] == doctest::Approx(0.5f));
    CHECK(pcm[8] == 1.0f);
    CHECK(remaining == 0);
}
