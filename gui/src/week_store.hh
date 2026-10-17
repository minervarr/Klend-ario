#pragma once

// Turning a database into the week the view draws.
//
// One query, seven local days, a week label and the holidays that fall on them.
// It is a separate step from the scene so the scene never opens a database and
// this never decides a pixel.
//
// `visibility` is handed to the QUERY, not applied here: "hidden" is a query
// dimension the CLI, the MCP server and the GUI already share, and a second
// filter in a frontend is how the three start to disagree.

#include <string>
#include <string_view>

#include "arc/result.hh"
#include "klend_ario/db.hh"
#include "klend_ario/query.hh"
#include "scene.hh"

namespace klend_ario::gui {

// The header text for a Sunday-first week: "Week 42 · Oct 2026".
//
// The label comes from the week's OWN numbering, not from its Sunday alone: a
// week that contains 1 January is week 1 of the NEW year (the US reading), even
// though it starts in December. Taking the counter from the December Sunday
// called that week "Week 53 · Dec 2026" and then the next week jumped to 2.
std::string weekLabelFor(const LocalWeek &week);

// The local week containing `anchorUtc`, read from `db`. `nowUtc` decides which
// column is today and where the now-line goes; passing them separately is what
// lets the view browse another week while still marking the real current day.
arc::Result<WeekModel> loadWeekModel(klend_ario::Db &db, long long anchorUtc,
                                     long long nowUtc, klend_ario::Visibility visibility,
                                     std::string_view holidayCountry = "PE");

} // namespace klend_ario::gui
