#include "scene.hh"

#include <algorithm>
#include <cmath>
#include <utility>

namespace klend_ario::gui {
namespace {

const char *kDayLetters[7] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
const char *kEllipsis = "\xE2\x80\xA6"; // …

Op fillOp(const Rect &rect, Color color, int radius = 0) {
    Op op;
    op.kind = OpKind::Fill;
    op.rect = rect;
    op.color = color;
    op.radius = radius;
    return op;
}

Op strokeOp(const Rect &rect, Color color, int thickness, int radius = 0) {
    Op op;
    op.kind = OpKind::Stroke;
    op.rect = rect;
    op.color = color;
    op.thickness = thickness;
    op.radius = radius;
    return op;
}

Op textOp(const Rect &rect, std::string text, int size, Color color,
          Align align = Align::Left) {
    Op op;
    op.kind = OpKind::Text;
    op.rect = rect;
    op.color = color;
    op.size = size;
    op.align = align;
    op.text = std::move(text);
    return op;
}

Op hatchOp(const Rect &rect, Color color, int thickness, int spacing) {
    Op op;
    op.kind = OpKind::Hatch;
    op.rect = rect;
    op.color = color;
    op.thickness = thickness;
    op.spacing = spacing;
    return op;
}

Color toColor(Rgb rgb, unsigned char alpha = 255) {
    return Color{rgb.r, rgb.g, rgb.b, alpha};
}

// Same hue and saturation, lower value: the urgency border must not become a
// second colour channel, but it must be visible against its own fill.
Rgb shade(Rgb rgb, double factor) {
    auto channel = [factor](unsigned char v) {
        return static_cast<unsigned char>(std::lround(static_cast<double>(v) * factor));
    };
    return Rgb{channel(rgb.r), channel(rgb.g), channel(rgb.b)};
}

// Truncate to `maxWidth`, cutting on a UTF-8 boundary and appending an ellipsis.
// Returns "" when not even the ellipsis fits, so the caller draws nothing rather
// than a lone dot.
std::string fitText(std::string_view text, int size, int maxWidth, const TextMetrics &measure) {
    if (text.empty() || maxWidth <= 0 || size <= 0) {
        return {};
    }
    if (measure.width(text, size) <= maxWidth) {
        return std::string(text);
    }

    // A byte that is not a continuation byte starts a codepoint.
    std::vector<size_t> starts;
    for (size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) {
            starts.push_back(i);
        }
    }
    for (auto it = starts.rbegin(); it != starts.rend(); ++it) {
        std::string candidate(text.substr(0, *it));
        candidate += kEllipsis;
        if (measure.width(candidate, size) <= maxWidth) {
            return candidate;
        }
    }
    return {};
}

} // namespace

Scene buildWeekScene(int width, int height, const WeekModel &week, long long nowUtc,
                     const TextMetrics &text, const Theme &theme) {
    Scene scene;
    const Frame frame = frameFor(width, height);
    const Rect &content = frame.content;
    const UiMetrics m = metricsFor(content);
    const WeekGrid grid = weekGrid(content, m.topBarHeight + m.dayBarHeight);

    const Rect topBar{content.x, content.y, content.w, m.topBarHeight};
    const Rect dayBar{content.x, content.y + m.topBarHeight, content.w, m.dayBarHeight};

    // --- Background, today's tint and the grid, in that order. Everything that
    // follows is drawn on top of them.
    scene.ops.push_back(fillOp(Rect{0, 0, width, height}, theme.background));
    for (int i = 0; i < 7; ++i) {
        if (week.days[static_cast<size_t>(i)].isToday) {
            scene.ops.push_back(fillOp(grid.column[static_cast<size_t>(i)], theme.todayColumn));
        }
    }
    for (int i = 1; i < 7; ++i) {
        const Rect &column = grid.column[static_cast<size_t>(i)];
        scene.ops.push_back(
            fillOp(Rect{column.x, grid.body.y, 1, grid.body.h}, theme.gridLine));
    }
    if (dayBar.h > 0) {
        scene.ops.push_back(
            fillOp(Rect{content.x, dayBar.bottom() - 1, content.w, 1}, theme.gridLine));
    }

    // --- Navigation widths, needed before the week label so the two never
    // overlap. Widths come from the injected metrics, so a wide font pushes the
    // buttons left instead of clipping the labels.
    struct NavButton {
        const char *id;
        const char *label;
    };
    const NavButton nav[3] = {{"prev", "<"}, {"today", "today"},
                              {"next", ">"}}; // < today >
    int navWidth = 0;
    int navTextWidth[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        navTextWidth[i] = text.width(nav[i].label, m.navLabelSize);
        navWidth += navTextWidth[i] + 2 * m.buttonPad;
    }
    // A real gap between pills: at 1080p labelPad is 3, which drew the three
    // buttons as one bar with the labels floating inside it.
    const int navGap = std::max(m.buttonPad / 2, 4);
    navWidth += 2 * navGap;

    // --- Week counter, top-left.
    const int labelWidth =
        std::max(topBar.w - 2 * m.headerPad - navWidth - m.labelPad, 0);
    if (!week.weekLabel.empty() && topBar.h > 0) {
        const std::string label = fitText(week.weekLabel, m.weekLabelSize,
                                          labelWidth, text);
        if (!label.empty()) {
            scene.ops.push_back(textOp(
                Rect{content.x + m.headerPad,
                     topBar.y + (topBar.h - m.weekLabelSize) / 2,
                     labelWidth, m.weekLabelSize},
                label, m.weekLabelSize, theme.headerText));
        }
    }

    int navX = topBar.right() - m.headerPad - navWidth;
    const int navY = topBar.y + (topBar.h - m.buttonHeight) / 2;
    for (int i = 0; i < 3; ++i) {
        const int buttonW = navTextWidth[i] + 2 * m.buttonPad;
        const Rect button{navX, navY, buttonW, m.buttonHeight};
        scene.ops.push_back(fillOp(button, theme.buttonFill, m.buttonHeight / 2));
        scene.ops.push_back(textOp(
            Rect{button.x, button.y + (button.h - m.navLabelSize) / 2, button.w,
                 m.navLabelSize},
            nav[i].label, m.navLabelSize, theme.headerText, Align::Center));
        scene.hits.push_back(HitTarget{button, nav[i].id});
        navX += buttonW + navGap;
    }

    // --- Day headers: "SU 11". Today is bright, a holiday is amber, the rest
    // are dim, which is the whole visual hierarchy a header needs.
    for (int i = 0; i < 7; ++i) {
        const DayInfo &info = week.days[static_cast<size_t>(i)];
        const Rect &column = grid.column[static_cast<size_t>(i)];
        const std::string label =
            std::string(kDayLetters[i]) + " " + formatDayNumber(info.day);
        Color color = theme.dimText;
        if (!info.holiday.empty()) {
            color = theme.holidayAccent;
        }
        if (info.isToday) {
            color = theme.nowLine;
        }
        scene.ops.push_back(textOp(
            Rect{column.x + m.labelPad, dayBar.y + (dayBar.h - m.dayLetterSize) / 2,
                 column.w - 2 * m.labelPad, m.dayLetterSize},
            label, m.dayLetterSize, color, Align::Center));
    }

    // --- The hatched tail: from a day's last visible block to midnight. Hiding
    // the sleep block (by querying visible rows only) extends the tail over it,
    // which is exactly the "nothing else today" reading merged.md asks for.
    for (int i = 0; i < 7; ++i) {
        const LocalDay &day = week.days[static_cast<size_t>(i)].day;
        long long lastEnd = day.startUtc;
        for (const Event &event : week.events) {
            if (event.type == EventType::Alarm) {
                continue;
            }
            if (event.startUtc >= day.endUtc || event.endUtc <= day.startUtc) {
                continue;
            }
            lastEnd = std::max(lastEnd, std::min(event.endUtc, day.endUtc));
        }
        const Rect tail = tailRect(grid.column[static_cast<size_t>(i)], day.startUtc,
                                   day.endUtc, lastEnd);
        if (tail.h >= m.tailMinHeight) {
            scene.ops.push_back(
                hatchOp(tail, theme.tailHatch, 1, std::max(m.hatchSpacing, 2)));
        }
    }

    // --- Events, day by day. `layoutDay` sees the whole day so overlaps become
    // side-by-side slots; the scene only decides how each slot looks.
    bool anyEvent = false;
    for (int i = 0; i < 7; ++i) {
        const LocalDay &day = week.days[static_cast<size_t>(i)].day;
        const Rect &column = grid.column[static_cast<size_t>(i)];

        std::vector<size_t> indices;
        std::vector<DaySpan> spans;
        for (size_t k = 0; k < week.events.size(); ++k) {
            const Event &event = week.events[k];
            if (event.type == EventType::Alarm) {
                continue; // a reminder is not a block
            }
            if (event.startUtc >= day.endUtc || event.endUtc <= day.startUtc) {
                continue;
            }
            indices.push_back(k);
            spans.push_back(DaySpan{event.startUtc, event.endUtc});
        }
        if (spans.empty()) {
            continue;
        }
        anyEvent = true;

        const std::vector<DayBox> boxes =
            layoutDay(column, day.startUtc, day.endUtc, spans);
        for (size_t j = 0; j < indices.size(); ++j) {
            const Event &event = week.events[indices[j]];
            const DayBox &box = boxes[j];

            Rect rect = box.rect;
            if (box.slots > 1) {
                rect.x += (box.slot > 0 ? m.boxGutter : 0);
                rect.w -= m.boxGutter;
            }
            rect.w = std::max(rect.w, 2);
            rect.h = std::max(rect.h, 1);

            const Rgb fillRgb = rgbForStyle(styleFor(event));
            const Color fill = toColor(fillRgb, 240);
            scene.ops.push_back(fillOp(rect, fill, 2));

            // Urgency 0 still shows one pixel; 100 must not eat the fill, so the
            // inset is clamped to a third of the box.
            int border = 1 + static_cast<int>(std::lround(
                                 styleFor(event).borderFactor * (m.maxBorderPx - 1)));
            border = std::clamp(border, 1, std::max(1, rect.h / 3));
            scene.ops.push_back(strokeOp(rect, toColor(shade(fillRgb, 0.55)), border, 2));

            // Labels by height tier: time, then title, then nothing. A five
            // minute event keeps its (thin, coloured) box and shows no text
            // rather than a clipped smear.
            const Color fg = toColor(textColorOn(fillRgb));
            const int innerX = rect.x + border + m.labelPad;
            const int innerW = rect.w - 2 * (border + m.labelPad);
            int y = rect.y + border + m.labelPad;
            const int innerBottom = rect.bottom() - border - m.labelPad;
            if (innerW <= 2) {
                continue;
            }

            if (!box.clippedTop && innerBottom - y >= m.eventTimeSize) {
                scene.ops.push_back(textOp(
                    Rect{innerX, y, innerW, m.eventTimeSize},
                    formatClockLocal(event.startUtc), m.eventTimeSize, fg));
                y += m.eventTimeSize + m.labelPad;
            }
            if (innerBottom - y >= m.eventTitleSize) {
                const std::string title =
                    fitText(event.title, m.eventTitleSize, innerW, text);
                if (!title.empty()) {
                    scene.ops.push_back(textOp(Rect{innerX, y, innerW, m.eventTitleSize},
                                               title, m.eventTitleSize, fg));
                }
            }
        }
    }

    // --- Now-line, on today's column, on top of the blocks it crosses.
    for (int i = 0; i < 7; ++i) {
        const DayInfo &info = week.days[static_cast<size_t>(i)];
        if (!info.isToday) {
            continue;
        }
        const LocalDay &day = info.day;
        if (nowUtc < day.startUtc || nowUtc >= day.endUtc) {
            continue;
        }
        const Rect &column = grid.column[static_cast<size_t>(i)];
        const int y = offsetForInstant(column, day.startUtc, day.endUtc, nowUtc);
        scene.ops.push_back(
            fillOp(Rect{column.x, y, column.w, m.nowLineThickness}, theme.nowLine));
    }

    // --- Empty state. Without it an empty week looks exactly like a broken one.
    if (!anyEvent && grid.body.h > 0) {
        const int size = m.dayLetterSize;
        scene.ops.push_back(textOp(
            Rect{grid.body.x, grid.body.y + (grid.body.h - size) / 2, grid.body.w, size},
            "No events this week. Add one in the CLI, or ask your AI.", size,
            theme.emptyHint, Align::Center));
    }

    return scene;
}

} // namespace klend_ario::gui
