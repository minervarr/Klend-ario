#include "klend_ario/timeexpr.hh"

#include <cctype>
#include <limits>
#include <string>

#include "klend_ario/event.hh"

namespace klend_ario {
namespace {

std::string_view trim(std::string_view text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::string lowerAscii(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

// Overflow-safe arithmetic: expressions can be typed by anyone, so a huge
// number has to be rejected rather than silently wrap into a wrong instant.
bool addChecked(long long a, long long b, long long &out) {
    if ((b > 0 && a > std::numeric_limits<long long>::max() - b) ||
        (b < 0 && a < std::numeric_limits<long long>::min() - b)) {
        return false;
    }
    out = a + b;
    return true;
}

bool mulChecked(long long a, long long b, long long &out) {
    if (a == 0 || b == 0) {
        out = 0;
        return true;
    }
    if (a < 0 || b < 0) {
        return false; // durations are non-negative; signs are handled by the caller
    }
    if (a > std::numeric_limits<long long>::max() / b) {
        return false;
    }
    out = a * b;
    return true;
}

// 00:00 UTC of the day containing `utcSeconds`, correct before 1970 too.
long long startOfDayUtc(long long utcSeconds) {
    const long long secondsIntoDay = ((utcSeconds % 86400) + 86400) % 86400;
    return utcSeconds - secondsIntoDay;
}

bool parseDigits(std::string_view text, size_t &i, long long &out) {
    const size_t begin = i;
    long long value = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        const int digit = text[i] - '0';
        if (value > (std::numeric_limits<long long>::max() - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
        ++i;
    }
    if (i == begin) {
        return false;
    }
    out = value;
    return true;
}

long long unitSeconds(char unit) {
    switch (unit) {
    case 's':
        return 1;
    case 'm':
        return 60;
    case 'h':
        return 60 * 60;
    case 'd':
        return 24 * 60 * 60;
    case 'w':
        return 7 * 24 * 60 * 60;
    default:
        return 0;
    }
}

// The keyword half of an instant expression. Relative arithmetic is applied by
// the caller, so `today+2h` and `monday-30m` share one rule.
std::optional<long long> resolveKeyword(std::string_view text, long long nowUtc) {
    static constexpr const char *kWeekdays[] = {"sunday",   "monday", "tuesday", "wednesday",
                                                 "thursday", "friday", "saturday"};

    if (text == "now") {
        return nowUtc;
    }
    if (text == "today") {
        return startOfDayUtc(nowUtc);
    }
    if (text == "tomorrow") {
        return startOfDayUtc(nowUtc) + 86400;
    }
    if (text == "yesterday") {
        return startOfDayUtc(nowUtc) - 86400;
    }
    if (text == "week") {
        return startOfWeekUtc(nowUtc);
    }
    if (text == "next-week") {
        return startOfWeekUtc(nowUtc) + 7 * 86400;
    }
    if (text == "last-week") {
        return startOfWeekUtc(nowUtc) - 7 * 86400;
    }
    for (int day = 0; day < 7; ++day) {
        if (text == kWeekdays[day]) {
            // The next occurrence at 00:00 UTC, with today counting. A person
            // typing "monday" on a Monday means today, not a week from now.
            const int delta = (day - weekdayUtc(nowUtc) + 7) % 7;
            return startOfDayUtc(nowUtc) + static_cast<long long>(delta) * 86400;
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<long long> parseDurationExpr(std::string_view text) {
    text = trim(text);
    if (text.empty()) {
        return std::nullopt;
    }

    long long total = 0;
    size_t i = 0;
    while (i < text.size()) {
        long long value = 0;
        if (!parseDigits(text, i, value)) {
            return std::nullopt;
        }

        // A bare number is seconds; otherwise the unit must be known. There is
        // no default unit for a mixed group ("1h30" is 1h + 30s for that
        // reason: the last group is still explicit about being seconds).
        long long factor = 1;
        if (i < text.size()) {
            factor = unitSeconds(static_cast<char>(std::tolower(static_cast<unsigned char>(text[i]))));
            if (factor == 0) {
                return std::nullopt;
            }
            ++i;
        }

        long long group = 0;
        if (!mulChecked(value, factor, group)) {
            return std::nullopt;
        }
        if (!addChecked(total, group, total)) {
            return std::nullopt;
        }
    }
    return total;
}

std::optional<long long> parseInstantExpr(std::string_view text, long long nowUtc) {
    text = trim(text);
    if (text.empty()) {
        return std::nullopt;
    }

    if (const auto absolute = parseIso8601(text)) {
        return absolute;
    }
    // A date without a time is a real thing to type, and always means the UTC
    // midnight that begins that day.
    if (text.size() == 10 && text[4] == '-' && text[7] == '-') {
        std::string expanded(text);
        expanded += "T00:00:00Z";
        if (const auto dateOnly = parseIso8601(expanded)) {
            return dateOnly;
        }
    }

    const std::string keyword = lowerAscii(text);
    if (const auto resolved = resolveKeyword(keyword, nowUtc)) {
        return resolved;
    }

    // A leading sign is relative to now: +90m, -1d.
    if (text[0] == '+' || text[0] == '-') {
        const auto duration = parseDurationExpr(text.substr(1));
        if (!duration) {
            return std::nullopt;
        }
        long long result = 0;
        if (!addChecked(nowUtc, text[0] == '+' ? *duration : -*duration, result)) {
            return std::nullopt;
        }
        return result;
    }

    // Otherwise `<keyword><sign><duration>`. Scan for a sign that is followed
    // by a duration, so the hyphen inside `next-week` is not mistaken for one.
    for (size_t i = 1; i < text.size(); ++i) {
        const char c = text[i];
        const bool signedOffset =
            (c == '+' || c == '-') && (i + 1 < text.size()) && text[i + 1] >= '0' && text[i + 1] <= '9';
        if (!signedOffset) {
            continue;
        }

        const auto base = resolveKeyword(lowerAscii(text.substr(0, i)), nowUtc);
        const auto duration = parseDurationExpr(text.substr(i + 1));
        if (!base || !duration) {
            return std::nullopt;
        }
        long long result = 0;
        if (!addChecked(*base, c == '+' ? *duration : -*duration, result)) {
            return std::nullopt;
        }
        return result;
    }

    return std::nullopt;
}

} // namespace klend_ario
