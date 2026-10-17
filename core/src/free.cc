#include "klend_ario/free.hh"

#include <algorithm>
#include <utility>

namespace klend_ario {
namespace {

struct Interval {
    long long startUtc = 0;
    long long endUtc = 0;
};

bool byStart(const Interval &a, const Interval &b) {
    if (a.startUtc != b.startUtc) {
        return a.startUtc < b.startUtc;
    }
    return a.endUtc < b.endUtc;
}

} // namespace

std::vector<FreeSlot> freeSlots(const std::vector<Event> &events, long long fromUtc,
                                long long toUtc, long long minDurationSeconds) {
    if (toUtc <= fromUtc || minDurationSeconds <= 0) {
        return {};
    }

    std::vector<Interval> busy;
    busy.reserve(events.size());
    for (const Event &event : events) {
        if (event.type == EventType::Alarm) {
            continue;
        }
        const long long startUtc = std::max(event.startUtc, fromUtc);
        const long long endUtc = std::min(event.endUtc, toUtc);
        if (startUtc < endUtc) {
            busy.push_back(Interval{startUtc, endUtc});
        }
    }
    std::sort(busy.begin(), busy.end(), byStart);

    std::vector<FreeSlot> slots;
    long long cursor = fromUtc;
    for (const Interval &block : busy) {
        if (block.startUtc > cursor && block.startUtc - cursor >= minDurationSeconds) {
            slots.push_back(FreeSlot{cursor, block.startUtc});
        }
        cursor = std::max(cursor, block.endUtc);
    }
    if (cursor < toUtc && toUtc - cursor >= minDurationSeconds) {
        slots.push_back(FreeSlot{cursor, toUtc});
    }
    return slots;
}

} // namespace klend_ario
