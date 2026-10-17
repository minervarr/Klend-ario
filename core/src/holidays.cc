#include "klend_ario/holidays.hh"

#include <string>
#include <utility>

namespace klend_ario {
namespace {

arc::Error unknownCountry(std::string_view country) {
    return arc::Error{arc::ErrorKind::Other, 0,
                      "unknown country: " + std::string(country)};
}

} // namespace

arc::Result<std::vector<holidays::Holiday>> holidaysFor(std::string_view country,
                                                        int year) {
    auto values = holidays::holidaysFor(country, year);
    if (!values) {
        return unknownCountry(country);
    }
    return std::move(*values);
}

arc::Result<bool> isHoliday(std::string_view country,
                            const holidays::Date &date) {
    auto value = holidays::isHoliday(country, date);
    if (!value) {
        return unknownCountry(country);
    }
    return *value;
}

arc::Result<std::optional<holidays::Holiday>> holidayOn(
    std::string_view country, const holidays::Date &date) {
    // The library cannot distinguish "unknown country" from "no holiday" in a
    // bare optional, so resolve the provider first and keep the two answers
    // separate at this boundary.
    if (holidays::providerFor(country) == nullptr) {
        return unknownCountry(country);
    }
    return holidays::holidayOn(country, date);
}

} // namespace klend_ario
