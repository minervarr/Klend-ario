#include "engine.hh"

namespace klend_ario::alarm {

AlarmEngine::AlarmEngine(klend_ario::Db db, Notifier &notifier)
    : db_(std::move(db)), notifier_(notifier) {}

std::vector<klend_ario::AlarmTrigger> AlarmEngine::poll(long long nowUtc, long long horizonSeconds) {
    const long long from = started_ ? lastChecked_ : nowUtc - horizonSeconds;
    started_ = true;
    lastChecked_ = nowUtc;

    // A little past `now` so an event STARTING exactly now is included.
    auto events = db_.listEvents(from, nowUtc + 1);
    if (!events) {
        return {};
    }
    std::vector<klend_ario::AlarmTrigger> due =
        klend_ario::dueAlarms(events.value(), from, nowUtc);
    for (const klend_ario::AlarmTrigger &trigger : due) {
        notifier_.fire(trigger);
    }
    return due;
}

std::optional<klend_ario::AlarmTrigger> AlarmEngine::peekNext(long long nowUtc) {
    // A year ahead is plenty for "the next alarm"; the calendar is not a
    // scheduling system and an unbounded scan would read every row.
    auto events = db_.listEvents(nowUtc, nowUtc + 365LL * 86400);
    if (!events) {
        return std::nullopt;
    }
    return klend_ario::nextAlarm(events.value(), nowUtc);
}

} // namespace klend_ario::alarm
