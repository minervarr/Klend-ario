#undef NDEBUG
#include <cassert>
#include <cstdio>

#include "klend_ario/db.hh"
#include "klend_ario/event.hh"

using namespace klend_ario;

namespace {

Event makeEvent(const char *title, long long start, long long end) {
    Event e;
    e.title = title;
    e.startUtc = start;
    e.endUtc = end;
    return e;
}

void testOpenAppliesSchema() {
    auto db = Db::openMemory();
    assert(db.ok());

    Event e;
    e.title = "Standup";
    e.startUtc = 1000;
    e.endUtc = 1600;
    e.importance = 80;
    e.urgency = 10;
    e.notes = "daily";
    auto id = db.value().insertEvent(e);
    assert(id.ok());
    assert(id.value() > 0);

    auto read = db.value().getEvent(id.value());
    assert(read.ok());
    assert(read.value().title == "Standup");
    assert(read.value().startUtc == 1000);
    assert(read.value().endUtc == 1600);
    assert(read.value().importance == 80);
    assert(read.value().urgency == 10);
    assert(read.value().notes == "daily");
    assert(read.value().type == EventType::Event);
    // Insert stamps created/updated when the caller leaves them at 0.
    assert(read.value().createdUtc != 0);
    assert(read.value().updatedUtc == read.value().createdUtc);
}

void testConstraints() {
    auto db = Db::openMemory();
    assert(db.ok());

    // end before start violates the CHECK.
    assert(!db.value().insertEvent(makeEvent("bad", 200, 100)).ok());

    // importance outside 0..100 violates the CHECK.
    Event bad = makeEvent("bad2", 100, 200);
    bad.importance = 101;
    assert(!db.value().insertEvent(bad).ok());
}

void testListRangeOverlap() {
    auto db = Db::openMemory();
    assert(db.ok());
    db.value().insertEvent(makeEvent("a", 900, 1100));  // spans into the range
    db.value().insertEvent(makeEvent("b", 1500, 1700)); // fully inside
    db.value().insertEvent(makeEvent("c", 2500, 2600)); // after

    auto list = db.value().listEvents(1000, 2000);
    assert(list.ok());
    assert(list.value().size() == 2);
    // Ordered by start. The half-open range includes "a" (its end is 1100>1000)
    // even though it started before the window — overlap, not containment.
    assert(list.value()[0].title == "a");
    assert(list.value()[1].title == "b");
}

void testRemove() {
    auto db = Db::openMemory();
    assert(db.ok());
    auto id = db.value().insertEvent(makeEvent("gone", 1, 2));
    assert(id.ok());
    assert(db.value().removeEvent(id.value()).ok());
    assert(!db.value().getEvent(id.value()).ok());
    // Removing an id that never existed still succeeds.
    assert(db.value().removeEvent(999999).ok());
}

void testMissingEventIsError() {
    auto db = Db::openMemory();
    assert(db.ok());
    assert(!db.value().getEvent(42).ok());
}

} // namespace

int main() {
    testOpenAppliesSchema();
    testConstraints();
    testListRangeOverlap();
    testRemove();
    testMissingEventIsError();
    std::printf("schema_test: all assertions passed\n");
    return 0;
}
