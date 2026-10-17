#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "klend_ario/conflict.hh"
#include "klend_ario/event.hh"

using namespace klend_ario;

namespace {

long long at(const char *text) {
    const auto value = parseIso8601(text);
    assert(value.has_value());
    return *value;
}

Event makeEvent(long long id, const char *title, const char *start, const char *end,
                EventType type = EventType::Event) {
    Event event;
    event.id = id;
    event.title = title;
    event.startUtc = at(start);
    event.endUtc = at(end);
    event.type = type;
    return event;
}

void expect(const Conflict &conflict, long long firstId, const char *firstTitle, long long secondId,
            const char *secondTitle, const char *start, const char *end) {
    assert(conflict.firstId == firstId);
    assert(conflict.firstTitle == firstTitle);
    assert(conflict.secondId == secondId);
    assert(conflict.secondTitle == secondTitle);
    assert(conflict.startUtc == at(start));
    assert(conflict.endUtc == at(end));
}

void testPairwiseOverlaps() {
    // Input is deliberately unsorted. A and C only touch; E is zero-length;
    // F is an alarm row and must not participate.
    const std::vector<Event> events = {
        makeEvent(3, "C", "2026-10-16T10:00:00Z", "2026-10-16T11:00:00Z"),
        makeEvent(1, "A", "2026-10-16T09:00:00Z", "2026-10-16T10:00:00Z"),
        makeEvent(6, "F", "2026-10-16T09:00:00Z", "2026-10-16T10:00:00Z", EventType::Alarm),
        makeEvent(2, "B", "2026-10-16T09:30:00Z", "2026-10-16T10:30:00Z"),
        makeEvent(5, "E", "2026-10-16T09:30:00Z", "2026-10-16T09:30:00Z"),
        makeEvent(4, "Nested", "2026-10-16T09:15:00Z", "2026-10-16T09:45:00Z"),
        makeEvent(7, "G", "2026-10-16T09:50:00Z", "2026-10-16T10:10:00Z"),
    };

    const std::vector<Conflict> conflicts = findConflicts(events);
    assert(conflicts.size() == 7);

    expect(conflicts[0], 1, "A", 4, "Nested", "2026-10-16T09:15:00Z", "2026-10-16T09:45:00Z");
    expect(conflicts[1], 1, "A", 2, "B", "2026-10-16T09:30:00Z", "2026-10-16T10:00:00Z");
    expect(conflicts[2], 4, "Nested", 2, "B", "2026-10-16T09:30:00Z", "2026-10-16T09:45:00Z");
    expect(conflicts[3], 1, "A", 7, "G", "2026-10-16T09:50:00Z", "2026-10-16T10:00:00Z");
    expect(conflicts[4], 2, "B", 7, "G", "2026-10-16T09:50:00Z", "2026-10-16T10:10:00Z");
    expect(conflicts[5], 2, "B", 3, "C", "2026-10-16T10:00:00Z", "2026-10-16T10:30:00Z");
    expect(conflicts[6], 7, "G", 3, "C", "2026-10-16T10:00:00Z", "2026-10-16T10:10:00Z");
}

void testTripleOverlapIsThreePairs() {
    const std::vector<Event> events = {
        makeEvent(1, "A", "2026-10-16T09:00:00Z", "2026-10-16T12:00:00Z"),
        makeEvent(2, "B", "2026-10-16T10:00:00Z", "2026-10-16T11:00:00Z"),
        makeEvent(3, "C", "2026-10-16T10:30:00Z", "2026-10-16T12:30:00Z"),
    };
    const std::vector<Conflict> conflicts = findConflicts(events);
    assert(conflicts.size() == 3);
    expect(conflicts[0], 1, "A", 2, "B", "2026-10-16T10:00:00Z", "2026-10-16T11:00:00Z");
    expect(conflicts[1], 1, "A", 3, "C", "2026-10-16T10:30:00Z", "2026-10-16T12:00:00Z");
    expect(conflicts[2], 2, "B", 3, "C", "2026-10-16T10:30:00Z", "2026-10-16T11:00:00Z");
}

void testTouchingZeroLengthAndAlarmsDoNotConflict() {
    const std::vector<Event> events = {
        makeEvent(1, "A", "2026-10-16T09:00:00Z", "2026-10-16T10:00:00Z"),
        makeEvent(2, "B", "2026-10-16T10:00:00Z", "2026-10-16T11:00:00Z"),
        makeEvent(3, "Zero", "2026-10-16T10:00:00Z", "2026-10-16T10:00:00Z"),
        makeEvent(4, "Reminder", "2026-10-16T09:00:00Z", "2026-10-16T11:00:00Z",
                  EventType::Alarm),
        makeEvent(5, "Also reminder", "2026-10-16T09:30:00Z", "2026-10-16T09:45:00Z",
                  EventType::Alarm),
    };
    assert(findConflicts(events).empty());
    assert(findConflicts({}).empty());
}

void testSleepCanConflictWhenPassed() {
    // Visibility is the caller's filter; `findConflicts` sees what it is given.
    const std::vector<Event> events = {
        makeEvent(1, "Event", "2026-10-16T23:00:00Z", "2026-10-16T23:30:00Z"),
        makeEvent(2, "Sleep", "2026-10-16T22:00:00Z", "2026-10-17T08:00:00Z",
                  EventType::Sleep),
    };
    const std::vector<Conflict> conflicts = findConflicts(events);
    assert(conflicts.size() == 1);
    expect(conflicts[0], 2, "Sleep", 1, "Event", "2026-10-16T23:00:00Z",
           "2026-10-16T23:30:00Z");
}

} // namespace

int main() {
    testPairwiseOverlaps();
    testTripleOverlapIsThreePairs();
    testTouchingZeroLengthAndAlarmsDoNotConflict();
    testSleepCanConflictWhenPassed();
    std::printf("conflict_test: all assertions passed\n");
    return 0;
}
