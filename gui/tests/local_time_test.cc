#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "local_time.hh"

using namespace klend_ario::gui;

// The test owns the timezone. local_time is the only file that asks the OS, so
// this is the only suite whose answer depends on it — pin it and the assertions
// are exact instead of machine-dependent.
static void useZone(const char *zone) {
    setenv("TZ", zone, 1);
    tzset();
}

// 2026-10-17T15:00:00Z
static constexpr long long kSaturdayAfternoon = 1792249200;

static void testLimaIsUtcMinusFive() {
    useZone("America/Lima"); // UTC-5 all year: no DST, the product's home zone

    const LocalDay day = localDayContaining(kSaturdayAfternoon);
    assert(day.year == 2026 && day.month == 10 && day.day == 17);
    assert(day.weekday == 6); // Saturday, 0 = Sunday
    assert(day.startUtc == 1792213200); // 2026-10-17T05:00:00Z
    assert(day.endUtc - day.startUtc == 86400);

    // 15:00Z is 10:00 in Lima, in 24-hour form.
    assert(formatClockLocal(kSaturdayAfternoon) == "10:00");
    assert(formatClockLocal(1792213200) == "00:00");
    assert(formatClockLocal(1792213200 + 13 * 3600 + 5 * 60) == "13:05");
    assert(formatDayNumber(day) == "17");
    assert(formatMonthShort(day) == "Oct");
}

static void testLimaWeekStartsOnLocalSunday() {
    useZone("America/Lima");
    const LocalWeek week = localWeekContaining(kSaturdayAfternoon);
    assert(week.days[0].year == 2026 && week.days[0].month == 10 && week.days[0].day == 11);
    assert(week.days[0].weekday == 0);
    assert(week.days[0].startUtc == 1791694800); // 2026-10-11T05:00:00Z
    assert(week.days[6].weekday == 6 && week.days[6].day == 17);
    for (const LocalDay &day : week.days) {
        assert(day.endUtc - day.startUtc == 86400);
    }

    // The LOCAL Sunday is already Monday in UTC. That off-by-one is why the week
    // counter takes a civil date, not an instant.
    const LocalWeek monday = localWeekContaining(week.days[1].startUtc);
    assert(monday.days[0].day == 11);
}

static void testBerlinSpringForwardIsTwentyThreeHours() {
    useZone("Europe/Berlin"); // 2026-03-29: clocks jump 02:00 -> 03:00
    const LocalDay day = localDayContaining(1774785600); // 12:00Z
    assert(day.year == 2026 && day.month == 3 && day.day == 29);
    assert(day.weekday == 0); // Sunday
    assert(day.startUtc == 1774738800); // 2026-03-28T23:00:00Z
    assert(day.endUtc == 1774821600);   // 2026-03-29T22:00:00Z
    assert(day.endUtc - day.startUtc == 82800); // 23 hours

    // The week containing it starts on that same Sunday and only that day is short.
    const LocalWeek week = localWeekContaining(1774785600);
    assert(week.days[0].day == 29);
    assert(week.days[0].endUtc - week.days[0].startUtc == 82800);
    assert(week.days[1].endUtc - week.days[1].startUtc == 86400);
}

static void testBerlinFallBackIsTwentyFiveHours() {
    useZone("Europe/Berlin"); // 2026-10-25: clocks repeat 02:00 -> 03:00
    const LocalDay day = localDayContaining(1792929600); // 12:00Z
    assert(day.year == 2026 && day.month == 10 && day.day == 25);
    assert(day.startUtc == 1792879200); // 2026-10-24T22:00:00Z
    assert(day.endUtc == 1792969200);   // 2026-10-25T23:00:00Z
    assert(day.endUtc - day.startUtc == 90000); // 25 hours
}

static void testWeekCrossesMonthBoundary() {
    useZone("UTC0");
    // 2026-09-30 is a Wednesday; its week runs Sun Sep 27 .. Sat Oct 3.
    const LocalWeek week = localWeekContaining(1790769600); // 2026-09-30T12:00Z
    assert(week.days[0].month == 9 && week.days[0].day == 27);
    assert(week.days[6].month == 10 && week.days[6].day == 3);
    assert(week.days[6].weekday == 6);
}

int main() {
    testLimaIsUtcMinusFive();
    testLimaWeekStartsOnLocalSunday();
    testBerlinSpringForwardIsTwentyThreeHours();
    testBerlinFallBackIsTwentyFiveHours();
    testWeekCrossesMonthBoundary();
    std::printf("local_time_test: all assertions passed\n");
    return 0;
}
