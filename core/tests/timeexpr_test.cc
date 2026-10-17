#undef NDEBUG
#include <cassert>
#include <cstdio>

#include "klend_ario/event.hh"
#include "klend_ario/timeexpr.hh"

using namespace klend_ario;

namespace {

// The note's own reference time: 2026-10-16 is a Friday.
long long noteNow() {
    const auto now = parseIso8601("2026-10-16T14:44:00Z");
    assert(now.has_value());
    return *now;
}

long long instant(const char *text) {
    const auto value = parseIso8601(text);
    assert(value.has_value());
    return *value;
}

void testAbsolute() {
    const long long now = noteNow();
    assert(parseInstantExpr("2026-10-16T09:00:00Z", now) == instant("2026-10-16T09:00:00Z"));
    assert(parseInstantExpr("2026-10-16T09:00Z", now) == instant("2026-10-16T09:00:00Z"));
    // A date-only form is UTC midnight; it is also what makes `--from` pleasant.
    assert(parseInstantExpr("2026-10-16", now) == instant("2026-10-16T00:00:00Z"));
    assert(!parseInstantExpr("2026-10-16T09:00", now).has_value()); // no Z => not UTC
}

void testKeywords() {
    const long long now = noteNow();
    assert(parseInstantExpr("now", now) == now);
    assert(parseInstantExpr("today", now) == instant("2026-10-16T00:00:00Z"));
    assert(parseInstantExpr("tomorrow", now) == instant("2026-10-17T00:00:00Z"));
    assert(parseInstantExpr("yesterday", now) == instant("2026-10-15T00:00:00Z"));
    assert(parseInstantExpr("week", now) == instant("2026-10-11T00:00:00Z"));
    assert(parseInstantExpr("next-week", now) == instant("2026-10-18T00:00:00Z"));
    assert(parseInstantExpr("last-week", now) == instant("2026-10-04T00:00:00Z"));

    // Case does not matter for words.
    assert(parseInstantExpr("TODAY", now) == instant("2026-10-16T00:00:00Z"));

    // Today counts: a Friday typed on a Friday is today, and Sunday is the
    // Sunday that opens next week.
    assert(parseInstantExpr("friday", now) == instant("2026-10-16T00:00:00Z"));
    assert(parseInstantExpr("sunday", now) == instant("2026-10-18T00:00:00Z"));
    assert(parseInstantExpr("monday", now) == instant("2026-10-19T00:00:00Z"));
    assert(parseInstantExpr("thursday", now) == instant("2026-10-22T00:00:00Z"));
}

void testRelative() {
    const long long now = noteNow();
    assert(parseInstantExpr("+90m", now) == now + 5400);
    assert(parseInstantExpr("-1d", now) == now - 86400);
    assert(parseInstantExpr("today+2h", now) == instant("2026-10-16T02:00:00Z"));
    assert(parseInstantExpr("week-1d", now) == instant("2026-10-10T00:00:00Z"));
    assert(parseInstantExpr("next-week+1d", now) == instant("2026-10-19T00:00:00Z"));
    assert(parseInstantExpr("tomorrow-30m", now) == instant("2026-10-16T23:30:00Z"));
    assert(parseInstantExpr("+0s", now) == now);
}

void testDurations() {
    assert(parseDurationExpr("90m") == 5400);
    assert(parseDurationExpr("2h") == 7200);
    assert(parseDurationExpr("1h30m") == 5400);
    assert(parseDurationExpr("1d") == 86400);
    assert(parseDurationExpr("1w") == 604800);
    assert(parseDurationExpr("604800") == 604800); // bare number = seconds
    assert(parseDurationExpr("1h30") == 3600 + 30); // last group stays explicit
    assert(parseDurationExpr("0") == 0);
    assert(parseDurationExpr(" 15m ") == 900);
}

void testRejectsGarbage() {
    const long long now = noteNow();
    assert(!parseInstantExpr("", now).has_value());
    assert(!parseInstantExpr("   ", now).has_value());
    assert(!parseInstantExpr("banana", now).has_value());
    assert(!parseInstantExpr("fridayy", now).has_value());
    assert(!parseInstantExpr("2026-13-01", now).has_value());
    assert(!parseInstantExpr("+", now).has_value());
    assert(!parseInstantExpr("-", now).has_value());
    assert(!parseInstantExpr("today+", now).has_value());
    assert(!parseInstantExpr("today+2x", now).has_value());

    assert(!parseDurationExpr("").has_value());
    assert(!parseDurationExpr("1x").has_value());
    assert(!parseDurationExpr("1h30x").has_value());
    assert(!parseDurationExpr("h").has_value());
    assert(!parseDurationExpr("999999999999999999999").has_value());
}

void testPreEpoch() {
    // One second before the epoch is still 1969-12-31, and "today" is its
    // midnight — the floor, not a truncation toward zero.
    assert(parseInstantExpr("today", -1) == instant("1969-12-31T00:00:00Z"));
    assert(parseInstantExpr("tomorrow", -1) == instant("1970-01-01T00:00:00Z"));
    // 1969-12-31 was a Wednesday; the next Thursday is 1970-01-01.
    assert(parseInstantExpr("thursday", -1) == instant("1970-01-01T00:00:00Z"));
}

} // namespace

int main() {
    testAbsolute();
    testKeywords();
    testRelative();
    testDurations();
    testRejectsGarbage();
    testPreEpoch();
    std::printf("timeexpr_test: all assertions passed\n");
    return 0;
}
