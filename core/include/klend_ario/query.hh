#pragma once

#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include "klend_ario/event.hh"

// A query over `events`, as a value.
//
// The CLI's filters and the weekly view's queries must ask the same question in
// the same way, so the question is a struct in core rather than a pile of
// optional arguments at each call site. `Db::queryEvents` turns it into one
// prepared statement; user text is always bound, and the ORDER BY is chosen
// from a fixed whitelist (never interpolated).
//
// The range is half-open in both dimensions: an event is returned when
// `start_utc < toUtc` AND `end_utc > fromUtc` (essentialAgent/AGENTS.md §2.11).
// That is the same overlap rule as `listEvents`, so an event already underway is
// part of a query that starts at "now".

namespace klend_ario {

enum class EventSort {
    Start,      // ascending; the calendar's natural order
    StartDesc,  // newest first
    Importance, // most important first, then most urgent
    Urgency,    // most urgent first, then most important
    Updated,    // most recently edited first
    Title,      // A→Z, ASCII case-insensitive
};

const char *eventSortName(EventSort sort);
bool eventSortFromName(std::string_view name, EventSort &out);

enum class Visibility {
    Any,     // hidden and visible rows
    Visible, // hidden = 0
    Hidden,  // hidden = 1
};

struct EventQuery {
    // Unbounded defaults. The SQL builder omits a bound that is still at its
    // sentinel, so `search` does not force a full-range comparison and `next`
    // can use only `end_utc > now`.
    long long fromUtc = std::numeric_limits<long long>::min();
    long long toUtc = std::numeric_limits<long long>::max();

    std::string text; // literal substring of title or notes; "" = no filter
    std::optional<EventType> type;

    // Inclusive bounds; the defaults are the schema's full range.
    int minImportance = 0;
    int maxImportance = 100;
    int minUrgency = 0;
    int maxUrgency = 100;

    Visibility visibility = Visibility::Any;
    EventSort sort = EventSort::Start;

    // SQL semantics: 0 really means zero rows. A negative value means "no
    // LIMIT", which is why the default is -1 and not 0.
    int limit = -1;
    int offset = 0;
};

} // namespace klend_ario
