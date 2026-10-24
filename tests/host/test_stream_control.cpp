#include "doctest.h"
#include "sat_link/stream_control.h"
#include "format_sync_cases.h"
#include <thread>
#include <cstring>
#include "sat_link/sat_link.h"
TEST_CASE("Format protocol vectors and transmitter safety") { verify_format_sync(); }
TEST_CASE("Preparation is idempotent and only the audio task can complete it") {
    sat::StreamControl box;
    fmt_request_t f{1,1,44100,16,2};
    CHECK(box.publish(f)); sat::StreamRequest a;
    REQUIRE(box.snapshot(a)); uint8_t status;
    CHECK_FALSE(box.result(a.revision,status)); // inactive BT: no preparation yet
    box.complete(a.revision,true);
    CHECK(box.publish(f)); sat::StreamRequest duplicate;
    REQUIRE(box.snapshot(duplicate)); CHECK(duplicate.revision==a.revision);
    CHECK(box.result(a.revision,status)); CHECK(status==FMT_OK);
    f.bits=32; CHECK_FALSE(box.publish(f)); // same ID cannot name a different format
    f.generation++; CHECK(box.publish(f));
    REQUIRE(box.snapshot(duplicate)); CHECK(duplicate.revision!=a.revision);
    CHECK_FALSE(box.result(duplicate.revision,status));
    box.complete(a.revision,true); CHECK_FALSE(box.result(duplicate.revision,status));
    box.complete(duplicate.revision,false); CHECK(box.result(duplicate.revision,status));
    CHECK(status==FMT_REFUSED);
    --f.generation; CHECK_FALSE(box.publish(f)); // stale request
    box.clear(); CHECK(box.publish(f)); // same-format reconnect must be re-prepared
    REQUIRE(box.snapshot(a)); CHECK_FALSE(box.result(a.revision,status));
    box.complete(a.revision,true); box.invalidate_result();
    CHECK_FALSE(box.result(a.revision,status)); // switching away from BT
    f.nonce++; f.generation=1; CHECK(box.publish(f)); // reboot
}
TEST_CASE("Request publication cannot tear format or transaction identity") {
    sat::StreamControl box; std::atomic<bool> done{false};
    std::thread writer([&] {
        for(uint32_t i=1;i<=100000;++i) {
            const bool odd=(i&1u)!=0;
            box.publish({7,i,odd?44100u:48000u,(uint8_t)(odd?16:32),2});
        }
        done.store(true);
    });
    bool coherent=true;
    do {
        sat::StreamRequest r;
        if(!box.snapshot(r) || !r.format.nonce) continue;
        const bool odd=(r.format.generation&1u)!=0;
        coherent &= r.format.nonce==7 && r.format.channels==2 &&
                    r.format.rate_hz==(odd?44100u:48000u) && r.format.bits==(odd?16:32);
    } while(!done.load());
    writer.join(); CHECK(coherent);
}

TEST_CASE("Format frames match the satellite wire fixtures") {
    const fmt_request_t request = {0x12345678u,0x01020304u,48000,32,2};
    const fmt_ready_t ready = {request.nonce,request.generation,FMT_OK};
    uint8_t prepare[14], reply[9];
    fmt_encode(prepare,&request); fmt_encode_ready(reply,&ready);
    static const uint8_t prepare_wire[] = {0x02, 0x16, 0x0b, 0x78, 0x56, 0x34, 0x12, 0x04, 0x03, 0x02, 0x01, 0x80, 0xbb, 0x01, 0x05, 0x20, 0x02, 0x01, 0xf7, 0x00};
    static const uint8_t ready_wire[] = {0x02, 0x23, 0x09, 0x78, 0x56, 0x34, 0x12, 0x04, 0x03, 0x02, 0x01, 0x01, 0x02, 0x89, 0x00};
    sat::Writer prepare_writer, ready_writer;
    CHECK(prepare_writer.frame(FMT_PREPARE,prepare,sizeof prepare)==sizeof prepare_wire+1);
    CHECK(prepare_writer.wire()[0]==0);
    CHECK(std::memcmp(prepare_writer.wire()+1,prepare_wire,sizeof prepare_wire)==0);
    CHECK(ready_writer.frame(FMT_READY,reply,sizeof reply)==sizeof ready_wire+1);
    CHECK(ready_writer.wire()[0]==0);
    CHECK(std::memcmp(ready_writer.wire()+1,ready_wire,sizeof ready_wire)==0);
}
