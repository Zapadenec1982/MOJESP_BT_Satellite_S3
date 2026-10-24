/**
 * @file playlist.cpp
 * @brief Implementation of the index. See playlist.h for the concurrency contract.
 */

#include "playlist.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

namespace player_core {

Playlist& playlist() {
    static Playlist p;
    return p;
}

bool Playlist::init() {
    if (rels_) return true;
    // 384 KB. PSRAM because nothing here is touched from an interrupt or the audio task,
    // and internal RAM on this board is worth more than the few cycles the cache costs.
    rels_ = (char*)heap_caps_malloc((size_t)MAX_TRACKS * MAX_REL,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return rels_ != nullptr;
}

void Playlist::begin(const char* root) {
    gen_.fetch_add(1, std::memory_order_release);      // odd: being rebuilt
    count_.store(0, std::memory_order_release);
    snprintf(root_, sizeof(root_), "%s", root ? root : "");
    fill_ = 0;
}

bool Playlist::add(const char* rel) {
    if (!rels_ || fill_ >= MAX_TRACKS || !rel) return false;
    snprintf(at(fill_), MAX_REL, "%s", rel);
    fill_++;
    return true;
}

namespace {

/// Sorting compares through this, because what is being sorted is a permutation of indices
/// rather than the entries themselves: moving 192-byte rows around inside the comparison
/// loop is what made the folder-sized version too slow to grow.
const char* g_sort_base = nullptr;

int cmp_rel(const void* a, const void* b) {
    const uint16_t ia = *(const uint16_t*)a, ib = *(const uint16_t*)b;
    return strcasecmp(g_sort_base + (size_t)ia * Playlist::MAX_REL,
                      g_sort_base + (size_t)ib * Playlist::MAX_REL);
}

/// The part of `rel` after the last separator, or all of it when there is none.
const char* base_of(const char* rel) {
    const char* slash = strrchr(rel, '/');
    return slash ? slash + 1 : rel;
}

}  // namespace

void Playlist::sort_pending() {
    if (!rels_ || fill_ < 2) return;

    // An order for the entries, then the entries moved into it. Sorting the index costs
    // 2 KB and a few thousand comparisons; sorting the entries themselves would move a
    // third of a megabyte around, repeatedly.
    uint16_t* order = (uint16_t*)heap_caps_malloc((size_t)fill_ * sizeof(uint16_t),
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char* tmp = (char*)heap_caps_malloc(MAX_REL, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!order || !tmp) { free(order); free(tmp); return; }   // unsorted beats not playing

    for (int i = 0; i < fill_; i++) order[i] = (uint16_t)i;
    g_sort_base = rels_;
    qsort(order, (size_t)fill_, sizeof(uint16_t), cmp_rel);
    g_sort_base = nullptr;

    // Apply the permutation in place, cycle by cycle: one spare entry, one pass, no second
    // copy of the table. `order[i] == i` marks a position already in its final place.
    for (int i = 0; i < fill_; i++) {
        if (order[i] == (uint16_t)i) continue;
        memcpy(tmp, at(i), MAX_REL);
        int j = i;
        for (;;) {
            const int src = order[j];
            order[j] = (uint16_t)j;
            if (src == i) break;
            memcpy(at(j), at(src), MAX_REL);
            j = src;
        }
        memcpy(at(j), tmp, MAX_REL);
    }
    free(order);
    free(tmp);
}

int Playlist::find_pending(const char* rel) const {
    if (!rels_ || !rel || !rel[0]) return -1;
    for (int i = 0; i < fill_; i++) {
        if (strcmp(at(i), rel) == 0) return i;
    }
    return -1;
}

void Playlist::commit(int cursor) {
    if (cursor < 0) cursor = 0;
    if (cursor >= fill_) cursor = fill_ ? fill_ - 1 : 0;
    cursor_.store(cursor, std::memory_order_release);
    count_.store(fill_, std::memory_order_release);
    gen_.fetch_add(1, std::memory_order_release);      // even: settled
}

void Playlist::set_cursor(int i) {
    const int n = count();
    if (n <= 0) return;
    if (i < 0) i = 0; else if (i >= n) i = n - 1;
    cursor_.store(i, std::memory_order_release);
}

bool Playlist::rel_at(int i, char* out, size_t n) const {
    if (!rels_ || !out || n == 0) return false;
    for (int attempt = 0; attempt < 4; attempt++) {
        const uint32_t g0 = gen_.load(std::memory_order_acquire);
        if (g0 & 1u) continue;                          // mid-rebuild
        if (i < 0 || i >= count()) return false;
        snprintf(out, n, "%s", at(i));
        if (gen_.load(std::memory_order_acquire) == g0) return true;
    }
    return false;
}

bool Playlist::name_at(int i, char* out, size_t n) const {
    char rel[MAX_REL];
    if (!rel_at(i, rel, sizeof(rel))) return false;
    snprintf(out, n, "%s", base_of(rel));
    return true;
}

bool Playlist::folder_at(int i, char* out, size_t n) const {
    char rel[MAX_REL];
    if (!rel_at(i, rel, sizeof(rel))) return false;
    const char* slash = strrchr(rel, '/');
    if (!slash) { if (n) out[0] = '\0'; return true; }
    snprintf(out, n, "%.*s", (int)(slash - rel), rel);
    return true;
}

bool Playlist::path_at(int i, char* out, size_t n) const {
    char rel[MAX_REL];
    if (!rel_at(i, rel, sizeof(rel))) return false;
    snprintf(out, n, "%.*s/%.*s", MAX_DIR - 1, root_, MAX_REL - 1, rel);
    return true;
}

int Playlist::find(const char* rel) const {
    if (!rels_ || !rel || !rel[0]) return -1;
    const int n = count();
    char have[MAX_REL];
    for (int i = 0; i < n; i++) {
        if (rel_at(i, have, sizeof(have)) && strcmp(have, rel) == 0) return i;
    }
    return -1;
}

bool Playlist::root(char* out, size_t n) const {
    if (!out || n == 0) return false;
    snprintf(out, n, "%s", root_);
    return true;
}

namespace {
/// A different index than `from`, uniformly chosen. With one track there is no other, so the
/// caller gets the same one back and shuffle degenerates to repeat, which is the only
/// sensible reading of "shuffle a list of one".
int random_other(int n, int from) {
    if (n <= 1) return 0;
    int pick = (int)(esp_random() % (uint32_t)(n - 1));
    if (pick >= from) pick++;
    return pick;
}
}  // namespace

int Playlist::advance(bool shuffle, Repeat repeat) const {
    const int n = count();
    if (n <= 0) return -1;
    const int cur = cursor();
    if (repeat == Repeat::ONE) return cur;
    if (shuffle) return random_other(n, cur);
    if (cur + 1 < n) return cur + 1;
    return (repeat == Repeat::ALL) ? 0 : -1;           // −1: the card is over
}

int Playlist::next(bool shuffle, Repeat repeat) const {
    const int n = count();
    if (n <= 0) return -1;
    const int cur = cursor();
    if (shuffle) return random_other(n, cur);
    if (cur + 1 < n) return cur + 1;
    // Pressing "next" on the last track wraps even with repeat off: the button is an
    // instruction, not a policy, and stopping dead would look like it did nothing.
    (void)repeat;
    return 0;
}

int Playlist::previous(bool shuffle) const {
    const int n = count();
    if (n <= 0) return -1;
    const int cur = cursor();
    if (shuffle) return random_other(n, cur);
    return (cur > 0) ? cur - 1 : n - 1;
}

}  // namespace player_core
