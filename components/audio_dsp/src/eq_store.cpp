/**
 * @file eq_store.cpp
 * @brief The EQ document and its revision. See eq_store.h.
 */

#include "eq_store.h"

#include "safe_file.h"

#include "esp_heap_caps.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace audio_core {
namespace {

/// The copy kept of a file an older build left, and where a file this build cannot read goes.
constexpr const char* COPY = ".v2";
constexpr const char* ASIDE = ".bad";

bool with_suffix(const char* path, const char* suffix, char* out, size_t n) {
    const int w = snprintf(out, n, "%s%s", path, suffix);
    return w > 0 && (size_t)w < n;
}

/// One document's worth of scratch, static: the main task's stack has little to spare — load()
/// runs on it, and eq_parse below it is already 5.3 KB deep. load() uses this once, before
/// anything can be saved; save() copies the document into it under the lock and writes from it
/// outside.
EqDoc& scratch() {
    static EqDoc d;
    return d;
}

/// Two file names at a time, for the copies: not on the main task's stack (scratch()), and not
/// in internal RAM either, which the Wi-Fi co-processor's buffers and the I2S descriptors come
/// out of — in PSRAM, as the radio keeps its state (radio_module.cpp). Every file here is the
/// main task's, one at a time, so one pair serves keep_copy(), move_aside() and the copies at
/// boot.
struct Paths {
    char a[200];
    char b[200];
};
Paths& paths() {
    static Paths* p = static_cast<Paths*>(heap_caps_calloc_prefer(1, sizeof(Paths), 2,
                                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                                                  MALLOC_CAP_8BIT));
    assert(p);
    return *p;
}

/// The copy kept of a file an older build left (no "preset" in it): made before this build
/// first writes either file — a rollback can rewrite eq_custom.json even where this build
/// never did (load()) — and never written over, so it stays the file as it was before this
/// build. A file this build cannot read is not copied: it is moved aside when written over.
bool keep_copy(const char* path) {
    char* to = paths().a;
    if (!with_suffix(path, COPY, to, sizeof(Paths::a))) return false;
    if (!safe_file::exists(path) || safe_file::exists(to)) return true;
    return eq_copy_file(path, to);
}

/// A file this build could not read, moved aside before it is written over — a rename, and
/// the newest such file is the one kept.
bool move_aside(const char* path) {
    char* to = paths().a;
    if (!with_suffix(path, ASIDE, to, sizeof(Paths::a))) return false;
    if (!safe_file::exists(path)) return true;    // moved by a save that then failed
    if (rename(path, to) == 0) return true;
    remove(to);                                   // a filesystem whose rename will not replace
    return rename(path, to) == 0;
}

bool is_a_sound(const EqSection& s) { return eq_detect_preset(s, s) != EQ_PRESET_CUSTOM; }

}  // namespace

EqStore& eq_store() {
    // About 2 KB: two configurations and a few counters. Internal RAM is fine for that.
    static EqStore s;
    return s;
}

void EqStore::load(const char* eq_path, const char* custom_path) {
    std::lock_guard<std::mutex> lock(mx_);
    if (loaded_) return;
    notes_ = 0;
    if (eq_load(eq_path, doc_.cfg) && doc_.cfg.sections > 0) {
        older_eq_ = !doc_.cfg.preset[0];
    } else {
        // No file, or one this build cannot read: the flat ten-band set, not an empty page.
        // Not dirty — nothing is written until somebody edits it, and then the file it could
        // not read is moved aside first.
        eq_unreadable_ = safe_file::exists(eq_path);
        if (eq_unreadable_) notes_ |= EQ_UNREADABLE;
        eq_default_config(doc_.cfg);
    }
    // «Власний» is read on its own: any number of bands is a set.
    bool own = eq_load(custom_path, doc_.custom) && doc_.custom.sections > 0;
    if (own) {
        older_custom_ = !doc_.custom.preset[0];
    } else {
        custom_unreadable_ = safe_file::exists(custom_path);
        if (custom_unreadable_) notes_ |= CUSTOM_UNREADABLE;
        doc_.custom = doc_.cfg;
    }

    // What plays: the bands decide, and the name only names them. An older build's file has
    // no name, and the bands are looked up as they always were.
    const char* hint = doc_.cfg.preset;
    const int named = hint[0] ? eq_preset_find(hint, strlen(hint)) : -1;
    if (!hint[0]) {
        doc_.preset = eq_detect_preset(doc_.cfg.sec[0], doc_.custom.sec[0]);
    } else if (named > EQ_PRESET_CUSTOM) {
        EqSection& s = scratch().cfg.sec[0];       // not on the main task's stack (scratch())
        eq_build_preset(named, doc_.custom.sec[0], s);
        doc_.preset = eq_same_section(s, doc_.cfg.sec[0]) ? named : EQ_PRESET_CUSTOM;
    } else {
        doc_.preset = EQ_PRESET_CUSTOM;
    }
    // Under «Власний», what plays is «Власний» and the freshest of it — unless the file
    // named a sound: then it is that sound as it was saved, by a build that has its curve,
    // and the set in eq_custom.json stays «Власний» (eq_choose_preset brings it back).
    const bool a_sound = hint[0] && named != EQ_PRESET_CUSTOM;
    if (doc_.preset == EQ_PRESET_CUSTOM && !a_sound) {
        doc_.custom = doc_.cfg;
        own = false;
    }

    // A build that does not have one of today's sounds takes it, playing, for its own set, and
    // writes it to eq_custom.json the first time a preset is chosen there — with no "preset",
    // as that build writes. So a «Власний» from such a file that is exactly one of the
    // table's sounds is nobody's set: the copies kept before this build's first write say
    // what it was (an update, a sound, a rollback, a preset, an update again: tests).
    if (own && !doc_.custom.preset[0] && is_a_sound(doc_.custom.sec[0]) &&
        custom_from_copies_locked(eq_path, custom_path)) {
        notes_ |= CUSTOM_FROM_COPY;
    }
    rev_ = saved_rev_ = 1;
    loaded_ = true;
}

bool EqStore::custom_from_copies_locked(const char* eq_path, const char* custom_path) {
    char* a = paths().a;                          // once, at boot, under the lock
    char* b = paths().b;
    if (!with_suffix(eq_path, COPY, a, sizeof(Paths::a)) || !with_suffix(custom_path, COPY, b, sizeof(Paths::b))) return false;
    // The pair as the older build read it: what played was its own set unless it was a preset.
    EqDoc& old = scratch();
    const bool have_eq = eq_load(a, old.cfg) && old.cfg.sections > 0;
    const bool have_custom = eq_load(b, old.custom) && old.custom.sections > 0;
    const EqConfig* set = nullptr;
    if (have_eq && (!have_custom || eq_detect_preset(old.cfg.sec[0], old.custom.sec[0]) == EQ_PRESET_CUSTOM)) {
        set = &old.cfg;
    } else if (have_custom) {
        set = &old.custom;
    }
    if (!set || is_a_sound(set->sec[0])) return false;
    doc_.custom = *set;
    custom_dirty_ = true;                      // written back, marked, with the next save
    return true;
}

bool EqStore::loaded() const {
    std::lock_guard<std::mutex> lock(mx_);
    return loaded_;
}

uint8_t EqStore::load_notes() const {
    std::lock_guard<std::mutex> lock(mx_);
    return notes_;
}

uint32_t EqStore::revision() const {
    std::lock_guard<std::mutex> lock(mx_);
    return rev_;
}

uint32_t EqStore::get(EqDoc& out) const {
    std::lock_guard<std::mutex> lock(mx_);
    out = doc_;
    return rev_;
}

int EqStore::preset() const {
    std::lock_guard<std::mutex> lock(mx_);
    return doc_.preset;
}

// No copy of the document is taken to compare against: put() is called from the main task,
// whose stack has little to spare, and a document is over a kilobyte. Whether «Власний»
// moved is known before the change instead.
uint32_t EqStore::bump_locked(bool custom_moved) {
    if (custom_moved) custom_dirty_ = true;
    if (++rev_ == 0) rev_ = 1;                 // 0 means "refused"
    return rev_;
}

uint32_t EqStore::put(const EqDoc& doc) {
    std::lock_guard<std::mutex> lock(mx_);
    // eq_custom.json is written when «Власний» stops playing, or changes while it does not.
    // While it plays, eq.json holds it: one file to write after an edit, not two — a LittleFS
    // write is heard (modules/audio).
    const bool moved = doc_.preset == EQ_PRESET_CUSTOM
                           ? doc.preset != EQ_PRESET_CUSTOM
                           : !eq_same_section(doc_.custom.sec[0], doc.custom.sec[0]);
    doc_ = doc;
    loaded_ = true;
    return bump_locked(moved);
}

uint32_t EqStore::put_bands(const EqBand* bands, int count, uint32_t base) {
    std::lock_guard<std::mutex> lock(mx_);
    if (base != rev_ || !bands || count < 0 || count > MAX_BANDS) return 0;
    const bool moved = doc_.preset != EQ_PRESET_CUSTOM;   // a preset edited: written (put())
    EqSection& sec = doc_.cfg.sec[0];
    for (int i = 0; i < count; i++) sec.bands[i] = bands[i];
    sec.count = count;
    if (doc_.cfg.sections == 0) doc_.cfg.sections = 1;
    eq_hand_edited(doc_.cfg, doc_.custom, doc_.preset);   // the result is «Власний»
    return bump_locked(moved);
}

uint32_t EqStore::choose(int p, uint32_t base) {
    std::lock_guard<std::mutex> lock(mx_);
    if (base != rev_ || p < 0 || p >= eq_preset_count()) return 0;
    // Leaving «Власний» writes it (put()).
    const bool moved = doc_.preset == EQ_PRESET_CUSTOM;
    if (!eq_choose_preset(doc_.cfg, doc_.custom, doc_.preset, p)) return rev_;
    return bump_locked(moved);
}

bool EqStore::dirty() const {
    std::lock_guard<std::mutex> lock(mx_);
    return loaded_ && rev_ != saved_rev_;
}

bool EqStore::save(const char* eq_path, const char* custom_path) {
    // Copied out under the lock, written outside it: a flash write takes tens of milliseconds,
    // and the screen's task must not wait that long for an edit.
    EqDoc& doc = scratch();
    bool custom;
    {
        std::lock_guard<std::mutex> lock(mx_);
        if (!loaded_ || rev_ == saved_rev_) return true;
        doc = doc_;
        custom = custom_dirty_;
        saved_rev_ = rev_;
        custom_dirty_ = false;
    }
    // Each file says what it is. A sound this build does not have keeps the name it came
    // with, so the build that has it still knows it (load()).
    const bool foreign = doc.preset == EQ_PRESET_CUSTOM && !eq_same_section(doc.cfg.sec[0], doc.custom.sec[0]);
    if (!foreign) snprintf(doc.cfg.preset, sizeof(doc.cfg.preset), "%s", eq_preset(doc.preset).id);
    snprintf(doc.custom.preset, sizeof(doc.custom.preset), "%s", eq_preset(EQ_PRESET_CUSTOM).id);

    // Nothing an older build wrote is written over without its copy (keep_copy).
    if ((older_custom_ && !keep_copy(custom_path)) || (older_eq_ && !keep_copy(eq_path))) {
        std::lock_guard<std::mutex> lock(mx_);
        if (custom) custom_dirty_ = true;      // the next save tries it again
        return false;
    }
    older_custom_ = older_eq_ = false;
    // «Власний» first. Cut off before the second write, the pair still holds the user's set,
    // with what played before; the other way round, a preset chosen from «Власний» could
    // leave the set on disk nowhere.
    if (custom) {
        const bool aside = !custom_unreadable_ || move_aside(custom_path);
        if (aside) custom_unreadable_ = false;
        if (!aside || !eq_save(custom_path, doc.custom)) {
            std::lock_guard<std::mutex> lock(mx_);
            custom_dirty_ = true;
            return false;
        }
    }
    if (eq_unreadable_) {
        if (!move_aside(eq_path)) return false;
        eq_unreadable_ = false;
    }
    return eq_save(eq_path, doc.cfg);
}

}  // namespace audio_core
