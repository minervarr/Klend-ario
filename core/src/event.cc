#include "klend_ario/event.hh"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace klend_ario {
namespace {

// ---------------------------------------------------------------------------
// Howard Hinnant's civil-calendar algorithms. Integer math only — no <ctime>,
// no timezone database consulted behind our back — so the same input gives the
// same answer in every zone and can be asserted EXACTLY. Calendar arithmetic
// is a place where "almost right" is invisible and wrong.
// http://howardhinnant.github.io/date_algorithms.html
// ---------------------------------------------------------------------------

// Days since 1970-01-01 for a proleptic-Gregorian y-m-d.
long long daysFromCivil(long long y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);            // [0, 399]
    const unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u; // [0, 365]
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;        // [0, 146096]
    return era * 146097LL + static_cast<long long>(doe) - 719468LL;
}

// Inverse of daysFromCivil().
void civilFromDays(long long z, long long &y, unsigned &m, unsigned &d) {
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);         // [0, 146096]
    const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u; // [0, 399]
    const long long yr = static_cast<long long>(yoe) + era * 400;
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);      // [0, 365]
    const unsigned mp = (5u * doy + 2u) / 153u;                          // [0, 11]
    d = doy - (153u * mp + 2u) / 5u + 1u;                                 // [1, 31]
    m = (mp < 10u) ? mp + 3u : mp - 9u;                                  // [1, 12]
    y = yr + (m <= 2 ? 1 : 0);
}

// Floor division/modulo for negative epoch-second inputs (dates before 1970).
long long floorDiv(long long a, long long b) {
    const long long q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}
long long floorMod(long long a, long long b) {
    return a - floorDiv(a, b) * b;
}

// Reads exactly `n` decimal digits at `pos`, rejecting anything else.
bool readDigits(std::string_view text, size_t pos, size_t n, long long &out) {
    if (pos + n > text.size()) {
        return false;
    }
    long long value = 0;
    for (size_t i = 0; i < n; ++i) {
        const char c = text[pos + i];
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + (c - '0');
    }
    out = value;
    return true;
}

int clampTo100(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 100) {
        return 100;
    }
    return value;
}

} // namespace

const char *eventTypeName(EventType type) {
    switch (type) {
    case EventType::Event:
        return "event";
    case EventType::Sleep:
        return "sleep";
    case EventType::Wake:
        return "wake";
    case EventType::Alarm:
        return "alarm";
    }
    return "event";
}

bool eventTypeFromName(std::string_view name, EventType &out) {
    if (name == "event") {
        out = EventType::Event;
        return true;
    }
    if (name == "sleep") {
        out = EventType::Sleep;
        return true;
    }
    if (name == "wake") {
        out = EventType::Wake;
        return true;
    }
    if (name == "alarm") {
        out = EventType::Alarm;
        return true;
    }
    return false;
}

std::optional<long long> parseIso8601(std::string_view text) {
    // Shortest accepted form is "YYYY-MM-DDTHH:MMZ" (16 characters).
    if (text.size() < 16) {
        return std::nullopt;
    }
    long long year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (!readDigits(text, 0, 4, year) || text[4] != '-' || !readDigits(text, 5, 2, month) ||
        text[7] != '-' || !readDigits(text, 8, 2, day) || text[10] != 'T' ||
        !readDigits(text, 11, 2, hour) || text[13] != ':' || !readDigits(text, 14, 2, minute)) {
        return std::nullopt;
    }

    size_t pos = 16;
    if (text.size() >= 19 && text[16] == ':') {
        if (!readDigits(text, 17, 2, second)) {
            return std::nullopt;
        }
        pos = 19;
    }
    // UTC only for now: a trailing 'Z'. (Offsets are a deliberate later step,
    // not an oversight — see essentialAgent/MEMORY.md.)
    if (text.size() != pos + 1 || text[pos] != 'Z') {
        return std::nullopt;
    }

    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 ||
        second > 59) {
        return std::nullopt;
    }

    const long long days = daysFromCivil(year, static_cast<unsigned>(month),
                                         static_cast<unsigned>(day));
    // Round-trip catches impossible dates like 2026-02-31: the algorithms
    // normalise, so a mismatch means the input was not a real day.
    long long roundY = 0;
    unsigned roundM = 0, roundD = 0;
    civilFromDays(days, roundY, roundM, roundD);
    if (roundY != year || roundM != static_cast<unsigned>(month) ||
        roundD != static_cast<unsigned>(day)) {
        return std::nullopt;
    }

    return days * 86400LL + hour * 3600LL + minute * 60LL + second;
}

std::string formatIso8601(long long utcSeconds) {
    const long long days = floorDiv(utcSeconds, 86400);
    const long long rem = floorMod(utcSeconds, 86400);
    long long year = 0;
    unsigned month = 0, day = 0;
    civilFromDays(days, year, month, day);

    const long long hour = rem / 3600;
    const long long minute = (rem % 3600) / 60;
    const long long second = rem % 60;

    // Room for the widest field widths a long long year could produce; the
    // formatter never truncates.
    char buffer[80];
    std::snprintf(buffer, sizeof(buffer), "%04lld-%02u-%02uT%02lld:%02lld:%02lldZ", year,
                  month, day, hour, minute, second);
    return std::string(buffer);
}

long long utcNowSeconds() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

double hueForImportance(int importance) {
    const double t = clampTo100(importance) / 100.0;
    return kImportanceLowHue + (kImportanceHighHue - kImportanceLowHue) * t;
}

double borderFactorForUrgency(int urgency) { return clampTo100(urgency) / 100.0; }

EventStyle styleFor(const Event &event) {
    EventStyle style;
    style.hue = hueForImportance(event.importance);
    style.borderFactor = borderFactorForUrgency(event.urgency);
    return style;
}

namespace {

// HSL -> RGB with the fixed saturation/lightness above (kStyleSaturation,
// kStyleLightness). Standard formula, hue in degrees, wrapped into [0, 360).
Rgb hslToRgb(double hue, double saturation, double lightness) {
    hue = std::fmod(hue, 360.0);
    if (hue < 0.0) {
        hue += 360.0;
    }
    const double c = (1.0 - std::fabs(2.0 * lightness - 1.0)) * saturation;
    const double hp = hue / 60.0;
    const double x = c * (1.0 - std::fabs(std::fmod(hp, 2.0) - 1.0));
    const double m = lightness - c / 2.0;

    double r = 0.0, g = 0.0, b = 0.0;
    if (hp < 1.0) {
        r = c, g = x;
    } else if (hp < 2.0) {
        r = x, g = c;
    } else if (hp < 3.0) {
        g = c, b = x;
    } else if (hp < 4.0) {
        g = x, b = c;
    } else if (hp < 5.0) {
        r = x, b = c;
    } else {
        r = c, b = x;
    }

    auto channel = [m](double v) {
        return static_cast<unsigned char>(std::lround(std::clamp(v + m, 0.0, 1.0) * 255.0));
    };
    return Rgb{channel(r), channel(g), channel(b)};
}

} // namespace

Rgb rgbForStyle(const EventStyle &style) {
    return hslToRgb(style.hue, kStyleSaturation, kStyleLightness);
}

Rgb textColorOn(Rgb fill) {
    // sRGB -> linear, then Rec.709 luminance. Threshold is the midpoint between
    // "white label is comfortable" and "it is not": the yellow-green part of the
    // importance sweep lands above it and gets dark text; violet and red stay
    // below and keep white.
    auto linear = [](unsigned char v) {
        const double s = v / 255.0;
        return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    const double luminance =
        0.2126 * linear(fill.r) + 0.7152 * linear(fill.g) + 0.0722 * linear(fill.b);
    return luminance > 0.30 ? Rgb{18, 18, 22} : Rgb{247, 247, 250};
}

int weekdayUtc(long long utcSeconds) {
    // 1970-01-01 was a Thursday, which is 4 with Sunday == 0.
    const long long days = floorDiv(utcSeconds, 86400);
    return static_cast<int>(floorMod(days + 4, 7));
}

long long startOfWeekUtc(long long utcSeconds) {
    const long long days = floorDiv(utcSeconds, 86400);
    return (days - weekdayUtc(utcSeconds)) * 86400;
}

std::vector<long long> alarmTriggersUtc(const Event &event) {
    std::vector<long long> triggers;
    if (event.alarmOffset != 0) {
        triggers.push_back(event.startUtc - event.alarmOffset);
    }
    triggers.push_back(event.startUtc);
    return triggers;
}

WeekOfYear weekOfYearUtc(long long utcSeconds) {
    const long long days = floorDiv(utcSeconds, 86400);
    long long year = 0;
    unsigned month = 0, day = 0;
    civilFromDays(days, year, month, day);
    return weekOfYearFromCivilDate(static_cast<int>(year), static_cast<int>(month),
                                   static_cast<int>(day));
}

WeekOfYear weekOfYearFromCivilDate(int year, int month, int day) {
    const long long ymd = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    const long long jan1 = daysFromCivil(year, 1, 1);
    const int jan1Weekday = static_cast<int>(floorMod(jan1 + 4, 7)); // 0 = Sunday
    const long long dayOfYear = ymd - jan1;                          // 0-based

    WeekOfYear counter;
    counter.year = year;
    // Week 1 holds 1 January; each following Sunday opens the next week. The
    // integer division truncates, which is exactly the "first Sunday on or
    // after 1 January" boundary.
    counter.week = static_cast<int>((dayOfYear + jan1Weekday) / 7) + 1;
    return counter;
}

} // namespace klend_ario
