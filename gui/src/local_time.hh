#pragma once

// Local civil days, resolved ONCE at the edge.
//
// The model is UTC end to end (AGENTS.md §2.10). The weekly view draws LOCAL
// days, so somebody has to answer "what is this instant's local date, and where
// does that date begin and end as a UTC instant?" — that somebody is this file,
// and nothing downstream ever asks the OS again. Layout, scene and tests work on
// the instants this returns, which is why a DST day can be asserted with a
// fabricated 23-hour day instead of a timezone fixture.
//
// It is a frontend, so <ctime> is allowed here and forbidden in core.

#include <array>
#include <string>

namespace klend_ario::gui {

struct LocalDay {
    long long startUtc = 0; // local 00:00 as a UTC instant
    long long endUtc = 0;   // the NEXT local 00:00, as a UTC instant
    int year = 1970;
    int month = 1; // 1..12
    int day = 1;   // 1..31
    int weekday = 0; // 0 = Sunday .. 6 = Saturday
};

struct LocalWeek {
    std::array<LocalDay, 7> days; // Sunday .. Saturday
};

// The local civil day containing `utcSeconds`. `endUtc - startUtc` is 86,400 on
// an ordinary day, 82,800 on a spring-forward day and 90,000 on a fall-back one.
LocalDay localDayContaining(long long utcSeconds);

// The Sunday-first local week containing `utcSeconds`. The boundary is the LOCAL
// Sunday 00:00, which is not the same instant as the UTC one — that difference is
// exactly what `weekOfYearFromCivilDate` exists for.
LocalWeek localWeekContaining(long long utcSeconds);

// Local wall clock in 24-hour form ("09:30"), for event labels. Display only:
// never parse this back into a stored value.
std::string formatClockLocal(long long utcSeconds);

// "17" — the date number a day header shows.
std::string formatDayNumber(const LocalDay &day);

// "Oct" — the month, so a week spanning two months is still unambiguous.
std::string formatMonthShort(const LocalDay &day);

} // namespace klend_ario::gui
