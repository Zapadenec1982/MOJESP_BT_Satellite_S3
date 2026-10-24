/**
 * @file test_user_store.cpp
 * @brief The migration from /data to /userdata, in the four states a panel can be in.
 *
 * The migration runs once, on the first boot after the partition split, and there is no
 * second chance to get it right: what it does not carry over is the EQ someone tuned and the
 * station list they built. It also runs at a moment when power can go — the first boot after
 * a flash or an OTA. So the cases here are exactly the ones a panel meets:
 *
 *   - fresh device        the image has its shipped files, the store is empty
 *   - device with old files  what tools/user_data.py seeded back into /data before the flash
 *   - already migrated    the store has the person's files and must keep them
 *   - interrupted halfway some files copied, no marker, leftover candidates on disk
 *
 * The directories are ordinary directories under the host build tree, which is enough:
 * UserStore::migrate() takes them as arguments precisely so this is testable without a
 * partition.
 */

#include "doctest.h"

#include "user_store.h"
#include "safe_file.h"

#include <cstdio>
#include <cstring>
#include <string>

#if defined(_WIN32)
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir((p), 0777)
#endif

using modesp::UserStore;

namespace {

const std::string kTmp = MODESP_TEST_TMP;

void put(const std::string& path, const std::string& text) {
    FILE* f = fopen(path.c_str(), "wb");
    REQUIRE(f != nullptr);
    if (!text.empty()) fwrite(text.data(), 1, text.size(), f);
    fclose(f);
}

std::string get(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return "\x01" "absent";   // split: "\x01ab..." would read as one hex escape
    char buf[4096];
    const size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    return std::string(buf, n);
}

bool there(const std::string& path) { return get(path) != "\x01" "absent"; }

/// Two empty directories standing in for the two partitions.
struct Panel {
    std::string image;   ///< /data
    std::string store;   ///< /userdata

    explicit Panel(const char* name) {
        image = kTmp + "/" + name + "_data";
        store = kTmp + "/" + name + "_userdata";
        wipe(image);
        wipe(store);
        MKDIR(image.c_str());
        MKDIR(store.c_str());
    }
    ~Panel() { wipe(image); wipe(store); }

    std::string in_image(const char* f) const { return image + "/" + f; }
    std::string in_store(const char* f) const { return store + "/" + f; }

    static void wipe(const std::string& dir) {
        // Only the names this test suite ever creates, so there is nothing recursive here
        // and nothing outside the build tree can be reached.
        for (const char* const* it = UserStore::FILES; *it; ++it) {
            remove((dir + "/" + *it).c_str());
            remove((dir + "/" + *it + safe_file::SUFFIX).c_str());
            remove((dir + "/" + *it + UserStore::STAGED).c_str());
        }
        remove((dir + "/" + UserStore::MARKER).c_str());
        remove((dir + "/" + UserStore::MARKER + safe_file::SUFFIX).c_str());
        remove(dir.c_str());
    }
};

} // namespace

TEST_CASE("the file list is the runtime-written files and nothing the build owns") {
    int n = 0;
    bool has_eq = false, has_stations = false, has_last = false, has_devices = false;
    bool has_custom = false;
    for (const char* const* it = UserStore::FILES; *it; ++it, ++n) {
        if (!strcmp(*it, "eq.json"))       has_eq = true;
        if (!strcmp(*it, "stations.json")) has_stations = true;
        if (!strcmp(*it, "last_track"))    has_last = true;
        if (!strcmp(*it, "devices.json"))  has_devices = true;
        if (!strcmp(*it, "eq_custom.json")) has_custom = true;   // the DSP page's own set
        // Build output must never be migrated: a flash is supposed to replace it, and a
        // stale board.json carried forward would outlive the board it describes.
        CHECK(strcmp(*it, "board.json") != 0);
        CHECK(strcmp(*it, "bindings.json") != 0);
        CHECK(strcmp(*it, "ui.json") != 0);
    }
    CHECK(n == 5);
    CHECK(has_eq);
    CHECK(has_stations);
    CHECK(has_last);
    CHECK(has_devices);
    CHECK(has_custom);
}

TEST_CASE("fresh device: only what the image ships is carried over, and it is marked done") {
    Panel p("us_fresh");
    // A just-flashed panel: data.bin carries the factory eq.json and nothing else. Nobody
    // has tuned anything, so there is no station list and no resume point yet.
    put(p.in_image("eq.json"), "{\"version\":2,\"outputs\":[]}");

    const auto r = UserStore::migrate(p.image.c_str(), p.store.c_str());

    CHECK(r.copied == 1);
    CHECK(r.kept == 0);
    CHECK(r.absent == 4);
    CHECK(r.failed == 0);
    CHECK(r.marked);
    CHECK(get(p.in_store("eq.json")) == "{\"version\":2,\"outputs\":[]}");
    CHECK_FALSE(there(p.in_store("stations.json")));
    CHECK(UserStore::marker_version(p.store.c_str()) == UserStore::SCHEMA);
}

TEST_CASE("device with old files: everything the person made moves, the image keeps its copy") {
    Panel p("us_old");
    // What the customer's panel looks like after `user_data.py backup` + `seed` + a full
    // flash: their own files are in the image, because that is the only way they could
    // survive the flash at all.
    put(p.in_image("eq.json"), "{\"version\":2,\"bands\":\"two active\"}");
    put(p.in_image("stations.json"), "{\"stations\":[1,2,3,4,5,6]}");
    put(p.in_image("last_track"), "Albums/Bach/01.flac");
    put(p.in_image("devices.json"), "{\"manifest_version\":1,\"devices\":[]}");
    put(p.in_image("eq_custom.json"), "{\"version\":2,\"bands\":\"their own\"}");

    const auto r = UserStore::migrate(p.image.c_str(), p.store.c_str());

    CHECK(r.copied == 5);
    CHECK(r.absent == 0);
    CHECK(r.failed == 0);
    CHECK(r.marked);
    CHECK(get(p.in_store("eq.json")) == "{\"version\":2,\"bands\":\"two active\"}");
    CHECK(get(p.in_store("stations.json")) == "{\"stations\":[1,2,3,4,5,6]}");
    CHECK(get(p.in_store("last_track")) == "Albums/Bach/01.flac");
    CHECK(get(p.in_store("devices.json")) == "{\"manifest_version\":1,\"devices\":[]}");
    CHECK(get(p.in_store("eq_custom.json")) == "{\"version\":2,\"bands\":\"their own\"}");

    // Nothing is deleted from the source. The next flash overwrites the image anyway, and
    // deleting would make an interrupted run unrepeatable.
    CHECK(get(p.in_image("eq.json")) == "{\"version\":2,\"bands\":\"two active\"}");
}

TEST_CASE("already migrated: the store's copy wins over the image's, every time") {
    Panel p("us_done");
    put(p.in_image("eq.json"), "the stale snapshot the flash put back");
    put(p.in_store("eq.json"), "the EQ they have been editing since");

    const auto first = UserStore::migrate(p.image.c_str(), p.store.c_str());
    CHECK(first.copied == 0);
    CHECK(first.kept == 1);
    CHECK(get(p.in_store("eq.json")) == "the EQ they have been editing since");

    // And again, and again: the operation is idempotent, which is what lets the marker be
    // advisory rather than load-bearing.
    for (int i = 0; i < 3; i++) {
        const auto again = UserStore::migrate(p.image.c_str(), p.store.c_str());
        CHECK(again.copied == 0);
        CHECK(again.kept == 1);
        CHECK(again.failed == 0);
        CHECK(get(p.in_store("eq.json")) == "the EQ they have been editing since");
    }
}

TEST_CASE("interrupted halfway: the rest is picked up, what arrived is not touched again") {
    Panel p("us_interrupted");
    put(p.in_image("eq.json"), "EQ");
    put(p.in_image("stations.json"), "STATIONS");
    put(p.in_image("last_track"), "TRACK");

    // The state a power cut during the first boot leaves: eq.json made it across, the
    // station list was being written when the power went, nothing is marked.
    put(p.in_store("eq.json"), "EQ");
    put(p.in_store(("stations.json" + std::string(safe_file::SUFFIX)).c_str()), "STATI");
    CHECK(UserStore::marker_version(p.store.c_str()) == -1);

    const auto r = UserStore::migrate(p.image.c_str(), p.store.c_str());

    CHECK(r.kept == 1);        // eq.json was already there
    CHECK(r.copied == 2);      // stations.json and last_track finished the journey
    CHECK(r.failed == 0);
    CHECK(r.marked);
    CHECK(get(p.in_store("stations.json")) == "STATIONS");
    CHECK(get(p.in_store("last_track")) == "TRACK");
    // The half-written candidate is gone, not left to be mistaken for a file later.
    CHECK_FALSE(there(p.in_store("stations.json") + safe_file::SUFFIX));
}

TEST_CASE("a marker that is not a number is treated as no marker, and the run repeats safely") {
    Panel p("us_bad_marker");
    put(p.in_image("eq.json"), "EQ");
    put(p.store + "/" + UserStore::MARKER, "corrupted");

    CHECK(UserStore::marker_version(p.store.c_str()) == -1);
    const auto r = UserStore::migrate(p.image.c_str(), p.store.c_str());
    CHECK(r.copied == 1);
    CHECK(r.marked);
    CHECK(UserStore::marker_version(p.store.c_str()) == UserStore::SCHEMA);
}

TEST_CASE("an empty marker file reads as no marker rather than as version zero") {
    Panel p("us_empty_marker");
    put(p.store + "/" + UserStore::MARKER, "");
    CHECK(UserStore::marker_version(p.store.c_str()) == -1);
}

TEST_CASE("a marker from a later schema is reported as written, not clamped") {
    Panel p("us_future_marker");
    put(p.store + "/" + UserStore::MARKER, "99\n");
    // begin() compares with >=, so a store written by a later build is left alone instead of
    // being migrated into with today's rules.
    CHECK(UserStore::marker_version(p.store.c_str()) == 99);
    CHECK(UserStore::marker_version(p.store.c_str()) >= UserStore::SCHEMA);
}

TEST_CASE("a migration that cannot write anywhere leaves no marker, so it runs again") {
    Panel p("us_nowhere");
    put(p.in_image("eq.json"), "EQ");
    // A destination directory that does not exist stands in for a store that would not
    // mount: every copy fails, and the point is that the failure is not recorded as success.
    const std::string missing = kTmp + "/us_nowhere_absent_dir";
    const auto r = UserStore::migrate(p.image.c_str(), missing.c_str());
    CHECK(r.copied == 0);
    CHECK(r.failed >= 1);
    CHECK_FALSE(r.marked);
    CHECK(UserStore::marker_version(missing.c_str()) == -1);
}

TEST_CASE("with no store, reads and writes both go to the image partition") {
    // This is a panel that took an over-the-air app update while still on the old partition
    // table: there is no userdata partition, and nothing may break because of it.
    char buf[160];
    CHECK(std::string(UserStore::read_path_in(nullptr, "/data", "eq.json", buf, sizeof(buf)))
          == "/data/eq.json");
    CHECK(std::string(UserStore::write_path_in(nullptr, "/data", "eq.json", buf, sizeof(buf)))
          == "/data/eq.json");
}

TEST_CASE("with a store, a write always goes to it and a read falls back to the image") {
    Panel p("us_paths");
    char buf[512];

    // Nothing in the store yet: a read must find the shipped copy, which is how a fresh
    // panel comes up with the factory EQ instead of a flat one.
    put(p.in_image("eq.json"), "shipped");
    CHECK(std::string(UserStore::read_path_in(p.store.c_str(), p.image.c_str(),
                                              "eq.json", buf, sizeof(buf)))
          == p.in_image("eq.json"));

    // A write goes to the store regardless — that is the whole point of the split.
    CHECK(std::string(UserStore::write_path_in(p.store.c_str(), p.image.c_str(),
                                               "eq.json", buf, sizeof(buf)))
          == p.in_store("eq.json"));

    // Once it is there, the read follows it.
    put(p.in_store("eq.json"), "theirs");
    CHECK(std::string(UserStore::read_path_in(p.store.c_str(), p.image.c_str(),
                                              "eq.json", buf, sizeof(buf)))
          == p.in_store("eq.json"));
}

TEST_CASE("a buffer too small for the path yields an empty string, not a truncated one") {
    char buf[8];
    const char* got = UserStore::write_path_in("/userdata", "/data", "stations.json",
                                               buf, sizeof(buf));
    CHECK(std::string(got).empty());
}

TEST_CASE("a restore from a backup goes in at boot, and only for the files a person makes") {
    Panel p("us_staged");
    put(p.in_store("eq.json"), "{\"in use\":1}");
    put(p.in_store("stations.json"), "{\"in use\":2}");
    // what POST /api/userdata left before the restart (A6)
    put(p.in_store("eq.json") + UserStore::STAGED, "{\"from the backup\":1}");
    put(p.in_store("last_track") + UserStore::STAGED, "Albums/Bach/02.flac");

    CHECK(UserStore::promote_staged(p.store.c_str()) == 2);
    CHECK(get(p.in_store("eq.json")) == "{\"from the backup\":1}");
    CHECK(get(p.in_store("last_track")) == "Albums/Bach/02.flac");   // there was none: now there is
    CHECK(get(p.in_store("stations.json")) == "{\"in use\":2}");     // not in the backup: left alone
    CHECK_FALSE(there(p.in_store("eq.json") + UserStore::STAGED));
    CHECK_FALSE(there(p.in_store("last_track") + UserStore::STAGED));

    // nothing staged: nothing moves, a second boot changes nothing
    CHECK(UserStore::promote_staged(p.store.c_str()) == 0);
    CHECK(get(p.in_store("eq.json")) == "{\"from the backup\":1}");
}
