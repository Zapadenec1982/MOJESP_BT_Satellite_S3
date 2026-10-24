#pragma once
#include "dlna_transport.h"
#include <cstdint>
namespace dlna_client {
constexpr int PAGE_SIZE = 12;
enum class Command : uint8_t {
    Discover = 1,
    Open = 2,
    Back = 3,
    NextPage = 4,
    PrevPage = 5,
    Queue = 6,
    Cancel = 7
};
struct Row {
    char title[96]{}, subtitle[96]{};
    bool folder = false, playable = false;
};
struct View {
    uint16_t revision = 0;
    bool busy = false, servers = true, has_next = false, has_prev = false, can_back = false;
    uint8_t count = 0;
    char title[96]{}, status[96]{};
    Row rows[PAGE_SIZE]{};
};
bool init();
// All commands carry the displayed revision; stale taps cannot play a changed row.
int32_t encode(Command command, int row, uint16_t revision);
bool request(int32_t command);
bool snapshot(View &view);
bool take_track(dlna_core::Track &track, bool &next);
void artwork(const char *uri);
void stop();
} // namespace dlna_client
