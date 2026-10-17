#pragma once

#include <array>
#include <optional>
#include <vector>

#include "klend_ario/event.hh"

// A summary of a set of events, as a value.
//
// The caller chooses the set (a range, a type, visible/all); this function does
// not reach for a database or a clock. `nowUtc` only classifies events into
// upcoming/ongoing/past, so the same input and clock always give the same
// answer.
//
// `scheduledSeconds` is the SUM of event durations. Overlapping events
// therefore double-count; `conflictPairs` says how many such overlaps exist, so
// the two numbers are read together. `EventType::Alarm` rows count in `total`
// and `byType` but do not create conflict pairs (they are reminders, not
// blocks).

namespace klend_ario {

struct CalendarStats {
    long long total = 0;
    long long visible = 0;
    long long hidden = 0;

    // Indexed by EventType: Event, Sleep, Wake, Alarm.
    std::array<long long, 4> byType{};

    long long upcoming = 0; // start > now
    long long ongoing = 0;  // start <= now < end
    long long past = 0;     // end <= now

    long long scheduledSeconds = 0;
    long long conflictPairs = 0;

    // nullopt when the set is empty; the formatter prints "-" / null.
    std::optional<long long> earliestStartUtc;
    std::optional<long long> latestEndUtc;
};

CalendarStats computeStats(const std::vector<Event> &events, long long nowUtc);

} // namespace klend_ario
