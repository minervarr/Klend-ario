#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

#include "klend_ario/db.hh"
#include "klend_ario/event.hh"
#include "klend_ario/query.hh"

using namespace klend_ario;

namespace {

long long at(const char *text) {
    const auto value = parseIso8601(text);
    assert(value.has_value());
    return *value;
}

Event makeEvent(const char *title, const char *start, const char *end, int importance,
                int urgency, EventType type, bool hidden, const char *notes,
                long long updatedUtc) {
    Event event;
    event.title = title;
    event.startUtc = at(start);
    event.endUtc = at(end);
    event.importance = importance;
    event.urgency = urgency;
    event.type = type;
    event.hidden = hidden;
    event.notes = notes;
    event.createdUtc = 1000;
    event.updatedUtc = updatedUtc;
    return event;
}

void insert(Db &db, Event event, long long &id) {
    auto result = db.insertEvent(event);
    assert(result.ok());
    id = result.value();
}

struct Fixture {
    Db db;
    long long standup = 0; // A
    long long lunch = 0;   // B
    long long sleep = 0;   // C
    long long retro = 0;   // D
    long long ping = 0;    // E

    Fixture() {
        auto opened = Db::openMemory();
        assert(opened.ok());
        db = opened.take();

        insert(db,
               makeEvent("Standup", "2026-10-16T09:00:00Z", "2026-10-16T09:30:00Z", 80, 10,
                         EventType::Event, false, "daily sync", 100),
               standup);
        insert(db,
               makeEvent("lunch", "2026-10-16T13:00:00Z", "2026-10-16T14:00:00Z", 50, 20,
                         EventType::Event, false, "with Ana", 200),
               lunch);
        insert(db,
               makeEvent("Sleep", "2026-10-16T23:00:00Z", "2026-10-17T07:00:00Z", 30, 0,
                         EventType::Sleep, true, "", 300),
               sleep);
        insert(db,
               makeEvent("Retro", "2026-10-17T15:00:00Z", "2026-10-17T16:00:00Z", 80, 50,
                         EventType::Event, false, "100% done_now", 400),
               retro);
        insert(db,
               makeEvent("Ping", "2026-10-16T12:00:00Z", "2026-10-16T12:30:00Z", 60, 60,
                         EventType::Alarm, false, "ping", 500),
               ping);
    }
};

std::vector<Event> run(Db &db, const EventQuery &query) {
    auto result = db.queryEvents(query);
    assert(result.ok());
    return result.take();
}

void assertIds(const std::vector<Event> &events, std::initializer_list<long long> expected) {
    assert(events.size() == expected.size());
    size_t index = 0;
    for (const long long id : expected) {
        assert(events[index].id == id);
        ++index;
    }
}

void testDefaultsReturnEverything(Fixture &f) {
    // An empty query is unbounded, visible-and-hidden, ordered by start.
    assertIds(run(f.db, EventQuery{}), {f.standup, f.ping, f.lunch, f.sleep, f.retro});
}

void testHalfOpenOverlap(Fixture &f) {
    EventQuery query;

    // Exactly the event's own bounds: [09:00, 09:30) includes it.
    query.fromUtc = at("2026-10-16T09:00:00Z");
    query.toUtc = at("2026-10-16T09:30:00Z");
    assertIds(run(f.db, query), {f.standup});

    // Adjacent range: the event's end is the next range's start, so it is not
    // double-counted.
    query.fromUtc = at("2026-10-16T09:30:00Z");
    query.toUtc = at("2026-10-16T10:00:00Z");
    assertIds(run(f.db, query), {});

    // A range inside the event still overlaps it.
    query.fromUtc = at("2026-10-16T09:29:00Z");
    query.toUtc = at("2026-10-16T09:31:00Z");
    assertIds(run(f.db, query), {f.standup});

    // Empty range.
    query.fromUtc = at("2026-10-16T09:00:00Z");
    query.toUtc = query.fromUtc;
    assertIds(run(f.db, query), {});

    // An event that started yesterday and is still running belongs to today's
    // range: overlap, not containment.
    query.fromUtc = at("2026-10-16T23:30:00Z");
    query.toUtc = at("2026-10-16T23:45:00Z");
    assertIds(run(f.db, query), {f.sleep});

    query.fromUtc = at("2026-10-17T00:00:00Z");
    query.toUtc = at("2026-10-18T00:00:00Z");
    assertIds(run(f.db, query), {f.sleep, f.retro});

    // The sleep block has ended by 08:00 and Retro starts at 15:00.
    query.fromUtc = at("2026-10-17T08:00:00Z");
    query.toUtc = at("2026-10-17T09:00:00Z");
    assertIds(run(f.db, query), {});

    // Ping ends exactly when the next range begins; lunch begins exactly when
    // it ends. Neither leaks across the boundary.
    query.fromUtc = at("2026-10-16T12:30:00Z");
    query.toUtc = at("2026-10-16T13:00:00Z");
    assertIds(run(f.db, query), {});
}

void testText(Fixture &f) {
    EventQuery query;
    query.text = "sync";
    assertIds(run(f.db, query), {f.standup});

    // ASCII case folding, on notes as well as titles.
    query.text = "ANA";
    assertIds(run(f.db, query), {f.lunch});

    // A multi-word search is a phrase, not an AND of words.
    query.text = "daily sync";
    assertIds(run(f.db, query), {f.standup});
    query.text = "daily  sync";
    assertIds(run(f.db, query), {});

    // instr() is literal: LIKE wildcards are ordinary characters.
    query.text = "%";
    assertIds(run(f.db, query), {f.retro});
    query.text = "_";
    assertIds(run(f.db, query), {f.retro});
    query.text = "done_now";
    assertIds(run(f.db, query), {f.retro});

    query.text = "PING";
    assertIds(run(f.db, query), {f.ping});

    query.text = "zzz";
    assertIds(run(f.db, query), {});
}

void testType(Fixture &f) {
    EventQuery query;
    query.type = EventType::Sleep;
    assertIds(run(f.db, query), {f.sleep});

    query.type = EventType::Alarm;
    assertIds(run(f.db, query), {f.ping});

    query.type = EventType::Event;
    assertIds(run(f.db, query), {f.standup, f.lunch, f.retro});

    query.type = EventType::Wake;
    assertIds(run(f.db, query), {});
}

void testImportanceAndUrgency(Fixture &f) {
    EventQuery query;

    query.minImportance = 80;
    assertIds(run(f.db, query), {f.standup, f.retro});

    query = EventQuery{};
    query.maxImportance = 40;
    assertIds(run(f.db, query), {f.sleep});

    query = EventQuery{};
    query.minImportance = 60;
    query.maxImportance = 80;
    assertIds(run(f.db, query), {f.standup, f.ping, f.retro});

    // Bounds are inclusive on both ends.
    query = EventQuery{};
    query.minUrgency = 20;
    assertIds(run(f.db, query), {f.ping, f.lunch, f.retro});

    query = EventQuery{};
    query.minUrgency = 20;
    query.maxUrgency = 50;
    assertIds(run(f.db, query), {f.lunch, f.retro});

    query = EventQuery{};
    query.minUrgency = 61;
    assertIds(run(f.db, query), {});

    query = EventQuery{};
    query.minUrgency = 0;
    query.maxUrgency = 0;
    assertIds(run(f.db, query), {f.sleep});
}

void testVisibility(Fixture &f) {
    EventQuery query;
    query.visibility = Visibility::Visible;
    assertIds(run(f.db, query), {f.standup, f.ping, f.lunch, f.retro});

    query.visibility = Visibility::Hidden;
    assertIds(run(f.db, query), {f.sleep});

    query.visibility = Visibility::Any;
    assertIds(run(f.db, query), {f.standup, f.ping, f.lunch, f.sleep, f.retro});
}

void testSorts(Fixture &f) {
    EventQuery query;

    query.sort = EventSort::Start;
    assertIds(run(f.db, query), {f.standup, f.ping, f.lunch, f.sleep, f.retro});

    query.sort = EventSort::StartDesc;
    assertIds(run(f.db, query), {f.retro, f.sleep, f.lunch, f.ping, f.standup});

    // Standup and Retro tie on importance; Retro's higher urgency breaks the
    // tie. The rest fall by importance, then urgency.
    query.sort = EventSort::Importance;
    assertIds(run(f.db, query), {f.retro, f.standup, f.ping, f.lunch, f.sleep});

    query.sort = EventSort::Urgency;
    assertIds(run(f.db, query), {f.ping, f.retro, f.lunch, f.standup, f.sleep});

    query.sort = EventSort::Updated;
    assertIds(run(f.db, query), {f.ping, f.retro, f.sleep, f.lunch, f.standup});

    // "lunch" must sort before "Ping": COLLATE NOCASE, or ASCII would put the
    // uppercase P first.
    query.sort = EventSort::Title;
    assertIds(run(f.db, query), {f.lunch, f.ping, f.retro, f.sleep, f.standup});
}

void testLimitOffset(Fixture &f) {
    EventQuery query; // start order: Standup, Ping, lunch, Sleep, Retro

    query.limit = 2;
    assertIds(run(f.db, query), {f.standup, f.ping});

    query.limit = 2;
    query.offset = 1;
    assertIds(run(f.db, query), {f.ping, f.lunch});

    query = EventQuery{};
    query.limit = 1;
    query.offset = 1;
    assertIds(run(f.db, query), {f.ping});

    query = EventQuery{};
    query.offset = 2; // no limit
    assertIds(run(f.db, query), {f.lunch, f.sleep, f.retro});

    query = EventQuery{};
    query.limit = 0; // SQL semantics: zero rows, not "unlimited"
    assertIds(run(f.db, query), {});

    query = EventQuery{};
    query.offset = 10;
    assertIds(run(f.db, query), {});

    query = EventQuery{};
    query.limit = 100;
    assertIds(run(f.db, query), {f.standup, f.ping, f.lunch, f.sleep, f.retro});
}

void testCombined(Fixture &f) {
    EventQuery query;
    query.type = EventType::Event;
    query.visibility = Visibility::Visible;
    query.minImportance = 80;
    assertIds(run(f.db, query), {f.standup, f.retro});

    query = EventQuery{};
    query.text = "a";
    query.type = EventType::Event;
    query.visibility = Visibility::Visible;
    assertIds(run(f.db, query), {f.standup, f.lunch});

    query = EventQuery{};
    query.text = "ana";
    query.type = EventType::Event;
    assertIds(run(f.db, query), {f.lunch});

    query = EventQuery{};
    query.text = "sleep";
    query.visibility = Visibility::Hidden;
    assertIds(run(f.db, query), {f.sleep});

    query = EventQuery{};
    query.maxUrgency = 10;
    query.visibility = Visibility::Visible;
    assertIds(run(f.db, query), {f.standup});

    query = EventQuery{};
    query.sort = EventSort::Title;
    query.limit = 2;
    assertIds(run(f.db, query), {f.lunch, f.ping});
}

void testListEventsSharesTheOverlapRule(Fixture &f) {
    auto exact = f.db.listEvents(at("2026-10-16T09:00:00Z"), at("2026-10-16T09:30:00Z"));
    assert(exact.ok());
    assertIds(exact.value(), {f.standup});

    auto adjacent = f.db.listEvents(at("2026-10-16T09:30:00Z"), at("2026-10-16T13:00:00Z"));
    assert(adjacent.ok());
    assertIds(adjacent.value(), {f.ping});

    auto inside = f.db.listEvents(at("2026-10-16T12:00:00Z"), at("2026-10-16T12:30:00Z"));
    assert(inside.ok());
    assertIds(inside.value(), {f.ping});

    auto gap = f.db.listEvents(at("2026-10-16T12:30:00Z"), at("2026-10-16T13:00:00Z"));
    assert(gap.ok());
    assertIds(gap.value(), {});

    auto everything = f.db.listEvents(0, 4102444800LL);
    assert(everything.ok());
    assertIds(everything.value(), {f.standup, f.ping, f.lunch, f.sleep, f.retro});
}

void testSortNames() {
    EventSort sort = EventSort::Start;
    assert(eventSortFromName("start", sort) && sort == EventSort::Start);
    assert(eventSortFromName("-start", sort) && sort == EventSort::StartDesc);
    assert(eventSortFromName("importance", sort) && sort == EventSort::Importance);
    assert(eventSortFromName("urgency", sort) && sort == EventSort::Urgency);
    assert(eventSortFromName("updated", sort) && sort == EventSort::Updated);
    assert(eventSortFromName("title", sort) && sort == EventSort::Title);
    assert(!eventSortFromName("banana", sort));
    assert(!eventSortFromName("Start", sort)); // names are exact

    assert(std::string(eventSortName(EventSort::Start)) == "start");
    assert(std::string(eventSortName(EventSort::StartDesc)) == "-start");
    assert(std::string(eventSortName(EventSort::Importance)) == "importance");
    assert(std::string(eventSortName(EventSort::Urgency)) == "urgency");
    assert(std::string(eventSortName(EventSort::Updated)) == "updated");
    assert(std::string(eventSortName(EventSort::Title)) == "title");
}

} // namespace

int main() {
    Fixture fixture;
    testDefaultsReturnEverything(fixture);
    testHalfOpenOverlap(fixture);
    testText(fixture);
    testType(fixture);
    testImportanceAndUrgency(fixture);
    testVisibility(fixture);
    testSorts(fixture);
    testLimitOffset(fixture);
    testCombined(fixture);
    testListEventsSharesTheOverlapRule(fixture);
    testSortNames();
    std::printf("query_test: all assertions passed\n");
    return 0;
}
