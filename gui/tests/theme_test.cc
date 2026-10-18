#undef NDEBUG
#include <cassert>
#include <cstdio>

#include "theme.hh"

using namespace klend_ario::gui;

static void testMetricsAtFhd() {
    // Content at 1080p: 1680x840 after the 1cm-equivalent margins.
    const UiMetrics m = metricsFor(Rect{120, 120, 1680, 840});
    assert(m.topBarHeight == 55);
    assert(m.dayBarHeight == 49);
    assert(m.weekLabelSize == 27);
    assert(m.navLabelSize == 20);
    assert(m.dayLetterSize == 22);
    assert(m.dayDateSize == 18);
    assert(m.eventTimeSize == 14);
    assert(m.eventTitleSize == 17);
    assert(m.boxGutter == 3);
    assert(m.maxBorderPx == 7);
    assert(m.tailMinHeight == 10);
    assert(m.hatchSpacing == 10);
    assert(m.labelPad == 3);
    assert(m.headerPad == 13);
    assert(m.nowLineThickness == 2);
    // The button is tall enough to hold its label with padding.
    assert(m.buttonHeight >= m.navLabelSize + 2 * m.labelPad);
}

static void testMetricsAtSevenTwentyP() {
    // Full-bleed 720p content: the same fractions, smaller pixels. Every size is
    // smaller than at FHD and still positive — nothing collapses to invisible.
    const UiMetrics small = metricsFor(Rect{0, 0, 1280, 720});
    const UiMetrics large = metricsFor(Rect{120, 120, 1680, 840});
    assert(small.topBarHeight < large.topBarHeight);
    assert(small.eventTitleSize < large.eventTitleSize);
    assert(small.eventTimeSize >= 9);
    assert(small.dayLetterSize >= 11);
    assert(small.buttonHeight >= 18);
    assert(small.hatchSpacing >= 6);
}

static void testMetricsHaveFloors() {
    // A tiny content rect must not produce zero-sized text or negative padding.
    const UiMetrics m = metricsFor(Rect{0, 0, 100, 100});
    assert(m.topBarHeight == 28);
    assert(m.dayBarHeight == 26);
    assert(m.weekLabelSize == 13);
    assert(m.navLabelSize == 11);
    assert(m.dayLetterSize == 11);
    assert(m.dayDateSize == 10);
    assert(m.eventTimeSize == 9);
    assert(m.eventTitleSize == 10);
    assert(m.boxGutter == 1);
    assert(m.maxBorderPx == 2);
    assert(m.tailMinHeight == 6);
    assert(m.hatchSpacing == 6);
    assert(m.labelPad == 2);
    assert(m.headerPad == 8);
    assert(m.nowLineThickness == 1);
}

static void testReadableFloorClampsTextRoles() {
    // The window passes the generated font floor (16 px for ui.otf). Every text
    // role must respect it, while geometry keeps its own floors.
    const UiMetrics small = metricsFor(Rect{0, 0, 1280, 720}, 16);
    assert(small.weekLabelSize >= 16);
    assert(small.navLabelSize >= 16);
    assert(small.dayLetterSize >= 16);
    assert(small.dayDateSize >= 16);
    assert(small.eventTimeSize >= 16);
    assert(small.eventTitleSize >= 16);
    assert(small.labelPad >= 2);
    assert(small.hatchSpacing >= 6);

    // At 1080p the event time (14) was the only role below the floor; the
    // title (17) and the headers were already above it.
    const UiMetrics fhd = metricsFor(Rect{120, 120, 1680, 840}, 16);
    assert(fhd.eventTimeSize == 16);
    assert(fhd.eventTitleSize == 17);
    assert(fhd.weekLabelSize == 27);
}

static void testThemeIsReadable() {
    const Theme theme = defaultTheme();
    // Chrome is dark and text is light: a white-on-white palette would pass
    // every geometry test and be unusable.
    assert(theme.background.r < 60 && theme.background.g < 60 && theme.background.b < 60);
    assert(theme.headerText.r > 200 && theme.headerText.g > 200);
    assert(theme.dimText.r > theme.background.r);
    assert(theme.dimText.r < theme.headerText.r);
    // The now-line and the holiday accent are the two saturated accents, and
    // they must be distinguishable from each other.
    assert(theme.nowLine.r > 180 && theme.nowLine.b < 120);
    assert(theme.holidayAccent.g > 150);
    assert(theme.tailHatch.r > theme.background.r);
    assert(theme.tailHatch.r < theme.gridLine.r + 30);
}

int main() {
    testMetricsAtFhd();
    testMetricsAtSevenTwentyP();
    testMetricsHaveFloors();
    testReadableFloorClampsTextRoles();
    testThemeIsReadable();
    std::printf("theme_test: all assertions passed\n");
    return 0;
}
