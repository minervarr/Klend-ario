#undef NDEBUG
#include <cassert>
#include <cstdio>

#include "klend_ario/event.hh"

using namespace klend_ario;

static void testIsoRoundTrip() {
    const auto parsed = parseIso8601("2026-10-16T09:00:00Z");
    assert(parsed.has_value());
    assert(formatIso8601(*parsed) == "2026-10-16T09:00:00Z");
}

static void testEpochAnchor() {
    // The epoch is the zero point, and 2000-01-01 is exactly 10957 days later.
    // Asserting the anchor against a KNOWN day count is what catches an error
    // in the civil-date arithmetic that a round-trip alone would not.
    assert(parseIso8601("1970-01-01T00:00:00Z") == 0);
    assert(parseIso8601("2000-01-01T00:00:00Z") == 10957LL * 86400);
}

static void testSecondsOptional() {
    assert(parseIso8601("2026-10-16T09:00Z") == parseIso8601("2026-10-16T09:00:00Z"));
}

static void testRejectsGarbage() {
    assert(!parseIso8601("").has_value());
    assert(!parseIso8601("2026-10-16").has_value());                 // no time
    assert(!parseIso8601("2026-13-01T00:00:00Z").has_value());       // month 13
    assert(!parseIso8601("2026-02-30T00:00:00Z").has_value());       // day that never was
    assert(!parseIso8601("2026-10-16T09:00:00").has_value());        // no trailing Z
    assert(!parseIso8601("2026-10-16T25:00:00Z").has_value());       // hour 25
    assert(!parseIso8601("2026-10-16T09:00:00+02:00").has_value());  // offsets are later
}

static void testLeapDay() {
    assert(parseIso8601("2024-02-29T00:00:00Z").has_value());
    assert(!parseIso8601("2023-02-29T00:00:00Z").has_value());
}

static void testPreEpoch() {
    // One second before the epoch: negative, and the day/second split must
    // FLOOR rather than truncate toward zero, or the date comes out a day off.
    const auto parsed = parseIso8601("1969-12-31T23:59:59Z");
    assert(parsed.has_value());
    assert(*parsed == -1);
    assert(formatIso8601(-1) == "1969-12-31T23:59:59Z");
}

static void testTypeNames() {
    EventType type = EventType::Event;
    assert(eventTypeFromName("sleep", type) && type == EventType::Sleep);
    assert(eventTypeFromName("wake", type) && type == EventType::Wake);
    assert(eventTypeFromName("alarm", type) && type == EventType::Alarm);
    assert(eventTypeFromName("event", type) && type == EventType::Event);
    assert(!eventTypeFromName("nope", type));
    for (EventType value : {EventType::Event, EventType::Sleep, EventType::Wake, EventType::Alarm}) {
        EventType back = EventType::Event;
        assert(eventTypeFromName(eventTypeName(value), back) && back == value);
    }
}

int main() {
    testIsoRoundTrip();
    testEpochAnchor();
    testSecondsOptional();
    testRejectsGarbage();
    testLeapDay();
    testPreEpoch();
    testTypeNames();
    std::printf("event_test: all assertions passed\n");
    return 0;
}
