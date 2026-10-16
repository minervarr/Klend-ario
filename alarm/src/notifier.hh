#pragma once

#include <memory>

#include "klend_ario/alarm.hh"

// Where a fired alarm GOES — the platform seam.
//
// The engine decides WHEN (pure, in core). A Notifier decides HOW: libnotify on
// Linux, a native notification API on a phone, a stderr line in a headless test.
// The platform half is INSTALLED, never compiled into core — the same rule
// arc_fs follows for its media index and private directory.

namespace klend_ario::alarm {

class Notifier {
public:
    virtual ~Notifier() = default;
    virtual void fire(const klend_ario::AlarmTrigger &trigger) = 0;
};

// The best notifier this build has: libnotify when it was compiled in AND can
// initialise, else a log line so the engine still runs where no notification
// daemon exists.
std::unique_ptr<Notifier> makeNotifier();

// The log notifier, forced: for a headless machine, a service, or a test that
// needs to SEE what fired.
std::unique_ptr<Notifier> makeLogNotifier();

} // namespace klend_ario::alarm
