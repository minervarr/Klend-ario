#include "week_layout.hh"

#include <algorithm>
#include <cmath>

namespace klend_ario::gui {
namespace {

constexpr long long kDaySeconds = 86400;

// A column fraction (0..1 of the day) to a pixel offset within the column,
// clamped so an out-of-range event never draws outside its day.
int offsetFor(long long secondsFromDayStart, int columnHeight) {
    const double fraction =
        std::clamp(static_cast<double>(secondsFromDayStart) / kDaySeconds, 0.0, 1.0);
    return static_cast<int>(std::lround(fraction * columnHeight));
}

} // namespace

Frame frameFor(int width, int height) {
    Frame frame;
    int margin = 0;
    if (width >= kFhdWidth) {
        // 1cm-equivalent: the 16cm baseline maps to the screen width.
        margin = static_cast<int>(std::lround(static_cast<double>(width) / kBaselineWidthCm));
    }
    // Guard a screen so small the margin would consume it: a quarter of the
    // short side is the most a margin may take.
    margin = std::min(margin, std::min(width, height) / 4);
    frame.margin = margin;
    frame.content = Rect{margin, margin, width - 2 * margin, height - 2 * margin};
    return frame;
}

WeekGrid weekGrid(const Rect &content, int headerHeight) {
    WeekGrid grid;
    headerHeight = std::clamp(headerHeight, 0, content.h);

    grid.header = Rect{content.x, content.y, content.w, headerHeight};
    grid.body = Rect{content.x, content.y + headerHeight, content.w, content.h - headerHeight};

    const int base = grid.body.w / 7;
    const int remainder = grid.body.w - base * 7; // 0..6 leftover pixels
    int x = grid.body.x;
    for (int i = 0; i < 7; ++i) {
        const int w = base + (i < remainder ? 1 : 0);
        grid.column[i] = Rect{x, grid.body.y, w, grid.body.h};
        x += w;
    }
    return grid;
}

EventBox dayTimeRect(const Rect &column, long long dayStartUtc, long long eventStartUtc,
                     long long eventEndUtc) {
    const long long dayEndUtc = dayStartUtc + kDaySeconds;
    EventBox box;
    box.clippedTop = eventStartUtc < dayStartUtc;
    box.clippedBottom = eventEndUtc > dayEndUtc;

    const long long visibleStart = std::max(eventStartUtc, dayStartUtc);
    const long long visibleEnd = std::min(eventEndUtc, dayEndUtc);

    const int top = column.y + offsetFor(visibleStart - dayStartUtc, column.h);
    const int bottom = column.y + offsetFor(visibleEnd - dayStartUtc, column.h);

    // A zero-length or sub-pixel event still gets one visible pixel, so it is
    // never drawn as nothing.
    const int height = std::max(bottom - top, 1);
    box.rect = Rect{column.x, top, column.w, height};
    return box;
}

Rect tailRect(const Rect &column, long long dayStartUtc, long long lastEventEndUtc) {
    const long long from = std::max(lastEventEndUtc, dayStartUtc);
    const int top = column.y + offsetFor(from - dayStartUtc, column.h);
    return Rect{column.x, top, column.w, column.bottom() - top};
}

} // namespace klend_ario::gui
