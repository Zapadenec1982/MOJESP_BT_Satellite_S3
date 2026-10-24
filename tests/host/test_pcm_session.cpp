#include "doctest.h"
#include "pcm_ring.h"

TEST_CASE("PCM stale producer cannot change EOS after an ownership round trip") {
    audio_core::PcmRing ring;
    REQUIRE(ring.init(16));
    ring.claim(audio_core::PcmRing::OWNER_FILE);
    const auto first = ring.generation();
    CHECK(ring.set_eos(audio_core::PcmRing::OWNER_FILE, first, true));
    ring.claim(audio_core::PcmRing::OWNER_RADIO);
    CHECK_FALSE(ring.eos());
    CHECK_FALSE(ring.set_eos(audio_core::PcmRing::OWNER_FILE, first, true));
    ring.claim(audio_core::PcmRing::OWNER_FILE);
    CHECK(ring.set_eos(audio_core::PcmRing::OWNER_FILE, ring.generation(), true));
    CHECK_FALSE(ring.set_eos(audio_core::PcmRing::OWNER_FILE, first, false));
    CHECK(ring.eos());
}
