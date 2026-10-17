#include "klend_ario/alarm.hh"

#include <algorithm>

namespace klend_ario {
namespace {

std::vector<AlarmTrigger> triggersOf(const std::vector<Event> &events) {
    std::vector<AlarmTrigger> triggers;
    for (const Event &event : events) {
        for (const long long fireUtc : alarmTriggersUtc(event)) {
            triggers.push_back(AlarmTrigger{event.id, fireUtc, fireUtc != event.startUtc,
                                            event.title, event.startUtc});
        }
    }
    return triggers;
}

bool byFireTime(const AlarmTrigger &a, const AlarmTrigger &b) {
    if (a.fireUtc != b.fireUtc) {
        return a.fireUtc < b.fireUtc;
    }
    if (a.eventId != b.eventId) {
        return a.eventId < b.eventId;
    }
    // At the same instant, the T-offset trigger is the earlier intent.
    return a.offset && !b.offset;
}

} // namespace

std::vector<AlarmTrigger> alarmsIn(const std::vector<Event> &events, long long fromInclusive,
                                   long long toExclusive) {
    std::vector<AlarmTrigger> found;
    for (AlarmTrigger &trigger : triggersOf(events)) {
        if (trigger.fireUtc >= fromInclusive && trigger.fireUtc < toExclusive) {
            found.push_back(std::move(trigger));
        }
    }
    std::sort(found.begin(), found.end(), byFireTime);
    return found;
}

std::vector<AlarmTrigger> dueAlarms(const std::vector<Event> &events, long long afterExclusive,
                                    long long nowInclusive) {
    std::vector<AlarmTrigger> found;
    for (AlarmTrigger &trigger : triggersOf(events)) {
        if (trigger.fireUtc > afterExclusive && trigger.fireUtc <= nowInclusive) {
            found.push_back(std::move(trigger));
        }
    }
    std::sort(found.begin(), found.end(), byFireTime);
    return found;
}

std::optional<AlarmTrigger> nextAlarm(const std::vector<Event> &events, long long atOrAfterUtc) {
    std::optional<AlarmTrigger> best;
    for (AlarmTrigger &trigger : triggersOf(events)) {
        if (trigger.fireUtc < atOrAfterUtc) {
            continue;
        }
        if (!best || byFireTime(trigger, *best)) {
            best = std::move(trigger);
        }
    }
    return best;
}

} // namespace klend_ario
