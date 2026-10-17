#pragma once

// The weekly view as a LIST OF DRAW OPS.
//
// Between "rectangles" (week_layout) and "Vulkan calls" (the renderer) there is
// a decision layer: which colour, which label, how thick the border, where the
// now-line goes, what the header says, whether a sliver gets text at all. Left
// inside the renderer, none of that is testable without a GPU; put here, the
// whole visual contract is asserted by scene_test on any machine.
//
// The renderer's only job is to map each Op to a Canvas call. Text measurement
// is injected (TextMetrics) so truncation is decided — and tested — without a
// font, exactly like the alarm engine's Notifier seam.

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "klend_ario/event.hh"
#include "local_time.hh"
#include "theme.hh"
#include "week_layout.hh"

namespace klend_ario::gui {

class TextMetrics {
public:
    virtual ~TextMetrics() = default;
    // Width of `text` at `sizePx`, in pixels.
    virtual int width(std::string_view text, int sizePx) const = 0;
};

struct DayInfo {
    LocalDay day;
    bool isToday = false;
    std::string holiday; // empty = no holiday; otherwise its name (accent only)
};

struct WeekModel {
    std::string weekLabel; // "Week 42 · Oct 2026"
    std::array<DayInfo, 7> days{};
    // Every row overlapping the week that the view should consider. `Alarm`
    // rows are reminders, not blocks: the scene skips them (they have no
    // duration and would draw as a fake meeting at their anchor).
    std::vector<Event> events;
};

enum class OpKind { Fill, Stroke, Text, Hatch };
enum class Align { Left, Center, Right };

struct Op {
    OpKind kind = OpKind::Fill;
    Rect rect;         // Fill/Stroke: the shape. Hatch: the clipped region.
                       // Text: the anchor box — horizontally aligned inside it,
                       // and clipped to it. Baseline is rect.y + size.
    Color color;
    int thickness = 0; // Stroke: inward thickness; Hatch: line thickness.
    int spacing = 0;   // Hatch: px between 45-degree lines.
    int radius = 0;    // Fill/Stroke: corner radius (buttons).
    int size = 0;      // Text: px.
    Align align = Align::Left;
    std::string text;  // Text only.
};

struct HitTarget {
    Rect rect;
    std::string id; // "prev" | "today" | "next"
};

struct Scene {
    std::vector<Op> ops;   // painter's order: draw in sequence
    std::vector<HitTarget> hits;
};

// `week` is drawn as seven local days; `nowUtc` is passed in, never read from
// the clock, so the now-line and "today" are assertable.
Scene buildWeekScene(int width, int height, const WeekModel &week, long long nowUtc,
                     const TextMetrics &text, const Theme &theme = defaultTheme());

} // namespace klend_ario::gui
