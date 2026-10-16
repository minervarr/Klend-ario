#pragma once

#include <vector>

#include "klend_ario/alarm.hh"
#include "klend_ario/db.hh"

#include "notifier.hh"

namespace klend_ario::alarm {

// Polls the calendar and fires whatever became due.
//
// The whole engine is one method with no sleep in it: the caller owns the clock
// and the loop, so a test can drive it with chosen instants and a fake Notifier
// and assert exactly which alarms fire when. The daemon's `while (true)
// { poll(); sleep(); }` lives in main.cc, where it belongs.
class AlarmEngine {
public:
    AlarmEngine(klend_ario::Db db, Notifier &notifier);

    // Fires alarms whose trigger time fell in (previous poll, nowUtc]. On the
    // FIRST poll the window opens at `nowUtc - horizonSeconds`, so a daemon
    // started a little late still catches a recent alarm — but a small horizon,
    // so a restart does not replay yesterday.
    std::vector<klend_ario::AlarmTrigger> poll(long long nowUtc, long long horizonSeconds = 60);

    // The soonest future alarm, for a UI ("next: …"). Does not advance state.
    std::optional<klend_ario::AlarmTrigger> peekNext(long long nowUtc);

private:
    klend_ario::Db db_;
    Notifier &notifier_;
    long long lastChecked_ = 0;
    bool started_ = false;
};

} // namespace klend_ario::alarm
