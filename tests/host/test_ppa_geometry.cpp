/**
 * @file test_ppa_geometry.cpp
 * @brief The rotated-block geometry that hangs the PPA on IDF 5.5.5, and the grid that avoids it.
 *
 * modules/ui_lvgl/include/ppa_geometry.h. The UI rotates every flushed block by 270 degrees with
 * the PPA straight into the scanned-out frame buffer; a block whose last transfer unit is under
 * the DMA2D FIFO and gets no macro-block bypass never finishes (esp-idf#19023). Such blocks go as
 * stripes that are each safe; these cases pin which blocks need it and that the stripes are.
 */

#include "doctest.h"
#include "../../modules/ui_lvgl/include/ppa_geometry.h"

using ppa_geom::would_hang;
using ppa_geom::leftover_under_fifo;

TEST_CASE("ppa geometry: unrotated blocks never hang") {
    for (uint32_t w = 1; w <= 800; w++)
        for (uint32_t h = 1; h <= 120; h++)
            CHECK_FALSE(would_hang(w, h, false));
}

TEST_CASE("ppa geometry: a block LVGL can flush hangs whole") {
    // 785 x 60 at 270 degrees: units are 60 wide (28 left over) and 785 tall (1 row left over),
    // 28 x 1 x 24 bits < 1536, but 5.5.5 looks at 785 % 32 = 17 by 60 % 16 = 12 and sees plenty.
    CHECK(leftover_under_fifo(60, 785));
    CHECK_FALSE(leftover_under_fifo(785, 60));
    CHECK(would_hang(785, 60, true));
}

TEST_CASE("ppa geometry: every block on a 16 x 16 grid is safe") {
    for (uint32_t w = 16; w <= 800; w += 16)
        for (uint32_t h = 16; h <= 480; h += 16)
            CHECK_FALSE(would_hang(w, h, true));
}

TEST_CASE("ppa geometry: off the grid, hanging blocks exist in the sizes LVGL flushes") {
    int hanging = 0;
    for (uint32_t w = 1; w <= 800; w++)
        for (uint32_t h = 1; h <= 120; h++)
            if (would_hang(w, h, true)) hanging++;
    CHECK(hanging > 0);   // the stripes are not decoration
}

TEST_CASE("ppa geometry: a safe block is one stripe") {
    uint16_t rows[16];
    REQUIRE(ppa_geom::plan_stripes(752, 64, rows, 16) == 1);
    CHECK(rows[0] == 64);
}

TEST_CASE("ppa geometry: every split is safe, covers the block, and only narrow blocks have none") {
    uint16_t rows[16];
    int none_wide = 0, none_narrow = 0, split = 0;
    for (uint32_t w = 1; w <= 800; w++) {
        for (uint32_t h = 1; h <= 96; h++) {
            const int n = ppa_geom::plan_stripes(w, h, rows, 16);
            if (n == 0) {
                if (w > 32) none_wide++; else none_narrow++;
                continue;
            }
            if (n > 1) split++;
            uint32_t sum = 0;
            for (int i = 0; i < n; i++) {
                CHECK_FALSE(would_hang(w, rows[i], true));
                sum += rows[i];
            }
            CHECK(sum == h);
        }
    }
    CHECK(split > 0);
    CHECK(none_wide == 0);      // anything wider than one unit can always be cut safely
    MESSAGE("splits " << split << ", narrow blocks left to the CPU " << none_narrow);
}
