#include "doctest.h"
#include "audio_seek.h"
#include "audio_pipe.h"
#include <vector>

namespace {
void append16(std::vector<uint8_t>& b, uint16_t v) { b.push_back(v); b.push_back(v >> 8); }
void append32(std::vector<uint8_t>& b, uint32_t v) { append16(b,v); append16(b,v >> 16); }
void chunk(std::vector<uint8_t>& b, const char* name, const std::vector<uint8_t>& data) {
    b.insert(b.end(), name, name+4); append32(b,(uint32_t)data.size());
    b.insert(b.end(), data.begin(), data.end());
    if(data.size() & 1) b.push_back(0);
}
std::vector<uint8_t> wav(bool junk) {
    std::vector<uint8_t> b = {'R','I','F','F',0,0,0,0,'W','A','V','E'}, fmt;
    if(junk) chunk(b,"JUNK",{1,2,3});
    append16(fmt,1); append16(fmt,2); append32(fmt,48000); append32(fmt,48000*6);
    append16(fmt,6); append16(fmt,24); chunk(b,"fmt ",fmt);
    std::vector<uint8_t> pcm(48000*6*2);
    for(size_t i=0;i<pcm.size();i+=6) {
        pcm[i]=0x56; pcm[i+1]=0x34; pcm[i+2]=0x12;
        pcm[i+3]=0x67; pcm[i+4]=0x45; pcm[i+5]=0x23;
    }
    chunk(b,"data",pcm);
    chunk(b,"LIST",{9,9,9,9});
    const uint32_t riff = (uint32_t)b.size()-8;
    for(unsigned i=0;i<4;++i) b[4+i]=(uint8_t)(riff >> (i*8));
    return b;
}
size_t read_at(void* ctx, uint64_t off, void* dst, size_t n) {
    auto& b=*static_cast<std::vector<uint8_t>*>(ctx);
    if(off > b.size() || n > b.size()-off) return 0;
    std::memcpy(dst,b.data()+off,n); return n;
}
}

TEST_CASE("WAV seek: stereo24 is aligned to PCM data with standard or padded headers") {
    for(bool junk : {false,true}) {
        auto file=wav(junk);
        audio_core::WavPcmData pcm;
        REQUIRE(audio_core::read_wav_pcm_data(&file,read_at,file.size(),pcm));
        CHECK(pcm.offset == (junk ? 56 : 44));
        CHECK(pcm.bytes == 48000*6*2);
        CHECK(audio_core::pcm_seek_byte(pcm,0) == pcm.offset);
        CHECK(audio_core::pcm_seek_byte(pcm,-1) == pcm.offset);
        const auto middle=audio_core::pcm_seek_byte(pcm,1);
        CHECK(middle == pcm.offset+48000*6);
        CHECK((middle-pcm.offset)%6 == 0);
        float decoded[2]{};
        REQUIRE(audio_core::pcm_to_f32(file.data()+middle,6,24,2,decoded,1) == 1);
        CHECK(decoded[0] == doctest::Approx(0x123456/8388608.0));
        CHECK(decoded[1] == doctest::Approx(0x234567/8388608.0));
        CHECK(audio_core::pcm_seek_byte(pcm,2) == pcm.offset+pcm.bytes-6);
        CHECK(audio_core::pcm_seek_byte(pcm,100) == pcm.offset+pcm.bytes-6);
    }
}

TEST_CASE("WAV seek: malformed or compressed layouts are refused") {
    auto file=wav(false);
    audio_core::WavPcmData pcm;
    CHECK_FALSE(audio_core::read_wav_pcm_data(&file,read_at,file.size()-1,pcm));
    CHECK_FALSE(pcm.valid());
    file[20]=2; // ADPCM's blocks are not individual PCM frames
    CHECK_FALSE(audio_core::read_wav_pcm_data(&file,read_at,file.size(),pcm));
    file[20]=1;
    file[32]=5; // invalid block align for stereo24
    CHECK_FALSE(audio_core::read_wav_pcm_data(&file,read_at,file.size(),pcm));
    file[32]=6;
    file[40]=file[41]=file[42]=file[43]=0xff; // data beyond the RIFF/file end
    CHECK_FALSE(audio_core::read_wav_pcm_data(&file,read_at,file.size(),pcm));
}
