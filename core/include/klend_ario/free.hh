#pragma once

#include <vector>

#include "klend_ario/event.hh"

// Free time inside a range, as a pure interval computation.
//
// A "free slot" is a gap between busy blocks, clamped to the query range. Busy
// blocks are merged first, so overlapping events do not create fake gaps.
// `EventType::Alarm` rows are reminders, not blocks, and are ignored. Hidden
// rows are the caller's choice: the CLI excludes them by default.
//
// The range is half-open [fromUtc, toUtc), and a slot must be at least
// `minDurationSeconds` long to be reported. A non-positive duration or an empty
// range yields no slots.

namespace klend_ario {

struct FreeSlot {
    long long startUtc = 0;
    long long endUtc = 0;
};

std::vector<FreeSlot> freeSlots(const std::vector<Event> &events, long long fromUtc,
                                long long toUtc, long long minDurationSeconds);

} // namespace klend_ario
