#include "week_layout.hh"

#include <algorithm>
#include <cmath>

namespace klend_ario::gui {
namespace {

// A fraction of a day to a pixel offset within the column, clamped so an
// out-of-range event never draws outside its day.
int offsetFor(long long secondsFromDayStart, long long dayLengthSeconds, int columnHeight) {
    if (dayLengthSeconds <= 0) {
        return 0;
    }
    const double fraction = std::clamp(
        static_cast<double>(secondsFromDayStart) / static_cast<double>(dayLengthSeconds),
        0.0, 1.0);
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

EventBox dayTimeRect(const Rect &column, long long dayStartUtc, long long dayEndUtc,
                     long long eventStartUtc, long long eventEndUtc) {
    EventBox box;
    box.clippedTop = eventStartUtc < dayStartUtc;
    box.clippedBottom = eventEndUtc > dayEndUtc;

    const long long visibleStart = std::max(eventStartUtc, dayStartUtc);
    const long long visibleEnd = std::min(eventEndUtc, dayEndUtc);

    const int top = offsetForInstant(column, dayStartUtc, dayEndUtc, visibleStart);
    const int bottom = offsetForInstant(column, dayStartUtc, dayEndUtc, visibleEnd);

    // A zero-length or sub-pixel event still gets one visible pixel, so it is
    // never drawn as nothing.
    const int height = std::max(bottom - top, 1);
    box.rect = Rect{column.x, top, column.w, height};
    return box;
}

int offsetForInstant(const Rect &column, long long dayStartUtc, long long dayEndUtc,
                     long long atUtc) {
    return column.y + offsetFor(atUtc - dayStartUtc, dayEndUtc - dayStartUtc, column.h);
}

std::vector<DayBox> layoutDay(const Rect &column, long long dayStartUtc, long long dayEndUtc,
                              const std::vector<DaySpan> &spans) {
    std::vector<DayBox> boxes(spans.size());
    if (spans.empty() || column.empty()) {
        return boxes;
    }

    // --- 1. Walk the day in start order, grouping touching-by-time events into
    // clusters. A cluster ends at the first event that starts at or after the
    // cluster's running maximum end: half-open intervals that merely touch
    // (end == next start) are sequential, not concurrent.
    std::vector<size_t> order(spans.size());
    for (size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [&spans](size_t a, size_t b) {
        if (spans[a].startUtc != spans[b].startUtc) {
            return spans[a].startUtc < spans[b].startUtc;
        }
        return spans[a].endUtc < spans[b].endUtc;
    });

    size_t clusterBegin = 0;
    long long clusterEnd = spans[order[0]].endUtc;
    auto closeCluster = [&](size_t clusterEndIndex) {
        // --- 2. Greedy slot assignment inside the cluster: reuse the first slot
        // whose last event has ended (half-open, so `<= start` is free).
        std::vector<long long> slotEnds;
        for (size_t k = clusterBegin; k < clusterEndIndex; ++k) {
            const size_t index = order[k];
            size_t slot = 0;
            while (slot < slotEnds.size() && slotEnds[slot] > spans[index].startUtc) {
                ++slot;
            }
            if (slot == slotEnds.size()) {
                slotEnds.push_back(spans[index].endUtc);
            } else {
                slotEnds[slot] = spans[index].endUtc;
            }
            boxes[index].slot = static_cast<int>(slot);
        }

        const int slots = static_cast<int>(std::max<size_t>(slotEnds.size(), 1));
        const int base = column.w / slots;
        const int remainder = column.w - base * slots;
        int x = column.x;
        std::vector<int> width(static_cast<size_t>(slots));
        std::vector<int> left(static_cast<size_t>(slots));
        for (int s = 0; s < slots; ++s) {
            width[static_cast<size_t>(s)] = base + (s < remainder ? 1 : 0);
            left[static_cast<size_t>(s)] = x;
            x += width[static_cast<size_t>(s)];
        }

        for (size_t k = clusterBegin; k < clusterEndIndex; ++k) {
            const size_t index = order[k];
            const EventBox geometry =
                dayTimeRect(Rect{left[static_cast<size_t>(boxes[index].slot)], column.y,
                                 width[static_cast<size_t>(boxes[index].slot)], column.h},
                            dayStartUtc, dayEndUtc, spans[index].startUtc, spans[index].endUtc);
            boxes[index].rect = geometry.rect;
            boxes[index].clippedTop = geometry.clippedTop;
            boxes[index].clippedBottom = geometry.clippedBottom;
            boxes[index].slots = slots;
        }
    };

    for (size_t i = 1; i < order.size(); ++i) {
        if (spans[order[i]].startUtc >= clusterEnd) {
            closeCluster(i);
            clusterBegin = i;
            clusterEnd = spans[order[i]].endUtc;
        } else {
            clusterEnd = std::max(clusterEnd, spans[order[i]].endUtc);
        }
    }
    closeCluster(order.size());
    return boxes;
}

Rect tailRect(const Rect &column, long long dayStartUtc, long long dayEndUtc,
              long long lastEventEndUtc) {
    const long long dayLength = dayEndUtc - dayStartUtc;
    const long long from = std::max(lastEventEndUtc, dayStartUtc);
    const int top = column.y + offsetFor(from - dayStartUtc, dayLength, column.h);
    return Rect{column.x, top, column.w, column.bottom() - top};
}

} // namespace klend_ario::gui
