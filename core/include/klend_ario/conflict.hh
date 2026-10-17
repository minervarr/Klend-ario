#pragma once

#include <string>
#include <vector>

#include "klend_ario/event.hh"

// Pairwise event overlaps ("double-bookings"), as a pure value.
//
// The rule is the same half-open overlap the rest of the calendar uses:
// [start, end), so touching endpoints and zero-length events never conflict.
// `EventType::Alarm` rows are reminders anchored to a moment, not blocks of
// time, so they are ignored here (and by `free`); a reminder overlapping its
// target is the intended shape, not a conflict.
//
// Hidden rows are a QUERY decision, not a model one: the caller decides whether
// to pass them. The CLI's `conflicts` verb filters them by default.

namespace klend_ario {

struct Conflict {
    // The earlier-starting event is "first"; ties are ordered by id, so the
    // output is deterministic without depending on input order.
    long long firstId = 0;
    std::string firstTitle;
    long long secondId = 0;
    std::string secondTitle;

    long long startUtc = 0; // the overlapping interval, not either whole event
    long long endUtc = 0;
};

// Every overlapping pair among `events`, sorted by overlap start and then by
// pair id. A triple overlap yields three pairs (A/B, A/C, B/C).
std::vector<Conflict> findConflicts(const std::vector<Event> &events);

} // namespace klend_ario
