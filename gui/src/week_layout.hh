#pragma once

// The weekly view's GEOMETRY, as a pure function of numbers.
//
// Nothing here touches a Canvas, a window, or the OS — it takes a pixel frame,
// a day's start/end instant and an event's span, and returns rectangles. That
// is what lets week_layout_test assert the whole view with no GPU, which is the
// only reason the layout can be trusted before it is ever drawn.
//
// The renderer maps these rectangles to draw calls and supplies the colours;
// the colours live in core (klend_ario::styleFor / rgbForStyle) so every
// surface colours an event identically.
//
// Product rules encoded (merged.md §5): 1cm-equivalent margins at FHD and up,
// full-bleed below; seven day columns, week starts Sunday; event height is
// proportional to duration; a hatched tail marks "nothing else today".
//
// A day is passed as [dayStartUtc, dayEndUtc) rather than assumed to be 86,400
// seconds: the model stores UTC, but the columns are LOCAL days and a DST day is
// 23 or 25 hours long. Assuming 86,400 put every event after the transition in
// the wrong place, and did it only twice a year in only some zones.

#include <array>
#include <vector>

namespace klend_ario::gui {

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    int right() const { return x + w; }
    int bottom() const { return y + h; }
    bool empty() const { return w <= 0 || h <= 0; }
    // Half-open, like every other range in this app: the right and bottom edges
    // belong to the next rect. Hit-testing two adjacent day columns therefore
    // picks exactly one of them.
    bool contains(int px, int py) const {
        return px >= x && px < right() && py >= y && py < bottom();
    }
};

// ---------------------------------------------------------------------------
// The frame (merged.md §5.1). The design baseline is 16cm x 9cm with 1cm
// margins, scaled by the screen WIDTH (16cm <=> the screen width). Below FHD
// the margins are suppressed so a 720p-class screen draws edge to edge.
//
// The baseline calibrates the MARGIN; it does not letterbox. A 16:10 window
// keeps all of its height and its content is simply that much taller.
// ---------------------------------------------------------------------------
inline constexpr int kFhdWidth = 1920;
inline constexpr int kBaselineWidthCm = 16;

struct Frame {
    Rect content;  // the drawing area inside the margins
    int margin = 0; // px; 0 below FHD
};

Frame frameFor(int width, int height);

// ---------------------------------------------------------------------------
// The seven day columns (Sunday first) plus the header band (week counter and
// day labels). The columns tile `content` exactly: any leftover pixels from the
// integer division are spread over the first columns, so there is never a 1px
// seam or an uncovered sliver at the right edge.
// ---------------------------------------------------------------------------
struct WeekGrid {
    Rect header;
    Rect body;
    std::array<Rect, 7> column; // Sunday .. Saturday
};

WeekGrid weekGrid(const Rect &content, int headerHeight);

// ---------------------------------------------------------------------------
// One event's box inside a day column, on that column's start..end axis.
//
// `dayStartUtc`/`dayEndUtc` are the column's LOCAL midnight and the next one, as
// UTC instants. An event that starts before or ends after the day is CLIPPED to
// it — the flags say so, so the renderer can draw a continued marker rather than
// pretend the event is short.
// ---------------------------------------------------------------------------
struct EventBox {
    Rect rect;
    bool clippedTop = false;    // began before this day
    bool clippedBottom = false; // continues past this day
};

EventBox dayTimeRect(const Rect &column, long long dayStartUtc, long long dayEndUtc,
                     long long eventStartUtc, long long eventEndUtc);

// The pixel offset of an instant inside a day column, on the same clamped
// fraction `dayTimeRect` uses. Exposed because the now-line needs a position and
// must not re-derive the rounding (a second copy of this is how the line and the
// events it crosses drift apart).
int offsetForInstant(const Rect &column, long long dayStartUtc, long long dayEndUtc,
                     long long atUtc);

// ---------------------------------------------------------------------------
// A whole day laid out at once, because overlaps cannot be seen one event at a
// time. Events that overlap in time form a CLUSTER; the cluster is split into as
// many side-by-side slots as its maximum concurrency, and every event in the
// cluster gets the same slot width so the grid stays readable.
//
// `spans` is the input order; `boxes` is PARALLEL to it (same index), which is
// how the scene keeps its event and its rectangle together without an id map.
// Touching intervals (end == start) do NOT overlap, matching the half-open
// convention everywhere else in the app.
// ---------------------------------------------------------------------------
struct DaySpan {
    long long startUtc = 0;
    long long endUtc = 0;
};

struct DayBox {
    Rect rect;
    int slot = 0;  // 0-based sub-column inside its cluster
    int slots = 1; // how many sub-columns that cluster needed
    bool clippedTop = false;
    bool clippedBottom = false;
};

std::vector<DayBox> layoutDay(const Rect &column, long long dayStartUtc, long long dayEndUtc,
                              const std::vector<DaySpan> &spans);

// ---------------------------------------------------------------------------
// The hatched "nothing else today" region: from the end of a day's last visible
// event to the bottom of its column. `lastEventEndUtc` is ignored when it is not
// after `dayStartUtc` (an empty day is all tail).
// ---------------------------------------------------------------------------
Rect tailRect(const Rect &column, long long dayStartUtc, long long dayEndUtc,
              long long lastEventEndUtc);

} // namespace klend_ario::gui
