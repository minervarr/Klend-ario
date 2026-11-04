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

std::tm civilTm(int year, int month, int day, int hour, int minute, int second) {
    std::tm t{};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    t.tm_isdst = -1;
    return t;
}

bool sameCivil(const std::tm &tm, int year, int month, int day, int hour, int minute,
               int second) {
    return tm.tm_year == year - 1900 && tm.tm_mon == month - 1 && tm.tm_mday == day &&
           tm.tm_hour == hour && tm.tm_min == minute && tm.tm_sec == second;
}

// mktime WITH a forced DST opinion. The request is only meaningful if the
// resulting instant round-trips back to the same wall clock; that is how a
// nonexistent or repeated local time is detected instead of silently accepted.
bool candidateUtc(const std::tm &base, int isdst, long long &out) {
    std::tm t = base;
    t.tm_isdst = isdst;
    const std::time_t result = std::mktime(&t);
    if (result == static_cast<std::time_t>(-1)) {
        return false;
    }
    out = static_cast<long long>(result);
    return true;
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

LocalResolution localTimeToUtc(int year, int month, int day, int hour, int minute,
                               int second) {
    const std::tm base = civilTm(year, month, day, hour, minute, second);

    long long standard = 0;
    long long daylight = 0;
    const bool hasStandard = candidateUtc(base, 0, standard);
    const bool hasDaylight = candidateUtc(base, 1, daylight);
    const bool standardMatches =
        hasStandard && sameCivil(localTm(standard), year, month, day, hour, minute, second);
    const bool daylightMatches =
        hasDaylight && sameCivil(localTm(daylight), year, month, day, hour, minute, second);

    LocalResolution out;
    if (!standardMatches && !daylightMatches) {
        // Spring forward: this wall clock was skipped. mktime with isdst=-1
        // normalises it (02:30 -> 03:30); return that instant so the UI can
        // preview the shift, but mark it so saving is refused without consent.
        std::tm normalized = base;
        normalized.tm_isdst = -1;
        const std::time_t fallback = std::mktime(&normalized);
        out.utc = static_cast<long long>(fallback);
        out.nonexistent = true;
        return out;
    }

    if (standardMatches && daylightMatches) {
        // Fall back: the same wall clock happens twice. Choose the earlier
        // (the DST side) and say so; an editor that cares can offer the other.
        out.ambiguous = standard != daylight;
        out.utc = standard < daylight ? standard : daylight;
        return out;
    }

    out.utc = standardMatches ? standard : daylight;
    return out;
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
