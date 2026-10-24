/**
 * @file test_vlist_window.cpp
 * @brief VList's row pool: rows keep the entries they show while those stay in the window.
 *
 * modules/ui_lvgl/include/vlist_window.h. The widget used to refill all ten rows every time
 * a scroll crossed a row boundary; the property that fixes it is that only the rows whose
 * entry left the window change, and that the window is still shown exactly once.
 */

#include "doctest.h"
#include "../../modules/ui_lvgl/include/vlist_window.h"

#include <cstdlib>
#include <set>

namespace {

constexpr int POOL = 10;

struct Pool {
    int shown[POOL];
    Pool() { for (int& e : shown) e = -1; }
    /// Move the window; returns how many rows changed entry.
    int move(int first, int count) {
        int want[POOL];
        const int changed = vlist::assign(shown, POOL, first, count, want);
        for (int k = 0; k < POOL; k++) shown[k] = want[k];
        return changed;
    }
};

/// Every entry of the window is on exactly one row, and no row shows anything else.
void check_window(const Pool& p, int first, int count) {
    std::set<int> seen;
    for (int e : p.shown) {
        if (e < 0) continue;
        CHECK(e >= first);
        CHECK(e < first + POOL);
        CHECK(e < count);
        CHECK(seen.insert(e).second);
    }
    int expect = count - first;
    if (expect > POOL) expect = POOL;
    if (expect < 0) expect = 0;
    CHECK((int)seen.size() == expect);
}

} // namespace

TEST_CASE("vlist: the first placement fills every row in order") {
    Pool p;
    CHECK(p.move(0, 600) == POOL);
    for (int k = 0; k < POOL; k++) CHECK(p.shown[k] == k);
}

TEST_CASE("vlist: a window moved by one row refills one row") {
    Pool p;
    p.move(0, 600);
    CHECK(p.move(1, 600) == 1);
    CHECK(p.shown[0] == 10);                         // the row that left the top went to the bottom
    for (int k = 1; k < POOL; k++) CHECK(p.shown[k] == k);
    check_window(p, 1, 600);

    CHECK(p.move(0, 600) == 1);                      // and back
    CHECK(p.shown[0] == 0);
    check_window(p, 0, 600);
}

TEST_CASE("vlist: a window moved by d rows refills d rows") {
    Pool p;
    int first = 100;
    p.move(first, 600);
    for (int d = 1; d <= POOL + 2; d++) {
        int prev[POOL];
        for (int k = 0; k < POOL; k++) prev[k] = p.shown[k];
        first += d;
        CHECK(p.move(first, 600) == (d < POOL ? d : POOL));
        for (int k = 0; k < POOL; k++) {
            if (prev[k] >= first && prev[k] < first + POOL) CHECK(p.shown[k] == prev[k]);
        }
        check_window(p, first, 600);
    }
}

TEST_CASE("vlist: a jump refills every row") {
    Pool p;
    p.move(0, 600);
    CHECK(p.move(300, 600) == POOL);
    check_window(p, 300, 600);
}

TEST_CASE("vlist: a list shorter than the pool hides the rows it does not need") {
    Pool p;
    CHECK(p.move(0, 4) == 4);
    for (int k = 0; k < 4; k++) CHECK(p.shown[k] == k);
    for (int k = 4; k < POOL; k++) CHECK(p.shown[k] == -1);
    CHECK(p.move(0, 4) == 0);                        // nothing moved, nothing to fill
}

TEST_CASE("vlist: a list that shrank hides the rows past its end and keeps the rest") {
    Pool p;
    p.move(0, 600);
    CHECK(p.move(0, 5) == 5);
    for (int k = 0; k < 5; k++) CHECK(p.shown[k] == k);
    for (int k = 5; k < POOL; k++) CHECK(p.shown[k] == -1);
    check_window(p, 0, 5);
}

TEST_CASE("vlist: an empty list hides every row") {
    Pool p;
    p.move(0, 600);
    CHECK(p.move(0, 0) == POOL);
    for (int e : p.shown) CHECK(e == -1);
}

TEST_CASE("vlist: two rows claiming one entry leave it to the first") {
    int shown[POOL] = {3, 3, 5, -1, -1, -1, -1, -1, -1, -1};
    int want[POOL];
    vlist::assign(shown, POOL, 0, 600, want);
    CHECK(want[0] == 3);
    CHECK(want[1] != 3);
    std::set<int> seen;
    for (int e : want) { CHECK(e >= 0); CHECK(seen.insert(e).second); }
    CHECK(seen.size() == (size_t)POOL);
}

TEST_CASE("vlist: random scrolling keeps the window whole and touches only what moved") {
    std::srand(1234);
    const int count = 2000;
    Pool p;
    int first = 0;
    p.move(first, count);
    for (int step = 0; step < 5000; step++) {
        const int r = std::rand() % 100;
        int next;
        if (r < 80)      next = first + (std::rand() % 7) - 3;          // a finger: a few rows
        else if (r < 95) next = first + (std::rand() % 41) - 20;        // a flick
        else             next = std::rand() % count;                    // a jump (reveal, a new list)
        if (next < 0) next = 0;
        if (next > count - POOL) next = count - POOL;
        int prev[POOL];
        for (int k = 0; k < POOL; k++) prev[k] = p.shown[k];
        int moved = next > first ? next - first : first - next;
        if (moved > POOL) moved = POOL;
        CHECK(p.move(next, count) == moved);
        for (int k = 0; k < POOL; k++) {
            if (prev[k] >= next && prev[k] < next + POOL) CHECK(p.shown[k] == prev[k]);
        }
        check_window(p, next, count);
        first = next;
    }
}
