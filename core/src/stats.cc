#include "klend_ario/stats.hh"

#include "klend_ario/conflict.hh"

namespace klend_ario {

CalendarStats computeStats(const std::vector<Event> &events, long long nowUtc) {
    CalendarStats stats;
    for (const Event &event : events) {
        ++stats.total;
        if (event.hidden) {
            ++stats.hidden;
        } else {
            ++stats.visible;
        }

        const size_t typeIndex = static_cast<size_t>(event.type);
        if (typeIndex < stats.byType.size()) {
            ++stats.byType[typeIndex];
        }

        // The order matters at now: a zero-length event at `now` is already
        // over, so it is past, not ongoing.
        if (event.endUtc <= nowUtc) {
            ++stats.past;
        } else if (event.startUtc <= nowUtc) {
            ++stats.ongoing;
        } else {
            ++stats.upcoming;
        }

        if (event.endUtc > event.startUtc) {
            stats.scheduledSeconds += event.endUtc - event.startUtc;
        }

        if (!stats.earliestStartUtc || event.startUtc < *stats.earliestStartUtc) {
            stats.earliestStartUtc = event.startUtc;
        }
        if (!stats.latestEndUtc || event.endUtc > *stats.latestEndUtc) {
            stats.latestEndUtc = event.endUtc;
        }
    }

    // The same rule as the `conflicts` verb: alarm rows are reminders, not
    // blocks, and a triple overlap is three pairs.
    stats.conflictPairs = static_cast<long long>(findConflicts(events).size());
    return stats;
}

} // namespace klend_ario
