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
    // The baseline calibrates the margin, it does not letterbox: a 16:10 window
    // keeps every pixel of its height.
    assert(frameFor(1920, 1200).content.h == 960);
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
    EventBox box = dayTimeRect(column, 0, 86400, 12 * 3600, 18 * 3600);
    assert(!box.clippedTop && !box.clippedBottom);
    assert(box.rect.x == 0 && box.rect.w == 100);
    assert(box.rect.y == 40 + 330);
    assert(box.rect.h == 165);
}

static void testEventClipping() {
    const Rect column{0, 0, 100, 864}; // 1px per 100 seconds
    const long long dayStart = 100000;
    const long long dayEnd = dayStart + 86400;
    // Began the night before, ends 02:00 -> clipped at the top only.
    EventBox a = dayTimeRect(column, dayStart, dayEnd, dayStart - 3600, dayStart + 2 * 3600);
    assert(a.clippedTop);
    assert(!a.clippedBottom);
    assert(a.rect.y == 0);
    assert(a.rect.h == 72); // 2h at 1px/100s
    // Runs past midnight -> clipped at the bottom, to the column floor.
    EventBox b = dayTimeRect(column, dayStart, dayEnd, dayStart + 23 * 3600,
                             dayStart + 26 * 3600);
    assert(!b.clippedTop);
    assert(b.clippedBottom);
    assert(b.rect.bottom() == 864);
}

static void testZeroLengthEventIsVisible() {
    const Rect column{0, 0, 100, 864};
    assert(dayTimeRect(column, 0, 86400, 3600, 3600).rect.h >= 1);
}

// The two days a year that are not 86,400 seconds long. Both use a 30px/hour
// scale so the arithmetic is exact and the failure is unambiguous: with the old
// fixed 86,400 the noon offset would be 360 on the short day and 360 on the long
// one only by coincidence of rounding, and everything after 02:00 would drift.
static void testDstDayLengths() {
    // 23-hour day: 690px tall, 30px per hour.
    const Rect shortDay{0, 0, 100, 690};
    EventBox noonToSix = dayTimeRect(shortDay, 0, 23 * 3600, 12 * 3600, 18 * 3600);
    assert(noonToSix.rect.y == 360);
    assert(noonToSix.rect.h == 180);

    // 25-hour day: 750px tall, still 30px per hour.
    const Rect longDay{0, 0, 100, 750};
    EventBox same = dayTimeRect(longDay, 0, 25 * 3600, 12 * 3600, 18 * 3600);
    assert(same.rect.y == 360);
    assert(same.rect.h == 180);

    // A 25h day's last hour is still on the column, not past its bottom.
    EventBox late = dayTimeRect(longDay, 0, 25 * 3600, 24 * 3600, 25 * 3600);
    assert(!late.clippedBottom);
    assert(late.rect.bottom() == 750);
}

static void testLayoutDaySequential() {
    const Rect column{0, 0, 700, 700};
    // A then B, touching at 01:00: half-open, so they are sequential, not
    // concurrent. Each keeps the full column width.
    const std::vector<DaySpan> spans{{0, 3600}, {3600, 7200}};
    const std::vector<DayBox> boxes = layoutDay(column, 0, 86400, spans);
    assert(boxes.size() == 2);
    for (const DayBox &box : boxes) {
        assert(box.slots == 1);
        assert(box.slot == 0);
        assert(box.rect.x == 0 && box.rect.w == 700);
    }
    // A gap behaves the same way.
    const std::vector<DaySpan> gapped{{0, 3600}, {7200, 9000}};
    const std::vector<DayBox> apart = layoutDay(column, 0, 86400, gapped);
    assert(apart[0].slots == 1 && apart[1].slots == 1);
}

static void testLayoutDayOverlapSplitsColumn() {
    const Rect column{0, 0, 700, 700};
    // 10:00-12:00 and 11:00-13:00 -> a two-slot cluster.
    const std::vector<DaySpan> spans{{10 * 3600, 12 * 3600}, {11 * 3600, 13 * 3600}};
    const std::vector<DayBox> boxes = layoutDay(column, 0, 86400, spans);
    assert(boxes.size() == 2);
    assert(boxes[0].slots == 2 && boxes[1].slots == 2);
    assert(boxes[0].slot == 0 && boxes[1].slot == 1);
    assert(boxes[0].rect.x == 0 && boxes[0].rect.w == 350);
    assert(boxes[1].rect.x == 350 && boxes[1].rect.w == 350);

    // boxes[] is PARALLEL to the input, not sorted: feed the later event first
    // and the slots must follow the events, not the loop order.
    const std::vector<DaySpan> reversed{{11 * 3600, 13 * 3600}, {10 * 3600, 12 * 3600}};
    const std::vector<DayBox> flipped = layoutDay(column, 0, 86400, reversed);
    assert(flipped[0].slot == 1 && flipped[0].rect.x == 350);
    assert(flipped[1].slot == 0 && flipped[1].rect.x == 0);
}

static void testLayoutDayClusterEndsAtTouch() {
    const Rect column{0, 0, 700, 700};
    // A 09-10, B 09:30-11, C 11-12. A and B overlap; C only TOUCHES B, so it
    // opens a new cluster and gets the full width back.
    const std::vector<DaySpan> spans{
        {9 * 3600, 10 * 3600}, {9 * 3600 + 1800, 11 * 3600}, {11 * 3600, 12 * 3600}};
    const std::vector<DayBox> boxes = layoutDay(column, 0, 86400, spans);
    assert(boxes[0].slots == 2 && boxes[0].slot == 0);
    assert(boxes[1].slots == 2 && boxes[1].slot == 1);
    assert(boxes[2].slots == 1 && boxes[2].slot == 0);
    assert(boxes[2].rect.x == 0 && boxes[2].rect.w == 700);
}

static void testLayoutDayRemainderTilesExactly() {
    // Three concurrent events over 703px: 235 + 234 + 234 = 703, no seam.
    const Rect column{0, 0, 703, 700};
    const std::vector<DaySpan> spans{
        {0, 3600}, {0, 3600}, {0, 3600}};
    const std::vector<DayBox> boxes = layoutDay(column, 0, 86400, spans);
    assert(boxes[0].rect.x == 0 && boxes[0].rect.w == 235);
    assert(boxes[1].rect.x == 235 && boxes[1].rect.w == 234);
    assert(boxes[2].rect.x == 469 && boxes[2].rect.w == 234);
    assert(boxes[2].rect.right() == 703);
}

static void testLayoutDayReusesFreedSlot() {
    const Rect column{0, 0, 700, 700};
    // A 09-11, B 09-10, C 10-11: B and C never coexist, so the cluster needs
    // two slots, not three, and the cluster stays two-slot wide for all three.
    // C takes the slot B freed at 10:00.
    const std::vector<DaySpan> spans{
        {9 * 3600, 11 * 3600}, {9 * 3600, 10 * 3600}, {10 * 3600, 11 * 3600}};
    const std::vector<DayBox> boxes = layoutDay(column, 0, 86400, spans);
    assert(boxes[0].slots == 2 && boxes[1].slots == 2 && boxes[2].slots == 2);
    assert(boxes[0].slot == 1); // A, the long event
    assert(boxes[1].slot == 0); // B, first short event
    assert(boxes[2].slot == 0); // C, reuses the slot B freed
}

static void testLayoutDayClipFlags() {
    const Rect column{0, 0, 100, 864};
    const long long dayStart = 5000;
    const long long dayEnd = dayStart + 86400;
    const std::vector<DaySpan> spans{{dayStart - 600, dayStart + 600},
                                     {dayEnd - 600, dayEnd + 600}};
    const std::vector<DayBox> boxes = layoutDay(column, dayStart, dayEnd, spans);
    assert(boxes[0].clippedTop && !boxes[0].clippedBottom);
    assert(boxes[1].clippedBottom && !boxes[1].clippedTop);
}

static void testLayoutDayEmpty() {
    const Rect column{0, 0, 100, 864};
    assert(layoutDay(column, 0, 86400, {}).empty());
}

static void testTail() {
    const Rect column{0, 0, 100, 864};
    // Last event ends at noon -> the tail runs from noon to the bottom.
    const Rect tail = tailRect(column, 0, 86400, 12 * 3600);
    assert(tail.y == 432);
    assert(tail.bottom() == 864);
    // An empty day is all tail.
    const Rect all = tailRect(column, 0, 86400, 0);
    assert(all.y == 0 && all.h == 864);
    // A last event before the day started is still all tail.
    const Rect before = tailRect(column, 100000, 100000 + 86400, 50000);
    assert(before.y == 0 && before.h == 864);
    // A 23-hour day: noon is 12/23 of the column, not 12/24.
    const Rect dst = tailRect(column, 0, 23 * 3600, 12 * 3600);
    assert(dst.y == 451); // lround(0.5217 * 864)
}

int main() {
    testFrameMargins();
    testGridTilesContentExactly();
    testGridSpreadsRemainder();
    testEventProportionalHeight();
    testEventClipping();
    testZeroLengthEventIsVisible();
    testDstDayLengths();
    testLayoutDaySequential();
    testLayoutDayOverlapSplitsColumn();
    testLayoutDayClusterEndsAtTouch();
    testLayoutDayRemainderTilesExactly();
    testLayoutDayReusesFreedSlot();
    testLayoutDayClipFlags();
    testLayoutDayEmpty();
    testTail();
    std::printf("week_layout_test: all assertions passed\n");
    return 0;
}
