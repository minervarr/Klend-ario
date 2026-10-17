#include "local_time.hh"

#include <cstdio>
#include <ctime>

namespace klend_ario::gui {
namespace {

std::tm localTm(long long utcSeconds) {
    const std::time_t t = static_cast<std::time_t>(utcSeconds);
    std::tm out{};
#if defined(_WIN32)
    localtime_s(&out, &t);
#else
    localtime_r(&t, &out);
#endif
    return out;
}

// Local 00:00 for a civil date, as a UTC instant. mktime normalises overflow
// (day 32 -> the 1st of the next month), which is what lets the week walk
// backwards past a month boundary without any date arithmetic of our own.
//
// A zone that springs forward THROUGH midnight has no 00:00 that day; mktime
// returns the first instant that exists (01:00), which is the correct start of
// that civil date. The previous day's `endUtc` resolves to the same instant, so
// the day is 23 hours long instead of one day losing an hour.
long long localMidnightUtc(int year, int month, int day) {
    std::tm t{};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = 0;
    t.tm_min = 0;
    t.tm_sec = 0;
    t.tm_isdst = -1;
    return static_cast<long long>(std::mktime(&t));
}

} // namespace

LocalDay localDayContaining(long long utcSeconds) {
    const std::tm tm = localTm(utcSeconds);
    LocalDay day;
    day.year = tm.tm_year + 1900;
    day.month = tm.tm_mon + 1;
    day.day = tm.tm_mday;
    day.weekday = tm.tm_wday;
    day.startUtc = localMidnightUtc(day.year, day.month, day.day);
    day.endUtc = localMidnightUtc(day.year, day.month, day.day + 1);
    return day;
}

LocalWeek localWeekContaining(long long utcSeconds) {
    const LocalDay today = localDayContaining(utcSeconds);
    LocalWeek week;
    for (int i = 0; i < 7; ++i) {
        // Ask for the civil day `i - today.weekday` days away; mktime owns the
        // month/year rollover and the DST resolution.
        const long long midnight =
            localMidnightUtc(today.year, today.month, today.day + i - today.weekday);
        week.days[static_cast<size_t>(i)] = localDayContaining(midnight);
    }
    return week;
}

std::string formatClockLocal(long long utcSeconds) {
    const std::tm tm = localTm(utcSeconds);
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", tm.tm_hour, tm.tm_min);
    return buffer;
}

std::string formatDayNumber(const LocalDay &day) {
    char buffer[4];
    std::snprintf(buffer, sizeof(buffer), "%d", day.day);
    return buffer;
}

std::string formatMonthShort(const LocalDay &day) {
    static const char *kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const int index = (day.month >= 1 && day.month <= 12) ? day.month - 1 : 0;
    return kMonths[index];
}

} // namespace klend_ario::gui
