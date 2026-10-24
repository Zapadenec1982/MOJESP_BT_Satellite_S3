#include "doctest.h"
#include "../../modules/player/include/pcm_producer.h"

TEST_CASE("SD commands: a full queue cannot cancel a track with an unaccepted generation") {
    std::atomic<uint32_t> requested{7};
    CHECK_FALSE(player_core::queue_pcm_command(requested, true, [&](uint32_t next) {
        CHECK(next == 8);
        CHECK_FALSE(player_core::command_cancelled(requested,7));
        return false;                     // queue full: no message will ever carry next
    }));
    CHECK(requested.load() == 7);
    CHECK_FALSE(player_core::command_cancelled(requested,7));
    CHECK(player_core::queue_pcm_command(requested, true, [&](uint32_t next) {
        CHECK_FALSE(player_core::command_cancelled(requested,7));
        CHECK_FALSE(player_core::command_cancelled(requested,next)); // receiver ran early
        return true;
    }));
    CHECK(player_core::command_cancelled(requested,7));
    CHECK_FALSE(player_core::command_cancelled(requested,8));
}

TEST_CASE("SD commands: pause/resume do not discard a decoded block") {
    std::atomic<uint32_t> requested{12};
    CHECK(player_core::queue_pcm_command(requested, false, [&](uint32_t next) {
        CHECK(next == 12);
        CHECK_FALSE(player_core::command_cancelled(requested,12));
        return true;
    }));
    CHECK_FALSE(player_core::command_cancelled(requested,12));
}

TEST_CASE("SD producer: ownership lost while full cancels a partial write") {
    audio_core::PcmRing r;
    REQUIRE(r.init(2));
    r.claim(audio_core::PcmRing::OWNER_FILE);
    float frames[8]{};
    unsigned waits = 0;
    const bool done = player_core::put_pcm(r, audio_core::PcmRing::OWNER_FILE, r.generation(),
        frames, 4, [] { return false; }, [&] {
            ++waits;
            r.claim(audio_core::PcmRing::OWNER_RADIO);
        });
    CHECK_FALSE(done);
    CHECK(waits == 1);
    CHECK(r.owner() == audio_core::PcmRing::OWNER_RADIO);
    CHECK(r.available() == 0);
    r.deinit();
}

TEST_CASE("SD producer: STOP on a full ring returns control to the command loop") {
    audio_core::PcmRing r;
    REQUIRE(r.init(2));
    r.claim(audio_core::PcmRing::OWNER_FILE);
    const auto gen = r.generation();
    float frames[8]{};
    unsigned waits = 0;
    bool stopped = false;
    CHECK_FALSE(player_core::put_pcm(r, audio_core::PcmRing::OWNER_FILE, gen, frames, 4,
        [&] { return stopped; }, [&] { ++waits; stopped = true; }));
    CHECK(waits == 1);
    // A late STOP from this generation must not flush the radio's newly queued frames.
    r.claim(audio_core::PcmRing::OWNER_RADIO);
    REQUIRE(r.write(audio_core::PcmRing::OWNER_RADIO, frames, 2) == 2);
    CHECK_FALSE(r.request_flush(audio_core::PcmRing::OWNER_FILE, gen));
    CHECK_FALSE(r.reset(audio_core::PcmRing::OWNER_FILE, gen));
    CHECK_FALSE(r.reset(gen));
    CHECK(r.available() == 2);
    r.deinit();
}

TEST_CASE("SD producer: a reclaimed FILE ring rejects the previous decoded session") {
    audio_core::PcmRing r;
    REQUIRE(r.init(2));
    r.claim(audio_core::PcmRing::OWNER_FILE);
    const auto gen = r.generation();
    float frames[8]{};
    unsigned waits = 0;
    CHECK_FALSE(player_core::put_pcm(r, audio_core::PcmRing::OWNER_FILE, gen, frames, 4,
        [] { return false; }, [&] {
            ++waits;
            r.claim(audio_core::PcmRing::OWNER_RADIO);
            r.claim(audio_core::PcmRing::OWNER_FILE);
        }));
    CHECK(waits == 1);
    CHECK(r.available() == 0);
    r.deinit();
}

TEST_CASE("SD producer: normal backpressure preserves every frame") {
    audio_core::PcmRing r;
    REQUIRE(r.init(2));
    r.claim(audio_core::PcmRing::OWNER_FILE);
    float frames[] = {1,-1,2,-2,3,-3,4,-4}, heard[8]{};
    size_t consumed = 0;
    CHECK(player_core::put_pcm(r, audio_core::PcmRing::OWNER_FILE, r.generation(), frames, 4,
        [] { return false; }, [&] { consumed += r.read(heard + consumed*2, 2); }));
    consumed += r.read(heard + consumed*2, 2);
    CHECK(consumed == 4);
    for (unsigned i=0; i<8; ++i) CHECK(heard[i] == frames[i]);
    r.deinit();
}
