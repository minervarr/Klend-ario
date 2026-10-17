#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <vector>

#include "klend_ario/event.hh"
#include "klend_ario/stats.hh"

using namespace klend_ario;

namespace {

long long at(const char *text) {
    const auto value = parseIso8601(text);
    assert(value.has_value());
    return *value;
}

Event makeEvent(long long id, const char *start, const char *end, EventType type, bool hidden) {
    Event event;
    event.id = id;
    event.title = "e";
    event.startUtc = at(start);
    event.endUtc = at(end);
    event.type = type;
    event.hidden = hidden;
    return event;
}

void testCountsAndClassification() {
    const long long now = at("2026-10-16T12:00:00Z");
    const std::vector<Event> events = {
        makeEvent(1, "2026-10-16T09:00:00Z", "2026-10-16T10:00:00Z", EventType::Event, false),
        makeEvent(2, "2026-10-16T09:30:00Z", "2026-10-16T10:30:00Z", EventType::Event, false),
        makeEvent(3, "2026-10-16T23:00:00Z", "2026-10-17T07:00:00Z", EventType::Sleep, true),
        makeEvent(4, "2026-10-16T12:00:00Z", "2026-10-16T13:00:00Z", EventType::Alarm, false),
        makeEvent(5, "2026-10-16T14:00:00Z", "2026-10-16T15:00:00Z", EventType::Wake, false),
    };

    const CalendarStats stats = computeStats(events, now);
    assert(stats.total == 5);
    assert(stats.visible == 4);
    assert(stats.hidden == 1);

    assert(stats.byType[static_cast<size_t>(EventType::Event)] == 2);
    assert(stats.byType[static_cast<size_t>(EventType::Sleep)] == 1);
    assert(stats.byType[static_cast<size_t>(EventType::Wake)] == 1);
    assert(stats.byType[static_cast<size_t>(EventType::Alarm)] == 1);

    // A and B ended before noon; D starts exactly at noon (ongoing); C and E
    // are still ahead.
    assert(stats.past == 2);
    assert(stats.ongoing == 1);
    assert(stats.upcoming == 2);

    // 1h + 1h + 8h + 1h + 1h = 12h. Overlaps double-count by design.
    assert(stats.scheduledSeconds == 12 * 60 * 60);

    // A/B overlap. The alarm row is a reminder and does not conflict with D's
    // neighbour (there is none); C and E are disjoint.
    assert(stats.conflictPairs == 1);

    assert(stats.earliestStartUtc == at("2026-10-16T09:00:00Z"));
    assert(stats.latestEndUtc == at("2026-10-17T07:00:00Z"));
}

void testEmpty() {
    const CalendarStats stats =
        computeStats({}, at("2026-10-16T12:00:00Z"));
    assert(stats.total == 0);
    assert(stats.visible == 0 && stats.hidden == 0);
    assert(stats.upcoming == 0 && stats.ongoing == 0 && stats.past == 0);
    assert(stats.scheduledSeconds == 0);
    assert(stats.conflictPairs == 0);
    assert(!stats.earliestStartUtc.has_value());
    assert(!stats.latestEndUtc.has_value());
}

void testZeroLengthAtNowIsPast() {
    const long long now = at("2026-10-16T12:00:00Z");
    const std::vector<Event> events = {
        makeEvent(1, "2026-10-16T12:00:00Z", "2026-10-16T12:00:00Z", EventType::Event, false),
    };
    const CalendarStats stats = computeStats(events, now);
    assert(stats.past == 1);
    assert(stats.ongoing == 0);
    assert(stats.upcoming == 0);
    assert(stats.scheduledSeconds == 0);
    assert(stats.earliestStartUtc == now);
    assert(stats.latestEndUtc == now);
}

void testHiddenOverlapCountsWhenPassed() {
    const long long now = at("2026-10-16T12:00:00Z");
    const std::vector<Event> events = {
        makeEvent(1, "2026-10-16T09:00:00Z", "2026-10-16T10:00:00Z", EventType::Event, false),
        makeEvent(2, "2026-10-16T09:15:00Z", "2026-10-16T09:45:00Z", EventType::Sleep, true),
    };
    const CalendarStats stats = computeStats(events, now);
    assert(stats.visible == 1 && stats.hidden == 1);
    assert(stats.conflictPairs == 1); // visibility is the caller's filter
}

} // namespace

int main() {
    testCountsAndClassification();
    testEmpty();
    testZeroLengthAtNowIsPast();
    testHiddenOverlapCountsWhenPassed();
    std::printf("stats_test: all assertions passed\n");
    return 0;
}
