#undef NDEBUG

#include <cassert>
#include <cstdio>
#include <string>

#include "klend_ario/holidays.hh"

namespace {

using klend_ario::holidays::Date;
using klend_ario::holidays::Holiday;

void testKnownPeruCalendarThroughCore() {
    const auto calendar = klend_ario::holidaysFor("PE", 2026);
    assert(calendar.ok());
    const std::vector<Holiday> &holidays = calendar.value();
    assert(holidays.size() == 16);
    assert(holidays.front().name == "Año Nuevo");
    assert(holidays.back().name == "Navidad del Señor");
    assert(holidays.front().date == Date::create(2026, 1, 1));
    assert(holidays.back().date == Date::create(2026, 12, 25));
}

void testLookupIsCaseInsensitiveAndTrimmed() {
    const auto calendar = klend_ario::holidaysFor(" pe ", 2026);
    assert(calendar.ok());
    assert(calendar.value().size() == 16);
}

void testIsHolidayAndDiaNoLaborable() {
    const auto feriado = klend_ario::isHoliday("PE", Date::create(2026, 7, 28));
    assert(feriado.ok());
    assert(feriado.value());

    // A decreto supremo "día no laborable" is not a feriado.
    const auto bridge = klend_ario::isHoliday("PE", Date::create(2026, 7, 27));
    assert(bridge.ok());
    assert(!bridge.value());
}

void testHolidayOnDistinguishesEmptyFromUnknownCountry() {
    const auto fiestas = klend_ario::holidayOn("PE", Date::create(2026, 7, 29));
    assert(fiestas.ok());
    assert(fiestas.value().has_value());
    assert(fiestas.value()->name == "Fiestas Patrias");

    // Easter Sunday is a valid date with no holiday: Ok(nullopt), not an error.
    const auto easter = klend_ario::holidayOn("PE", Date::create(2026, 4, 5));
    assert(easter.ok());
    assert(!easter.value().has_value());

    // Unknown country is an error.
    const auto unknown = klend_ario::holidayOn("XX", Date::create(2026, 7, 29));
    assert(!unknown.ok());
    assert(unknown.error().message.find("XX") != std::string::npos);
}

void testUnknownCountryIsAnError() {
    const auto unknown = klend_ario::holidaysFor("XX", 2026);
    assert(!unknown.ok());
    assert(!unknown.error().message.empty());

    const auto invalid = klend_ario::holidaysFor("PERU", 2026);
    assert(!invalid.ok());

    // A known country with an out-of-range year is a successful empty answer.
    const auto empty = klend_ario::holidaysFor("PE", 0);
    assert(empty.ok());
    assert(empty.value().empty());
}

} // namespace

int main() {
    testKnownPeruCalendarThroughCore();
    testLookupIsCaseInsensitiveAndTrimmed();
    testIsHolidayAndDiaNoLaborable();
    testHolidayOnDistinguishesEmptyFromUnknownCountry();
    testUnknownCountryIsAnError();

    std::printf("holidays_test: all assertions passed\n");
    return 0;
}
