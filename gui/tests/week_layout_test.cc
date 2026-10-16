#undef NDEBUG
#include <cassert>
#include <cstdio>

#include "week_layout.hh"

using namespace klend_ario::gui;

static void testFrameMargins() {
    // FHD and up: 1cm-equivalent margin == width / 16.
    Frame fhd = frameFor(1920, 1080);
    assert(fhd.margin == 120);
    assert(fhd.content.x == 120 && fhd.content.y == 120);
    assert(fhd.content.w == 1680 && fhd.content.h == 840);
    // 4K.
    assert(frameFor(3840, 2160).margin == 240);
    // Below FHD: full bleed.
    Frame hd = frameFor(1280, 720);
    assert(hd.margin == 0);
    assert(hd.content.x == 0 && hd.content.w == 1280 && hd.content.h == 720);
}

static void testGridTilesContentExactly() {
    WeekGrid grid = weekGrid(Rect{0, 0, 700, 700}, 40);
    assert(grid.header.y == 0 && grid.header.h == 40);
    assert(grid.body.y == 40 && grid.body.h == 660);
    for (int i = 0; i < 7; ++i) { // 700 / 7 == 100 exactly
        assert(grid.column[i].x == i * 100);
        assert(grid.column[i].w == 100);
        assert(grid.column[i].h == 660);
    }
    assert(grid.column[6].right() == 700);
}

static void testGridSpreadsRemainder() {
    // 703 / 7 == 100 r3: the first three columns take one extra pixel, and the
    // total still lands exactly on the content edge (no seam, no right sliver).
    WeekGrid grid = weekGrid(Rect{0, 0, 703, 500}, 50);
    assert(grid.column[0].w == 101);
    assert(grid.column[1].w == 101);
    assert(grid.column[2].w == 101);
    assert(grid.column[3].w == 100);
    assert(grid.column[6].right() == 703);
    for (int i = 1; i < 7; ++i) {
        assert(grid.column[i].x == grid.column[i - 1].right());
    }
}

static void testEventProportionalHeight() {
    const Rect column{0, 40, 100, 660}; // one day over 660px
    // Noon..18:00 is a quarter of the day -> a quarter of the height.
    EventBox box = dayTimeRect(column, 0, 12 * 3600, 18 * 3600);
    assert(!box.clippedTop && !box.clippedBottom);
    assert(box.rect.x == 0 && box.rect.w == 100);
    assert(box.rect.y == 40 + 330);
    assert(box.rect.h == 165);
}

static void testEventClipping() {
    const Rect column{0, 0, 100, 864}; // 1px per 100 seconds
    // Began the night before, ends 02:00 -> clipped at the top only.
    EventBox a = dayTimeRect(column, 100000, 100000 - 3600, 100000 + 2 * 3600);
    assert(a.clippedTop);
    assert(!a.clippedBottom);
    assert(a.rect.y == 0);
    assert(a.rect.h == 72); // 2h at 1px/100s
    // Runs past midnight -> clipped at the bottom, to the column floor.
    EventBox b = dayTimeRect(column, 100000, 100000 + 23 * 3600, 100000 + 26 * 3600);
    assert(!b.clippedTop);
    assert(b.clippedBottom);
    assert(b.rect.bottom() == 864);
}

static void testZeroLengthEventIsVisible() {
    const Rect column{0, 0, 100, 864};
    assert(dayTimeRect(column, 0, 3600, 3600).rect.h >= 1);
}

static void testTail() {
    const Rect column{0, 0, 100, 864};
    // Last event ends at noon -> the tail runs from noon to the bottom.
    const Rect tail = tailRect(column, 0, 12 * 3600);
    assert(tail.y == 432);
    assert(tail.bottom() == 864);
    // An empty day is all tail.
    const Rect all = tailRect(column, 0, 0);
    assert(all.y == 0 && all.h == 864);
    // A last event before the day started is still all tail.
    const Rect before = tailRect(column, 100000, 50000);
    assert(before.y == 0 && before.h == 864);
}

int main() {
    testFrameMargins();
    testGridTilesContentExactly();
    testGridSpreadsRemainder();
    testEventProportionalHeight();
    testEventClipping();
    testZeroLengthEventIsVisible();
    testTail();
    std::printf("week_layout_test: all assertions passed\n");
    return 0;
}
