#pragma once

// The weekly view's GEOMETRY, as a pure function of numbers.
//
// Nothing here touches a Canvas, a window, or the OS — it takes a pixel frame,
// a day's start instant and an event's span, and returns rectangles. That is
// what lets week_layout_test assert the whole view with no GPU, which is the
// only reason the layout can be trusted before it is ever drawn.
//
// The renderer (Phase 4 proper, vk_canvas) maps these rectangles to draw calls
// and supplies the colours; the colours live in core (klend_ario::styleFor) so
// every surface colours an event identically.
//
// Product rules encoded (merged.md §5): 1cm-equivalent margins at FHD and up,
// full-bleed below; seven day columns, week starts Sunday; event height is
// proportional to duration; a hatched tail marks "nothing else today".

#include <array>

namespace klend_ario::gui {

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    int right() const { return x + w; }
    int bottom() const { return y + h; }
    bool empty() const { return w <= 0 || h <= 0; }
};

// ---------------------------------------------------------------------------
// The frame (merged.md §5.1). The design baseline is 16cm x 9cm with 1cm
// margins, scaled by the screen WIDTH (16cm <=> the screen width). Below FHD
// the margins are suppressed so a 720p-class screen draws edge to edge.
// ---------------------------------------------------------------------------
inline constexpr int kFhdWidth = 1920;
inline constexpr int kBaselineWidthCm = 16;

struct Frame {
    Rect content;  // the drawing area inside the margins
    int margin = 0; // px; 0 below FHD
};

Frame frameFor(int width, int height);

// ---------------------------------------------------------------------------
// The seven day columns (Sunday first) plus the day-label header band. The
// columns tile `content` exactly: any leftover pixels from the integer
// division are spread over the first columns, so there is never a 1px seam or
// an uncovered sliver at the right edge.
// ---------------------------------------------------------------------------
struct WeekGrid {
    Rect header;
    Rect body;
    std::array<Rect, 7> column; // Sunday .. Saturday
};

WeekGrid weekGrid(const Rect &content, int headerHeight);

// ---------------------------------------------------------------------------
// One event's box inside a day column, on a 00:00-24:00 axis.
//
// `dayStartUtc` is the instant the column's day begins (the caller resolves
// local midnight; this stays in UTC so it is exact). An event that starts
// before or ends after the day is CLIPPED to it — the flags say so, so the
// renderer can draw a continued marker rather than pretend the event is short.
// ---------------------------------------------------------------------------
struct EventBox {
    Rect rect;
    bool clippedTop = false;    // began before this day
    bool clippedBottom = false; // continues past this day
};

EventBox dayTimeRect(const Rect &column, long long dayStartUtc, long long eventStartUtc,
                     long long eventEndUtc);

// ---------------------------------------------------------------------------
// The hatched "nothing else today" region: from the end of a day's last event
// to the bottom of its column. `lastEventEndUtc` is ignored when it is not
// after `dayStartUtc` (an empty day is all tail).
// ---------------------------------------------------------------------------
Rect tailRect(const Rect &column, long long dayStartUtc, long long lastEventEndUtc);

} // namespace klend_ario::gui
