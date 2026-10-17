#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <vector>

#include "klend_ario/alarm.hh"
#include "klend_ario/event.hh"

using namespace klend_ario;

namespace {

Event makeEvent(long long id, long long start, int offset, const char *title = "e") {
    Event e;
    e.id = id;
    e.startUtc = start;
    e.endUtc = start + 3600;
    e.alarmOffset = offset;
    e.title = title;
    return e;
}

} // namespace

static void testTriggersAreOffsetThenStart() {
    std::vector<Event> events{makeEvent(1, 1000, 600)};
    const auto all = alarmsIn(events, 0, 2000);
    assert(all.size() == 2);
    assert(all[0].fireUtc == 400 && all[0].offset);   // T-offset first
    assert(all[1].fireUtc == 1000 && !all[1].offset); // then T=0
    assert(all[0].eventId == 1 && all[0].title == "e");
    // The trigger carries the event start, so a formatter can say "T-10m"
    // without looking the event up again.
    assert(all[0].eventStartUtc == 1000 && all[1].eventStartUtc == 1000);
}

static void testNoOffsetMeansOneTrigger() {
    std::vector<Event> events{makeEvent(1, 1000, 0)};
    assert(alarmsIn(events, 0, 2000).size() == 1);
}

static void testWindowIsHalfOpen() {
    std::vector<Event> events{makeEvent(1, 1000, 0)};
    assert(alarmsIn(events, 1000, 2000).size() == 1);
    assert(alarmsIn(events, 0, 1000).empty());
}

static void testDueIsExclusiveInclusive() {
    std::vector<Event> events{makeEvent(1, 1000, 0)};
    // (999, 1000] fires; a trigger already at the cursor does not fire again.
    assert(dueAlarms(events, 999, 1000).size() == 1);
    assert(dueAlarms(events, 1000, 1001).empty());
}

static void testSortedAcrossEvents() {
    std::vector<Event> events{makeEvent(2, 500, 0), makeEvent(1, 100, 0), makeEvent(3, 900, 0)};
    const auto all = alarmsIn(events, 0, 1000);
    assert(all.size() == 3);
    assert(all[0].fireUtc == 100 && all[1].fireUtc == 500 && all[2].fireUtc == 900);
}

static void testNextAlarm() {
    // Event 2 carries an offset, so its triggers are 200 and 500.
    std::vector<Event> events{makeEvent(1, 100, 0), makeEvent(2, 500, 300)};
    const auto first = nextAlarm(events, 0);
    assert(first.has_value() && first->fireUtc == 100);
    const auto after = nextAlarm(events, 150);
    assert(after.has_value() && after->fireUtc == 200);
    assert(after->eventId == 2 && after->offset);
    assert(!nextAlarm(events, 1000).has_value());
}

int main() {
    testTriggersAreOffsetThenStart();
    testNoOffsetMeansOneTrigger();
    testWindowIsHalfOpen();
    testDueIsExclusiveInclusive();
    testSortedAcrossEvents();
    testNextAlarm();
    std::printf("alarm_test: all assertions passed\n");
    return 0;
}
