/**
 * @file test_module_commands.cpp
 * @brief BaseModule's command queue — the thing that replaced player.cmd / radio.cmd.
 *
 * A command is an event. It used to travel as a SharedState key: the sender wrote
 * player.cmd = 4, the module polled it on its next turn and wrote 0 back. Three things were
 * wrong with that, and each of them is a test here:
 *
 *   - two presses between two polls were ONE press, because the second write overwrote the
 *     first before anybody read it;
 *   - the reset to zero was a second state change, so one button push went out over the
 *     WebSocket and MQTT twice;
 *   - the key sat in the store for ever, occupying a slot and appearing in GET /api/state as
 *     a number with no meaning.
 *
 * The routing is generated: which keys are commands, and which module owns each one, comes
 * from `"command": true` in the manifests via gen::STATE_COMMANDS. A module that is handed
 * somebody else's command must refuse it.
 */

#include "doctest.h"

#include "mocks/freertos_mock.h"
#include "mocks/esp_log_mock.h"

#include "modesp/base_module.h"
#include "modesp/shared_state.h"
#include "state_meta.h"

#include <cstring>
#include <vector>

using namespace modesp;

namespace {

/// A module that records what it drains, so a test can see the ORDER as well as the count.
class Recorder : public BaseModule {
public:
    explicit Recorder(const char* name) : BaseModule(name) {}

    /// next_command() is protected — a module drains its own queue. Exposed here so a test
    /// can watch one command at a time.
    using BaseModule::next_command;

    std::vector<std::pair<std::string, int32_t>> drain() {
        std::vector<std::pair<std::string, int32_t>> out;
        Command c;
        while (next_command(c)) out.emplace_back(c.key ? c.key : "", c.value);
        return out;
    }
};

/// The key/module pairs the manifests actually declare, so these tests fail loudly if a
/// command key is renamed or moved to another module without the senders being updated.
const gen::StateCommand* command_for(const char* key) {
    return gen::find_state_command(key);
}

}  // namespace

TEST_CASE("the manifests declare the command keys and who owns them") {
    struct { const char* key; const char* module; } expected[] = {
        {"player.cmd",      "player"},
        {"radio.cmd",       "radio"},
        {"dlna.cmd",        "dlna"},
        {"dlna.browse_cmd", "dlna"},
    };
    for (const auto& e : expected) {
        INFO("key: ", e.key);
        const auto* c = command_for(e.key);
        REQUIRE(c != nullptr);
        CHECK(std::string(c->module) == e.module);
    }
    // An ordinary value must NOT be routed to a queue, or it would never be stored.
    CHECK(command_for("audio.volume") == nullptr);
    CHECK(command_for("player.track") == nullptr);
    CHECK(command_for("radio.station") == nullptr);
}

TEST_CASE("a burst of presses is a burst of commands") {
    // The whole point. As a state key, these four writes left one value behind.
    Recorder player("player");
    for (int32_t v : {4, 4, 5, 4}) CHECK(player.post_command("player.cmd", v));

    auto got = player.drain();
    REQUIRE(got.size() == 4);
    CHECK(got[0].second == 4);
    CHECK(got[1].second == 4);
    CHECK(got[2].second == 5);
    CHECK(got[3].second == 4);
    CHECK(player.drain().empty());
}

TEST_CASE("the key travels with the command") {
    // dlna has two command keys on one queue: a transport press and a browse token must not
    // be confused for each other.
    Recorder dlna("dlna");
    REQUIRE(dlna.post_command("dlna.browse_cmd", 12345));
    REQUIRE(dlna.post_command("dlna.cmd", 3));

    auto got = dlna.drain();
    REQUIRE(got.size() == 2);
    CHECK(got[0].first == "dlna.browse_cmd");
    CHECK(got[0].second == 12345);
    CHECK(got[1].first == "dlna.cmd");
    CHECK(got[1].second == 3);
}

TEST_CASE("the queued key is the table's literal, not the caller's buffer") {
    // The HTTP handler passes a pointer into the request body, which is gone by the time
    // on_update() runs. post_command() resolves it to the generated table first.
    Recorder player("player");
    char scratch[32];
    strcpy(scratch, "player.cmd");
    REQUIRE(player.post_command(scratch, 6));
    memset(scratch, 'X', sizeof scratch);      // the request buffer is reused

    auto got = player.drain();
    REQUIRE(got.size() == 1);
    CHECK(got[0].first == "player.cmd");
    CHECK(got[0].second == 6);
}

TEST_CASE("a module refuses a command that is not its own") {
    Recorder player("player");
    CHECK_FALSE(player.post_command("radio.cmd", 1));      // belongs to the radio
    CHECK_FALSE(player.post_command("audio.volume", 30));  // not a command at all
    CHECK_FALSE(player.post_command("nonsense", 1));
    CHECK(player.pending_commands() == 0);
}

TEST_CASE("a full queue refuses rather than overwriting") {
    // Eight is more than a person can press inside one tick. A control point that floods
    // gets a false back, and the caller logs it — it is not dropped in silence.
    Recorder player("player");
    for (size_t i = 0; i < BaseModule::COMMAND_QUEUE_DEPTH; i++)
        CHECK(player.post_command("player.cmd", (int32_t)i));
    CHECK(player.pending_commands() == BaseModule::COMMAND_QUEUE_DEPTH);
    CHECK_FALSE(player.post_command("player.cmd", 99));

    // And what IS in the queue is the oldest eight, unchanged: refusing must not corrupt.
    auto got = player.drain();
    REQUIRE(got.size() == BaseModule::COMMAND_QUEUE_DEPTH);
    for (size_t i = 0; i < got.size(); i++) CHECK(got[i].second == (int32_t)i);
}

TEST_CASE("draining an empty queue is a no-op") {
    Recorder player("player");
    BaseModule::Command c;
    CHECK_FALSE(player.next_command(c));
    CHECK(player.pending_commands() == 0);
}

TEST_CASE("a command never reaches the state store") {
    // The reason player.cmd stopped being written: a value that means something for one tick
    // was being broadcast to every WebSocket client twice, as "4" and then as "0".
    SharedState st;
    Recorder player("player");
    REQUIRE(player.post_command("player.cmd", 4));
    CHECK_FALSE(st.has("player.cmd"));
    CHECK(st.size() == 0);
    CHECK(st.version() == 0);
}
