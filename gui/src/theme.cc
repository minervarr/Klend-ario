#include "theme.hh"

#include <algorithm>
#include <cmath>

namespace klend_ario::gui {
namespace {

int scaled(int base, double fraction, int floor) {
    return std::max(floor, static_cast<int>(std::lround(base * fraction)));
}

} // namespace

Theme defaultTheme() { return Theme{}; }

UiMetrics metricsFor(const Rect &content) {
    UiMetrics m;
    const int h = std::max(content.h, 1);
    const int w = std::max(content.w, 1);

    m.topBarHeight = scaled(h, 0.065, 28);
    m.dayBarHeight = scaled(h, 0.058, 26);
    m.weekLabelSize = scaled(h, 0.032, 13);
    m.navLabelSize = scaled(h, 0.024, 11);
    m.dayLetterSize = scaled(h, 0.026, 11);
    m.dayDateSize = scaled(h, 0.022, 10);
    m.eventTimeSize = scaled(h, 0.017, 9);
    m.eventTitleSize = scaled(h, 0.020, 10);
    m.boxGutter = scaled(w, 0.0015, 1);
    m.maxBorderPx = scaled(w, 0.004, 2);
    m.tailMinHeight = scaled(h, 0.012, 6);
    m.hatchSpacing = scaled(w, 0.006, 6);
    m.labelPad = scaled(h, 0.004, 2);
    m.headerPad = scaled(w, 0.008, 8);
    m.nowLineThickness = scaled(h, 0.0025, 1);
    m.buttonHeight = std::max(m.navLabelSize + 2 * m.labelPad + 6, 18);
    m.buttonPad = std::max(m.labelPad * 3, 6);
    return m;
}

} // namespace klend_ario::gui
