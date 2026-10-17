#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "klend_ario/event.hh"
#include "week_store.hh"

using namespace klend_ario;
using namespace klend_ario::gui;

// 2026-10-17T15:00:00Z — Saturday afternoon, 10:00 in Lima.
static constexpr long long kSaturdayAfternoon = 1792249200;

static Event makeEvent(const char *title, long long start, long long end, bool hidden = false) {
    Event event;
    event.title = title;
    event.startUtc = start;
    event.endUtc = end;
    event.hidden = hidden;
    return event;
}

static void testWeekLabel() {
    LocalWeek week;
    for (int i = 0; i < 7; ++i) {
        LocalDay &day = week.days[static_cast<size_t>(i)];
        day.year = 2026;
        day.month = 10;
        day.day = 11 + i;
        day.weekday = i;
    }
    assert(weekLabelFor(week) == "Week 42 \xC2\xB7 Oct 2026");

    // A week that contains 1 January is week 1 of the NEW year, even though it
    // starts in December: Dec 27 2026 .. Jan 2 2027.
    LocalWeek newYear;
    for (int i = 0; i < 7; ++i) {
        LocalDay &day = newYear.days[static_cast<size_t>(i)];
        day.year = (i < 5) ? 2026 : 2027;
        day.month = (i < 5) ? 12 : 1;
        day.day = (i < 5) ? 27 + i : i - 4; // 27..31, then 1..2
        day.weekday = i;
    }
    assert(newYear.days[6].month == 1 && newYear.days[6].day == 2);
    assert(weekLabelFor(newYear) == "Week 1 \xC2\xB7 Jan 2027");
}

static void testLoadIsOneVisibleWeek() {
    auto db = Db::openMemory();
    assert(db.ok());

    // Inside the Lima week (Sun Oct 11 05:00Z .. Sun Oct 18 05:00Z).
    assert(db.value().insertEvent(makeEvent("Inside", 1792200000, 1792203600)).ok());
    // Hidden rows belong to the data, not the view: the QUERY filters them.
    assert(db.value()
               .insertEvent(makeEvent("Sleep", 1792220000, 1792230000, true))
               .ok());
    // Next week: outside the range entirely.
    assert(db.value().insertEvent(makeEvent("Next week", 1792300000, 1792303600)).ok());

    auto visible = loadWeekModel(db.value(), kSaturdayAfternoon, kSaturdayAfternoon, Visibility::Visible);
    assert(visible.ok());
    assert(visible.value().events.size() == 1);
    assert(visible.value().events[0].title == "Inside");
    assert(visible.value().weekLabel == "Week 42 \xC2\xB7 Oct 2026");

    // Toggling hidden is a query change, not a scene change.
    auto all = loadWeekModel(db.value(), kSaturdayAfternoon, kSaturdayAfternoon, Visibility::Any);
    assert(all.ok());
    assert(all.value().events.size() == 2);

    // Today and the day boundaries come from the local week, not from UTC.
    const WeekModel &model = visible.value();
    assert(!model.days[0].isToday && model.days[6].isToday);
    assert(model.days[0].day.startUtc == 1791694800); // 2026-10-11T05:00:00Z
    assert(model.days[6].day.endUtc == model.days[6].day.startUtc + 86400);
}

static void testHolidayAccentComesFromTheLocalDate() {
    auto db = Db::openMemory();
    assert(db.ok());
    // 2026-07-28T15:00Z is 10:00 in Lima on Fiestas Patrias.
    auto model = loadWeekModel(db.value(), 1785250800, 1785250800, Visibility::Visible);
    assert(model.ok());
    // Week Sun Jul 26 .. Sat Aug 1; Tuesday the 28th is the holiday.
    assert(model.value().days[0].day.day == 26);
    assert(model.value().days[2].day.day == 28);
    assert(!model.value().days[2].holiday.empty());
    assert(model.value().days[1].holiday.empty());
}

int main() {
    // The local week is the whole point of this suite; pin the zone.
    setenv("TZ", "America/Lima", 1);
    tzset();

    testWeekLabel();
    testLoadIsOneVisibleWeek();
    testHolidayAccentComesFromTheLocalDate();
    std::printf("week_store_test: all assertions passed\n");
    return 0;
}
