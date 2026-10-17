#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "arc/result.hh"
#include "klend_ario/holidays/holidays.hh"

// Holiday queries at the core boundary.
//
// The holiday library answers with std::optional (nullopt = "unknown country"
// for list/bool queries, or "no holiday today" for holidayOn). Core speaks
// arc::Result, so this adapter makes the error unambiguous:
//
//   * Err  — the country code is not registered.
//   * Ok   — the query succeeded; holidayOn() may still carry std::nullopt.
//
// No exception crosses this boundary: the library's only throwing helpers
// (Date::create / Date::addDays) are for programmer errors and are not used by
// the query path.

namespace klend_ario {

/// All holidays of `year` for `country`, or an error for an unknown country.
arc::Result<std::vector<holidays::Holiday>> holidaysFor(std::string_view country,
                                                        int year);

/// True when `date` is a holiday for `country`; an error for an unknown country.
arc::Result<bool> isHoliday(std::string_view country, const holidays::Date &date);

/// The holiday on `date`, or Ok(std::nullopt) when that day is not a holiday.
/// An unknown country is an error.
arc::Result<std::optional<holidays::Holiday>> holidayOn(
    std::string_view country, const holidays::Date &date);

} // namespace klend_ario
