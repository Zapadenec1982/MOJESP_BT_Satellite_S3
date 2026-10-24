#include "doctest.h"
#include "modesp/net/ws_slots.h"

#include <cstdint>

using modesp::net::WsAdmit;
using modesp::net::WsSlot;
using modesp::net::ws_admit;
using modesp::net::ws_should_close;
using modesp::net::ws_silent_ms;

namespace {
constexpr uint32_t STALE = 15000;
constexpr size_t N = 3;
}

TEST_CASE("ws_admit gives a new client the first free slot") {
    WsSlot s[N];
    s[0] = {7, 1000, false};
    auto a = ws_admit(s, N, 9, 2000, STALE);
    CHECK(a.action == WsAdmit::Free);
    CHECK(a.slot == 1);
}

TEST_CASE("ws_admit finds a reused socket number in its own slot") {
    WsSlot s[N] = {{7, 0, false}, {8, 0, false}, {9, 0, false}};
    auto a = ws_admit(s, N, 8, 100, STALE);
    CHECK(a.action == WsAdmit::Tracked);
    CHECK(a.slot == 1);
}

TEST_CASE("ws_admit refuses a fourth client while all three were heard within the threshold") {
    const uint32_t now = 100000;
    WsSlot s[N] = {{7, now - 1000, false}, {8, now - STALE, false}, {9, now, false}};
    auto a = ws_admit(s, N, 10, now, STALE);
    CHECK(a.action == WsAdmit::Reject);
    CHECK(a.slot == -1);
}

TEST_CASE("ws_admit gives the slot of the longest silent client once it is past the threshold") {
    const uint32_t now = 100000;
    WsSlot s[N] = {{7, now - STALE - 1, false}, {8, now - 60000, false}, {9, now - 500, false}};
    auto a = ws_admit(s, N, 10, now, STALE);
    CHECK(a.action == WsAdmit::Evict);
    CHECK(a.slot == 1);
}

TEST_CASE("ws_admit takes a client out of step before one that is merely silent") {
    const uint32_t now = 100000;
    WsSlot s[N] = {{7, now - 60000, false}, {8, now, true}, {9, now, false}};
    auto a = ws_admit(s, N, 10, now, STALE);
    CHECK(a.action == WsAdmit::Evict);
    CHECK(a.slot == 1);
}

TEST_CASE("ws_admit refuses a socket that is not one") {
    WsSlot s[N];
    CHECK(ws_admit(s, N, -1, 0, STALE).action == WsAdmit::Reject);
}

TEST_CASE("ws_should_close: silent past the threshold, or out of step; never a free slot") {
    const uint32_t now = 50000;
    CHECK_FALSE(ws_should_close(WsSlot{}, now, STALE));
    CHECK_FALSE(ws_should_close(WsSlot{-1, 0, true}, now, STALE));
    CHECK_FALSE(ws_should_close(WsSlot{7, now - STALE, false}, now, STALE));
    CHECK(ws_should_close(WsSlot{7, now - STALE - 1, false}, now, STALE));
    CHECK(ws_should_close(WsSlot{7, now, true}, now, STALE));
}

TEST_CASE("silence is measured across the 32-bit millisecond wrap") {
    const WsSlot s{7, UINT32_MAX - 999, false};  // heard 1 s before the clock wraps
    CHECK(ws_silent_ms(s, 500) == 1500);
    CHECK_FALSE(ws_should_close(s, 500, STALE));
    CHECK(ws_should_close(s, STALE, STALE));
}
