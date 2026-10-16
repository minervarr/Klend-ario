#include "notifier.hh"

#include <cstdio>

#include "klend_ario/event.hh"

namespace klend_ario::alarm {

#ifdef KLEND_HAVE_LIBNOTIFY
// Defined in notifier_libnotify.cc; returns nullptr only if libnotify refuses
// to initialise, in which case makeNotifier() falls back to the log.
std::unique_ptr<Notifier> makeLibnotifyNotifier();
#endif

namespace {

// The portable floor: write a line to stderr. stderr, not stdout, because a
// frontend's stdout may be a protocol channel (as the MCP server's is).
class LogNotifier : public Notifier {
public:
    void fire(const klend_ario::AlarmTrigger &trigger) override {
        std::fprintf(stderr, "alarm: %s [%s] %s\n", trigger.title.c_str(),
                     trigger.offset ? "T-offset" : "T=0",
                     klend_ario::formatIso8601(trigger.fireUtc).c_str());
    }
};

} // namespace

std::unique_ptr<Notifier> makeLogNotifier() {
    return std::unique_ptr<Notifier>(new LogNotifier());
}

std::unique_ptr<Notifier> makeNotifier() {
#ifdef KLEND_HAVE_LIBNOTIFY
    if (auto notifier = makeLibnotifyNotifier()) {
        return notifier;
    }
#endif
    return makeLogNotifier();
}

} // namespace klend_ario::alarm
