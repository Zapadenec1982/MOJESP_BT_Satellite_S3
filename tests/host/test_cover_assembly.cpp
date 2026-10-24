#include "doctest.h"
#include "sat_link/cover_assembly.h"
#include "sat_link/sat_link.h"
#include <vector>

namespace {
std::vector<uint8_t> part(uint32_t n,uint32_t g,uint32_t total,uint32_t off,size_t bytes) {
    std::vector<uint8_t> p(16+bytes,0xa5);
    cover_put32(p.data(),n); cover_put32(p.data()+4,g);
    cover_put32(p.data()+8,total); cover_put32(p.data()+12,off);
    return p;
}
}
TEST_CASE("cover requires reset and exact ordered complete content") {
    sat::CoverAssembly a;
    std::vector<uint8_t> dst(1500);
    auto p=part(42,1,1500,0,1000);
    CHECK_FALSE(a.append(p.data(),p.size(),dst.data(),dst.size()));
    a.reset(42,1);
    CHECK(a.append(p.data(),p.size(),dst.data(),dst.size()));
    CHECK_FALSE(a.complete);
    p=part(42,1,1500,1000,500);
    CHECK(a.append(p.data(),p.size(),dst.data(),dst.size()));
    CHECK(a.complete);
    CHECK(a.used==1500);
    for(auto b:dst) CHECK(b==0xa5);
    CHECK_FALSE(a.append(p.data(),p.size(),dst.data(),dst.size()));
}
TEST_CASE("late cover from old track or boot cannot corrupt new track") {
    sat::CoverAssembly a; a.reset(7,2);
    uint8_t dst[8]{};
    for(auto p:{part(7,1,8,0,8),part(6,2,8,0,8)}) {
        CHECK_FALSE(a.append(p.data(),p.size(),dst,sizeof dst));
        CHECK_FALSE(a.failed);
    }
    auto p=part(7,2,8,0,8);
    REQUIRE(a.append(p.data(),p.size(),dst,sizeof dst));
    CHECK(a.complete);
}
TEST_CASE("cover rejects gaps duplicates inconsistent totals and overflow") {
    std::vector<uint8_t> dst(COVER_BYTES_MAX);
    for(auto bad:{part(7,2,1500,1000,500),part(7,2,0,0,1),
                  part(7,2,COVER_BYTES_MAX+1,0,1000),part(7,2,10,0,11),
                  part(7,2,2000,0,1001),part(7,2,2000,0,999),
                  part(7,2,2000,0,0),part(7,2,10,UINT32_MAX,10)}) {
        sat::CoverAssembly a; a.reset(7,2);
        CHECK_FALSE(a.append(bad.data(),bad.size(),dst.data(),dst.size()));
        CHECK(a.failed);
    }
    for(auto bad:{part(7,2,1500,0,1000),part(7,2,1600,1000,600)}) {
        sat::CoverAssembly a; a.reset(7,2);
        auto first=part(7,2,1500,0,1000);
        REQUIRE(a.append(first.data(),first.size(),dst.data(),dst.size()));
        CHECK_FALSE(a.append(bad.data(),bad.size(),dst.data(),dst.size()));
        CHECK(a.failed);
    }
}
TEST_CASE("cover maximum size fits existing framing and reassembles") {
    std::vector<uint8_t> dst(COVER_BYTES_MAX);
    sat::CoverAssembly a; a.reset(7,2);
    struct Context { sat::CoverAssembly* a; std::vector<uint8_t>* dst; } ctx{&a,&dst};
    sat::Writer w; sat::Reader r;
    for(uint32_t off=0; off<COVER_BYTES_MAX; off+=COVER_PART_MAX) {
        const size_t left=COVER_BYTES_MAX-off;
        auto p=part(7,2,COVER_BYTES_MAX,off,left<COVER_PART_MAX?left:COVER_PART_MAX);
        const size_t n=w.frame(COVER_DATA,p.data(),p.size());
        REQUIRE(n>0);
        r.feed(w.wire(),n,[](void* arg,const sat::Frame& f) {
            auto& c=*static_cast<Context*>(arg);
            CHECK(f.type==COVER_DATA);
            CHECK(c.a->append(f.payload,f.len,c.dst->data(),c.dst->size()));
        },&ctx);
    }
    CHECK(a.complete); CHECK(a.used==COVER_BYTES_MAX);
    CHECK(r.bad_crc()==0); CHECK(r.too_big()==0);
}

TEST_CASE("streamed covers commit only after matching length and CRC") {
    std::vector<uint8_t> dst(COVER_BYTES_MAX);
    sat::CoverAssembly a; a.reset(7,2);
    uint32_t crc=0;
    for(uint32_t off=0;off<COVER_BYTES_MAX;) {
        const size_t n=std::min<size_t>(501,COVER_BYTES_MAX-off);
        auto p=part(7,2,0,off,n);
        REQUIRE(a.append(p.data(),p.size(),dst.data(),dst.size(),true));
        CHECK_FALSE(a.complete);
        crc=cover_crc32(crc,p.data()+16,n); off+=(uint32_t)n;
    }
    auto end=part(7,2,COVER_BYTES_MAX,crc,0);
    REQUIRE(a.finish(end.data(),end.size()));
    CHECK(a.complete); CHECK(a.total==COVER_BYTES_MAX);
    CHECK_FALSE(a.finish(end.data(),end.size()));
    CHECK(cover_crc32(0,(const uint8_t*)"123456789",9)==0xcbf43926u);
}
TEST_CASE("streamed cover rejects corruption loss overflow and mixed modes") {
    std::vector<uint8_t> dst(COVER_BYTES_MAX);
    for(unsigned fault=0;fault<9;++fault) {
        sat::CoverAssembly a; a.reset(7,2);
        auto p=part(7,2,0,0,501);
        REQUIRE(a.append(p.data(),p.size(),dst.data(),dst.size(),true));
        if (fault<4) {
            auto end=part(7,2,501,cover_crc32(0,p.data()+16,501),0);
            if(fault==0) end[8]^=1;
            if(fault==1) end[12]^=1;
            if(fault==2) end.pop_back();
            if(fault==3) end.push_back(0);
            CHECK_FALSE(a.finish(end.data(),end.size()));
        } else {
            auto bad=part(7,2,0,501,1);
            if(fault==4) bad=part(7,2,0,0,501); // duplicate
            if(fault==5) bad=part(7,2,0,502,1); // gap
            if(fault==6) bad=part(7,2,502,501,1); // legacy mid-stream
            if(fault==7) bad=part(7,2,0,501,1001);
            if(fault==8) bad=part(7,2,0,501,0);
            CHECK_FALSE(a.append(bad.data(),bad.size(),dst.data(),dst.size(),true));
        }
        CHECK(a.failed); CHECK_FALSE(a.complete);
    }
    sat::CoverAssembly a; a.reset(7,2);
    auto end=part(7,1,12,0,0);
    CHECK_FALSE(a.finish(end.data(),end.size())); CHECK_FALSE(a.failed);
    end=part(7,2,0,0,0);
    CHECK_FALSE(a.finish(end.data(),end.size())); CHECK(a.failed);
    a.reset(7,3);
    auto p=part(7,2,0,0,10);
    CHECK_FALSE(a.append(p.data(),p.size(),dst.data(),dst.size(),true)); CHECK_FALSE(a.failed);
    p=part(7,3,10,0,10); // old sender still works on new host
    REQUIRE(a.append(p.data(),p.size(),dst.data(),dst.size(),true)); CHECK(a.complete);
    a.reset(7,4);
    p=part(7,4,0,0,10);
    CHECK_FALSE(a.append(p.data(),p.size(),dst.data(),9,true)); CHECK(a.failed);
}
