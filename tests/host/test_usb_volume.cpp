/**
 * @file test_usb_volume.cpp
 * @brief HOST tests for how the computer's volume slider moves the room
 *        (modules/usb_audio/include/usb_volume.h): the room keeps its level when a stream starts,
 *        the slider means nothing while held or while USB is not the source, it then maps through
 *        the room's level to the panel's top and to silence, and the knob re-references it.
 */

#include "doctest.h"
#include "../../modules/usb_audio/include/usb_volume.h"

using namespace usb_volume;

TEST_CASE("usb_volume: the slider's position spreads -80..0 dB over 0..100") {
    CHECK(position(MIN_256) == 0);
    CHECK(position(MIN_256 - 256) == 0);
    CHECK(position(MAX_256) == 100);
    CHECK(position(-40 * 256) == 50);
    CHECK(position(-20 * 256) == 75);
    CHECK(position(-79 * 256) == 1);
}

TEST_CASE("usb_volume: the room keeps its level when the computer starts at full volume") {
    Follow f;
    f.session_start(30, 100);
    CHECK(f.on_level(100, 30, true) == -1);   // within the hold: a restated level, not a press
    f.tick(Follow::HOLD_MS);
    CHECK(f.on_level(100, 30, true) == 30);   // unchanged slider, unchanged room
    CHECK(f.on_level(50, 30, true) == 15);    // half way down is half the room's level
    CHECK(f.on_level(0, 30, true) == 0);      // the bottom is silence
}

TEST_CASE("usb_volume: up from a lowered slider reaches the panel's top at the slider's top") {
    Follow f;
    f.session_start(40, 60);
    f.tick(Follow::HOLD_MS);
    CHECK(f.on_level(60, 40, true) == 40);
    CHECK(f.on_level(80, 40, true) == 70);    // half of the span above the reference
    CHECK(f.on_level(100, 40, true) == 100);
}

TEST_CASE("usb_volume: while USB is not the source the slider only moves the reference") {
    Follow f;
    f.session_start(30, 100);
    f.tick(Follow::HOLD_MS);
    CHECK(f.on_level(50, 30, false) == -1);
    CHECK(f.on_level(50, 30, true) == 30);    // the reference moved with it: no jump on return
}

TEST_CASE("usb_volume: the knob moves the room, and the slider counts from there") {
    Follow f;
    f.session_start(30, 80);
    f.tick(Follow::HOLD_MS);
    CHECK(f.on_level(80, 30, true) == 30);
    f.room_moved(50);                          // turned up on the panel
    CHECK(f.on_level(80, 50, true) == 50);    // the slider where it was: the knob's level stands
    CHECK(f.on_level(40, 50, true) == 25);
}
