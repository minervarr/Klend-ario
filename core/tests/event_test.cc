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

static void testSecondsOfDayModel() {
    // The 2026-10-16 notes' worked example, pinned as a vector. A civil day is
    // HALF-OPEN [00:00:00, next 00:00:00): the length is 86,400 s, while
    // 23:59:59 is the last named second at 86,399 s after midnight.
    const auto midnight = parseIso8601("2026-10-16T00:00:00Z");
    const auto late = parseIso8601("2026-10-16T22:13:20Z");
    const auto halfPast = parseIso8601("2026-10-16T22:30:00Z");
    const auto lastSecond = parseIso8601("2026-10-16T23:59:59Z");
    const auto nextMidnight = parseIso8601("2026-10-17T00:00:00Z");
    assert(midnight && late && halfPast && lastSecond && nextMidnight);

    assert(*late - *midnight == 80000);       // 22:13:20
    assert(*halfPast - *midnight == 81000);   // 22:30:00
    assert(*halfPast - *late == 1000);        // 00:16:40
    assert(*lastSecond - *midnight == 86399); // last named second
    assert(*nextMidnight - *midnight == 86400); // the day's LENGTH
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

static void testImportanceHue() {
    // The anchors are the ends of the Eisenhower colours: violet (low) at 0,
    // red (high) at 100.
    assert(hueForImportance(0) == kImportanceLowHue);
    assert(hueForImportance(100) == kImportanceHighHue);
    assert(hueForImportance(50) == 135.0); // straight interpolation
    // Out of range is CLAMPED, never wrapped back around the colour wheel.
    assert(hueForImportance(-10) == hueForImportance(0));
    assert(hueForImportance(1000) == hueForImportance(100));
    // Monotone from violet down to red.
    assert(hueForImportance(25) > hueForImportance(75));
}

static void testUrgencyBorder() {
    assert(borderFactorForUrgency(0) == 0.0);
    assert(borderFactorForUrgency(50) == 0.5);
    assert(borderFactorForUrgency(100) == 1.0);
    assert(borderFactorForUrgency(999) == 1.0);
}

static void testStyleFromEvent() {
    Event e;
    e.importance = 100;
    e.urgency = 0;
    const EventStyle style = styleFor(e);
    assert(style.hue == 0.0);
    assert(style.borderFactor == 0.0);
}

static void testWeekdayAndWeekStart() {
    // 1970-01-01 (the epoch) was a Thursday, which is 4 with Sunday == 0.
    assert(weekdayUtc(0) == 4);
    // 2026-10-16 is a Friday.
    const auto friday = parseIso8601("2026-10-16T12:00:00Z");
    assert(friday.has_value());
    assert(weekdayUtc(*friday) == 5);
    // Its week begins on the Sunday before it: 2026-10-11, 00:00 UTC.
    const long long sunday = *parseIso8601("2026-10-11T00:00:00Z");
    assert(startOfWeekUtc(*friday) == sunday);
    // A Sunday noon is its own week start.
    assert(startOfWeekUtc(*parseIso8601("2026-10-11T12:00:00Z")) == sunday);
    // The Saturday before belongs to the PREVIOUS week (2026-10-04).
    assert(startOfWeekUtc(*parseIso8601("2026-10-10T23:59:59Z")) ==
           *parseIso8601("2026-10-04T00:00:00Z"));
}

static void testAlarmTriggers() {
    Event e;
    e.startUtc = *parseIso8601("2026-10-16T09:00:00Z");
    e.alarmOffset = 600; // ten minutes before
    const std::vector<long long> both = alarmTriggersUtc(e);
    assert(both.size() == 2);
    assert(both[0] == e.startUtc - 600); // T-offset fires first
    assert(both[1] == e.startUtc);       // then T=0

    e.alarmOffset = 0;
    const std::vector<long long> only = alarmTriggersUtc(e);
    assert(only.size() == 1);
    assert(only[0] == e.startUtc);
}

static void testWeekCounter() {
    // 2026-01-01 was a Thursday, so week 1 is Jan 1..Jan 3 and the next Sunday
    // (Jan 4) opens week 2.
    const WeekOfYear w1 = weekOfYearUtc(*parseIso8601("2026-01-01T00:00:00Z"));
    assert(w1.year == 2026 && w1.week == 1);
    assert(weekOfYearUtc(*parseIso8601("2026-01-03T23:59:59Z")).week == 1);
    assert(weekOfYearUtc(*parseIso8601("2026-01-04T00:00:00Z")).week == 2);
    // A mid-year date: 2026-10-16 is day 289 (0-based 288); (288 + 4)/7 + 1 = 42.
    const WeekOfYear mid = weekOfYearUtc(*parseIso8601("2026-10-16T12:00:00Z"));
    assert(mid.year == 2026 && mid.week == 42);
    // A late-December date stays in its own (old) year.
    assert(weekOfYearUtc(*parseIso8601("2025-12-31T12:00:00Z")).year == 2025);
}

static void testWeekCounterFromCivilDate() {
    // The civil-date form must agree with the UTC form for a UTC instant ...
    assert(weekOfYearFromCivilDate(2026, 1, 1).week == 1);
    assert(weekOfYearFromCivilDate(2026, 1, 4).week == 2);
    assert(weekOfYearFromCivilDate(2026, 10, 16).week == 42);
    assert(weekOfYearFromCivilDate(2025, 12, 31).year == 2025);

    // ... and it is the form a LOCAL week needs. 2026-10-18 is the Sunday that
    // opens week 43; at UTC+09:00 its local midnight is 2026-10-17T15:00:00Z,
    // which the UTC week counter still calls week 42 (Saturday). That off-by-one
    // is exactly why the GUI resolves the local date itself.
    const long long localSundayMidnight = *parseIso8601("2026-10-17T15:00:00Z");
    assert(weekOfYearUtc(localSundayMidnight).week == 42);
    assert(weekOfYearFromCivilDate(2026, 10, 18).week == 43);
}

static void testStyleToRgb() {
    // importance 0 is violet, importance 100 is red: at the fixed
    // saturation/lightness these are the exact bytes the view draws.
    const Rgb violet = rgbForStyle(EventStyle{kImportanceLowHue, 0.0});
    assert(violet.r == 117 && violet.g == 33 && violet.b == 202);
    const Rgb red = rgbForStyle(EventStyle{kImportanceHighHue, 1.0});
    assert(red.r == 202 && red.g == 33 && red.b == 33);

    // Hue wraps instead of producing a different colour at 0 and 360.
    const Rgb wrapped = rgbForStyle(EventStyle{-90.0, 0.0});
    assert(wrapped.r == violet.r && wrapped.g == violet.g && wrapped.b == violet.b);

    // The label colour follows the fill's luminance: violet and red keep a
    // near-white label, the yellow-green middle switches to near-black.
    const Rgb onViolet = textColorOn(violet);
    assert(onViolet.r > 200 && onViolet.g > 200 && onViolet.b > 200);
    const Rgb onLime = textColorOn(rgbForStyle(EventStyle{90.0, 0.0}));
    assert(onLime.r < 60 && onLime.g < 60 && onLime.b < 60);

    // importance 50 sits between the endpoints: the hue travels through
    // magenta, so it must not reproduce either endpoint's colour.
    Event midEvent;
    midEvent.importance = 50;
    const Rgb mid = rgbForStyle(styleFor(midEvent));
    assert(mid.r != violet.r || mid.b != violet.b);
    assert(mid.b != red.b || mid.g != red.g);
}

int main() {
    testIsoRoundTrip();
    testEpochAnchor();
    testSecondsOptional();
    testRejectsGarbage();
    testLeapDay();
    testPreEpoch();
    testSecondsOfDayModel();
    testTypeNames();
    testImportanceHue();
    testUrgencyBorder();
    testStyleFromEvent();
    testWeekdayAndWeekStart();
    testAlarmTriggers();
    testWeekCounter();
    testWeekCounterFromCivilDate();
    testStyleToRgb();
    std::printf("event_test: all assertions passed\n");
    return 0;
}
