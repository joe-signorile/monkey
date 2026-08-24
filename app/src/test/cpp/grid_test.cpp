// Host-side check for Grid's scroll physics and hit-testing.
//
// This logic cannot be exercised on a stock emulator: a bare image has too few
// apps to overflow one screen, so maxScroll() is 0 and every scroll path is
// dead code at runtime. Grid deliberately depends on neither GL nor JNI so it
// can be compiled and run straight on the host.
//
//   ./tools/run-grid-test.sh

#include "../../main/cpp/grid.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    }
}

void checkNear(float actual, float expected, const char* what) {
    if (std::fabs(actual - expected) > 0.5f) {
        std::printf("  FAIL: %s (got %.2f, want %.2f)\n", what, actual, expected);
        ++g_failures;
    }
}

// 1280x800 viewport => 4 columns, 320px pitch, 384px rowPitch (pitch * 1.2,
// the label strip adds 20%). `n` tiles => ceil(n/4) rows.
Grid makeGrid(size_t n, int w = 1280, int h = 800) {
    Grid grid;
    grid.setViewport(w, h);
    for (size_t i = 0; i < n; ++i) {
        AppTile tile;
        tile.pkg = "pkg." + std::to_string(i);
        tile.iconW = tile.iconH = 128;
        grid.add(std::move(tile));
    }
    grid.layout();
    return grid;
}

// Scrolls by dragging, which is the only way to move scroll_ from outside.
void dragBy(Grid& grid, float dy) {
    grid.onDown(640.0f, 400.0f);
    grid.onMove(640.0f, 400.0f - dy, 0.016f);
    grid.onUp(640.0f, 400.0f - dy);
}

void settle(Grid& grid) {
    // 5 simulated seconds is far past the spring-back convergence constant.
    for (int i = 0; i < 300; ++i) grid.advance(1.0f / 60.0f);
}

void testColumnsClampAtWideViewport() {
    // 2560/320 = 8 columns, above the 6-column cap.
    Grid grid = makeGrid(17, 2560, 1600);
    // 17 tiles over 6 columns = 3 rows; pitch 426.67, rowPitch 512.0 =>
    // 1536px content < 1600px viewport, so maxScroll() is 0 and any drag is
    // pure overscroll bounce.
    dragBy(grid, 500.0f);
    settle(grid);
    checkNear(grid.iconRectOf(0).y, (426.67f - 426.67f * 0.68f) * 0.5f,
              "content shorter than viewport settles back to the top after release");
}

void testScrollSpringsBackAtBothEnds() {
    Grid grid = makeGrid(40);  // 10 rows * 384 rowPitch = 3840 content, 800 viewport

    dragBy(grid, -5000.0f);  // drag hard past the top
    check(grid.iconRectOf(0).y > 51.2f, "a hard drag past the top overscrolls, not clamps");
    settle(grid);
    checkNear(grid.iconRectOf(0).y, 51.2f, "scroll springs back to the top after release");

    dragBy(grid, 99999.0f);  // drag hard past the bottom
    // maxScroll = 3840 - 800 = 3040; last row (9) top = 9*384 - 3040 + 51.2 = 467.2.
    check(grid.iconRectOf(36).y < 467.2f,
          "a hard drag past the bottom overscrolls, not clamps");
    settle(grid);
    checkNear(grid.iconRectOf(36).y, 467.2f, "scroll springs back to the bottom after release");
}

void testOverscrollIsDamped() {
    Grid grid = makeGrid(40);
    dragBy(grid, -1000.0f);  // drag past the top by 1000px raw
    // Damped overscroll must land strictly between "didn't move" (a hard
    // clamp) and "moved the full raw distance" (no damping at all).
    const float overscrollPx = grid.iconRectOf(0).y - 51.2f;
    check(overscrollPx > 0.0f && overscrollPx < 1000.0f,
          "overscroll past the top is damped, not clamped or undamped");
}

void testOverscrollKeepsGridBusyUntilSettled() {
    Grid grid = makeGrid(40);
    dragBy(grid, -1000.0f);
    check(grid.busy(),
          "an overscrolled grid must stay busy so the render thread doesn't idle mid-bounce");
    settle(grid);
    check(!grid.busy(), "the grid goes idle once the spring-back settles");
}

void testTapVersusScroll() {
    Grid grid = makeGrid(40);

    grid.onDown(160.0f, 160.0f);
    check(grid.onUp(160.0f, 160.0f) == 0, "a still touch on tile 0 is a tap");

    grid.onDown(160.0f, 160.0f);
    grid.onMove(160.0f, 60.0f, 0.016f);
    check(grid.onUp(160.0f, 60.0f) == Grid::kNoTile,
          "a touch that travels past the slop is a scroll, not a tap");
}

void testGutterTapsMiss() {
    Grid grid = makeGrid(40);
    // (10,10) is inside tile 0's cell but outside the icon+label tap zone
    // (inset 51.2 on every side except the label strip below).
    grid.onDown(10.0f, 10.0f);
    check(grid.onUp(10.0f, 10.0f) == Grid::kNoTile,
          "a tap in the gutter must not launch the neighbouring app");
}

void testLabelTapHitsSameTile() {
    Grid grid = makeGrid(40);
    // Tile 0's icon rect is y:[51.2, 268.8]; the label strip below it runs
    // 268.8 to 332.8 (labelStripH_ = pitch_ * 0.2 = 64). A tap there, not on
    // the icon itself, must still hit tile 0.
    grid.onDown(160.0f, 300.0f);
    check(grid.onUp(160.0f, 300.0f) == 0, "a tap on the label hits the same tile as its icon");
}

void testBelowLabelStripMisses() {
    Grid grid = makeGrid(40);
    // Past the label strip (332.8) is gutter before the next row (row pitch
    // 384); must miss, same as the side gutters.
    grid.onDown(160.0f, 360.0f);
    check(grid.onUp(160.0f, 360.0f) == Grid::kNoTile,
          "a tap below the label strip, in the gutter before the next row, misses");
}

void testHitTestFollowsScroll() {
    Grid grid = makeGrid(40);
    dragBy(grid, 384.0f);  // exactly one row down (rowPitch_ = pitch_ * 1.2)

    grid.onDown(160.0f, 160.0f);
    check(grid.onUp(160.0f, 160.0f) == 4,
          "after scrolling one row, the top-left tap hits the second row");
}

void testTapPastLastTileMisses() {
    Grid grid = makeGrid(5);  // 2 rows, second row holds only tile 4
    grid.onDown(160.0f + 320.0f, 160.0f + 384.0f);  // row 1, col 1 => index 5
    check(grid.onUp(160.0f + 320.0f, 160.0f + 384.0f) == Grid::kNoTile,
          "a tap on an empty cell past the last tile misses");
}

void testFlingDecaysAndStops() {
    Grid grid = makeGrid(40);

    grid.onDown(640.0f, 700.0f);
    grid.onMove(640.0f, 400.0f, 0.016f);  // fast flick upward
    grid.onUp(640.0f, 400.0f);
    check(grid.busy(), "a flick leaves the grid flinging");

    settle(grid);
    check(!grid.busy(), "a fling eventually comes to rest");
}

void testDownStopsAFling() {
    Grid grid = makeGrid(40);
    grid.onDown(640.0f, 700.0f);
    grid.onMove(640.0f, 400.0f, 0.016f);
    grid.onUp(640.0f, 400.0f);
    check(grid.busy(), "precondition: flinging");

    grid.onDown(640.0f, 400.0f);
    grid.onUp(640.0f, 400.0f);
    check(!grid.busy(), "touching down catches a moving grid");
}

void testClearResetsScroll() {
    Grid grid = makeGrid(40);
    dragBy(grid, 1000.0f);
    grid.clear();
    check(grid.count() == 0, "clear empties the tile list");
    Grid fresh = makeGrid(40);
    checkNear(fresh.iconRectOf(0).y, 51.2f, "a fresh grid starts at the top");
}

}  // namespace

int main() {
    std::printf("Grid tests\n");
    testColumnsClampAtWideViewport();
    testScrollSpringsBackAtBothEnds();
    testOverscrollIsDamped();
    testOverscrollKeepsGridBusyUntilSettled();
    testTapVersusScroll();
    testGutterTapsMiss();
    testLabelTapHitsSameTile();
    testBelowLabelStripMisses();
    testHitTestFollowsScroll();
    testTapPastLastTileMisses();
    testFlingDecaysAndStops();
    testDownStopsAFling();
    testClearResetsScroll();

    if (g_failures == 0) {
        std::printf("all passed\n");
        return EXIT_SUCCESS;
    }
    std::printf("%d failure(s)\n", g_failures);
    return EXIT_FAILURE;
}
