#include "notifier.hh"

#include <libnotify/notify.h>

#include <cstdio>

namespace klend_ario::alarm {
namespace {

// The Linux desktop backend: a transient desktop notification (merged.md §6).
// Sound (PipeWire) is a separate concern and is NOT implemented yet — see
// essentialAgent/MEMORY.md §9.
class LibnotifyNotifier : public Notifier {
public:
    LibnotifyNotifier() { available_ = notify_init("klend_ario") != 0; }
    ~LibnotifyNotifier() override {
        if (available_) {
            notify_uninit();
        }
    }

    // false when libnotify could not initialise (no D-Bus session, e.g. a
    // headless machine) — the caller then falls back to the log notifier rather
    // than silently swallowing every alarm.
    bool available() const { return available_; }

    void fire(const klend_ario::AlarmTrigger &trigger) override {
        if (!available_) {
            return;
        }
        NotifyNotification *notification =
            notify_notification_new(trigger.title.c_str(), trigger.offset ? "Coming up" : "Now",
                                    nullptr);
        GError *error = nullptr;
        if (notify_notification_show(notification, &error) == 0) {
            std::fprintf(stderr, "alarm: libnotify could not show the notification: %s\n",
                         error != nullptr ? error->message : "unknown error");
            if (error != nullptr) {
                g_error_free(error);
            }
        }
        g_object_unref(G_OBJECT(notification));
    }

private:
    bool available_ = false;
};

} // namespace

std::unique_ptr<Notifier> makeLibnotifyNotifier() {
    auto notifier = std::unique_ptr<LibnotifyNotifier>(new LibnotifyNotifier());
    if (!notifier->available()) {
        return nullptr; // no session bus; let makeNotifier() pick the log
    }
    return notifier;
}

} // namespace klend_ario::alarm
