#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <vector>

#include "engine.hh"

#include "klend_ario/db.hh"
#include "klend_ario/event.hh"

using namespace klend_ario;

namespace {

struct FakeNotifier : alarm::Notifier {
    std::vector<AlarmTrigger> fired;
    void fire(const AlarmTrigger &trigger) override { fired.push_back(trigger); }
};

Event makeEvent(long long start, int offset) {
    Event e;
    e.title = "X";
    e.startUtc = start;
    e.endUtc = start + 600;
    e.alarmOffset = offset;
    return e;
}

} // namespace

int main() {
    auto db = Db::openMemory();
    assert(db.ok());
    assert(db.value().insertEvent(makeEvent(1000, 600)).ok()); // triggers 400 and 1000
    assert(db.value().insertEvent(makeEvent(5000, 0)).ok());   // trigger 5000

    FakeNotifier notifier;
    alarm::AlarmEngine engine(db.take(), notifier);

    // First poll at 1000 with a generous horizon catches both of event 1's
    // triggers (the ones after `now - horizon`).
    const auto first = engine.poll(1000, 3600);
    assert(first.size() == 2);
    assert(first[0].fireUtc == 400 && first[0].offset);
    assert(first[1].fireUtc == 1000 && !first[1].offset);
    assert(notifier.fired.size() == 2);

    // Polling again at the same instant fires nothing: already seen.
    assert(engine.poll(1000, 3600).empty());
    assert(notifier.fired.size() == 2);

    // The next alarm after the current instant is event 2 at 5000.
    const auto next = engine.peekNext(1001);
    assert(next.has_value() && next->fireUtc == 5000);

    // A later poll crosses event 2's trigger.
    const auto later = engine.poll(5000, 60);
    assert(later.size() == 1);
    assert(later[0].fireUtc == 5000);
    assert(notifier.fired.size() == 3);

    // A fresh engine started "late" does not replay old alarms: the first-poll
    // horizon is small, so triggers from long ago are outside the window.
    FakeNotifier fresh;
    alarm::AlarmEngine restarted(Db::openMemory().take(), fresh);
    assert(restarted.poll(1200, 60).empty());
    assert(fresh.fired.empty());

    std::printf("engine_test: all assertions passed\n");
    return 0;
}
