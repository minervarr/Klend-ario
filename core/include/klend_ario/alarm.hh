#pragma once

#include <optional>
#include <string>
#include <vector>

#include "klend_ario/event.hh"

// WHEN an alarm fires, as a pure function of the calendar.
//
// The product rule is two triggers per event — T-offset and T=0 (merged.md §6,
// implemented by event.hh's alarmTriggersUtc). Everything about *deciding* is
// here and testable; everything about *delivering* (libnotify, PipeWire, a
// phone's notification API) lives in the alarm/ frontend behind a seam. That is
// the same split as arc_fs's MediaIndex: the portable half is pure, the
// platform half is installed.

namespace klend_ario {

struct AlarmTrigger {
    long long eventId = 0;
    long long fireUtc = 0;
    bool offset = false; // true = the T-offset trigger, false = T=0
    std::string title;
};

// Every trigger falling in [fromInclusive, toExclusive), sorted by fire time
// (ties broken by event id, then offset-first).
std::vector<AlarmTrigger> alarmsIn(const std::vector<Event> &events, long long fromInclusive,
                                   long long toExclusive);

// Every trigger that became due in (afterExclusive, nowInclusive]. This is the
// polling window: a trigger already seen must not fire again.
std::vector<AlarmTrigger> dueAlarms(const std::vector<Event> &events, long long afterExclusive,
                                    long long nowInclusive);

// The soonest trigger at or after `atOrAfterUtc`, or nothing when none exists.
std::optional<AlarmTrigger> nextAlarm(const std::vector<Event> &events, long long atOrAfterUtc);

} // namespace klend_ario
