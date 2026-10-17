#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "klend_ario/db.hh"
#include "klend_ario/event.hh"
#include "klend_ario/export.hh"
#include "klend_ario/import.hh"
#include "klend_ario/query.hh"

using namespace klend_ario;

namespace {

long long at(const char *text) {
    const auto value = parseIso8601(text);
    assert(value.has_value());
    return *value;
}

Event makeEvent(long long id, const char *title, const char *start, const char *end,
                EventType type, bool hidden) {
    Event event;
    event.id = id;
    event.title = title;
    event.startUtc = at(start);
    event.endUtc = at(end);
    event.type = type;
    event.importance = 80;
    event.urgency = 10;
    event.alarmOffset = 600;
    event.hidden = hidden;
    event.notes = "line one\nline two; ok";
    event.createdUtc = at("2026-10-16T08:00:00Z");
    event.updatedUtc = at("2026-10-16T08:30:00Z");
    return event;
}

std::vector<Event> fixture() {
    Event first = makeEvent(7, "Dinner, \"friends\"", "2026-10-16T19:00:00Z",
                            "2026-10-16T21:00:00Z", EventType::Event, false);
    Event second = makeEvent(9, "Nap", "2026-10-16T23:00:00Z", "2026-10-17T07:00:00Z",
                             EventType::Sleep, true);
    second.alarmOffset = -300;
    second.notes = "quiet";
    return {first, second};
}

void testRoundTrip(const std::vector<Event> &events) {
    for (const ImportFormat importFormat : {ImportFormat::Csv, ImportFormat::Jsonl}) {
        const ExportFormat exportFormat =
            importFormat == ImportFormat::Csv ? ExportFormat::Csv : ExportFormat::Jsonl;
        const std::string text = exportEvents(events, exportFormat);
        auto parsed = parseImportedEvents(text, importFormat);
        assert(parsed.ok());
        assert(parsed.value().size() == events.size());
        for (size_t i = 0; i < events.size(); ++i) {
            const Event &expected = events[i];
            const Event &actual = parsed.value()[i];
            assert(actual.id == expected.id);
            assert(actual.title == expected.title);
            assert(actual.startUtc == expected.startUtc);
            assert(actual.endUtc == expected.endUtc);
            assert(actual.type == expected.type);
            assert(actual.importance == expected.importance);
            assert(actual.urgency == expected.urgency);
            assert(actual.alarmOffset == expected.alarmOffset);
            assert(actual.hidden == expected.hidden);
            assert(actual.notes == expected.notes);
            assert(actual.createdUtc == expected.createdUtc);
            assert(actual.updatedUtc == expected.updatedUtc);
        }
    }
}

void testCsvHeaderByColumnAndDefaults() {
    const std::string text =
        "notes,title,end,start\n"
        "\"line one\nline two\",Standup,2026-10-16T09:30:00Z,2026-10-16T09:00:00Z\n";
    auto parsed = parseImportedEvents(text, ImportFormat::Csv);
    assert(parsed.ok());
    assert(parsed.value().size() == 1);
    const Event &event = parsed.value()[0];
    assert(event.title == "Standup");
    assert(event.startUtc == at("2026-10-16T09:00:00Z"));
    assert(event.endUtc == at("2026-10-16T09:30:00Z"));
    assert(event.notes == "line one\nline two");
    assert(event.type == EventType::Event);
    assert(event.importance == 50);
    assert(event.urgency == 50);
    assert(event.alarmOffset == 0);
    assert(!event.hidden);
    assert(event.id == 0);
}

void testCsvBomAndCrlf() {
    const std::string text =
        "\xEF\xBB\xBFtitle,start,end\r\n"
        "Standup,2026-10-16T09:00:00Z,2026-10-16T09:30:00Z\r\n";
    auto parsed = parseImportedEvents(text, ImportFormat::Csv);
    assert(parsed.ok());
    assert(parsed.value().size() == 1);
    assert(parsed.value()[0].title == "Standup");
}

void testCsvErrors() {
    auto expectError = [](const std::string &text, const char *needle) {
        auto parsed = parseImportedEvents(text, ImportFormat::Csv);
        assert(!parsed.ok());
        if (parsed.error().message.find(needle) == std::string::npos) {
            std::fprintf(stderr, "csv error: expected \"%s\" in: %s\n", needle,
                         parsed.error().message.c_str());
        }
        assert(parsed.error().message.find(needle) != std::string::npos);
    };

    expectError("", "empty");
    expectError("title,start,end\n", "no events");
    expectError("title,start\nA,2026-10-16T09:00:00Z\n", "missing required column");
    expectError("title,start,end,wat\nA,2026-10-16T09:00:00Z,2026-10-16T10:00:00Z,x\n",
                "unknown column");
    expectError("title,title,start,end\nA,B,2026-10-16T09:00:00Z,2026-10-16T10:00:00Z\n",
                "duplicate column");
    expectError("title,start,end\nA,2026-10-16T09:00:00Z\n", "expected 3 fields");
    expectError("title,start,end\nA,not-a-date,2026-10-16T10:00:00Z\n", "start is not");
    expectError("title,start,end\nA,2026-10-16T10:00:00Z,2026-10-16T09:00:00Z\n",
                "end is before start");
    expectError("title,start,end\n,2026-10-16T09:00:00Z,2026-10-16T10:00:00Z\n",
                "title must not be empty");
    expectError("title,start,end\n   ,2026-10-16T09:00:00Z,2026-10-16T10:00:00Z\n",
                "title must not be empty");
    expectError("title,start,end,type\nA,2026-10-16T09:00:00Z,2026-10-16T10:00:00Z,wat\n",
                "type must be");
    expectError("title,start,end,importance\nA,2026-10-16T09:00:00Z,2026-10-16T10:00:00Z,101\n",
                "importance must be");
    expectError("title,start,end,hidden\nA,2026-10-16T09:00:00Z,2026-10-16T10:00:00Z,maybe\n",
                "hidden must be");
    expectError("title,start,end\nA,\"2026-10-16T09:00:00Z,2026-10-16T10:00:00Z\n",
                "unterminated quoted field");
    expectError("title,start,end\nA,\"x\"y,2026-10-16T10:00:00Z\n",
                "unexpected character after a quoted field");
}

void testJsonlDefaultsAndEscapes() {
    const std::string text =
        "{\"title\":\"A\\nB\\u00e9\",\"start\":\"2026-10-16T09:00:00Z\","
        "\"end\":\"2026-10-16T10:00:00Z\",\"notes\":\"x\\\"y\"}\n";
    auto parsed = parseImportedEvents(text, ImportFormat::Jsonl);
    assert(parsed.ok());
    assert(parsed.value().size() == 1);
    const Event &event = parsed.value()[0];
    assert(event.title == "A\nB\xC3\xA9");
    assert(event.notes == "x\"y");
    assert(event.type == EventType::Event);
    assert(event.importance == 50);
    assert(event.urgency == 50);
    assert(event.alarmOffset == 0);
    assert(!event.hidden);
}

void testJsonlErrors() {
    auto expectError = [](const std::string &text, const char *needle) {
        auto parsed = parseImportedEvents(text, ImportFormat::Jsonl);
        assert(!parsed.ok());
        if (parsed.error().message.find(needle) == std::string::npos) {
            std::fprintf(stderr, "jsonl error: expected \"%s\" in: %s\n", needle,
                         parsed.error().message.c_str());
        }
        assert(parsed.error().message.find(needle) != std::string::npos);
    };

    expectError("", "empty");
    expectError("   \n\n", "empty");
    expectError("{\"start\":\"2026-10-16T09:00:00Z\",\"end\":\"2026-10-16T10:00:00Z\"}\n",
                "title");
    expectError("{\"title\":\"A\",\"start\":\"bad\",\"end\":\"2026-10-16T10:00:00Z\"}\n",
                "start is not");
    expectError("{\"title\":\"A\",\"start\":\"2026-10-16T10:00:00Z\","
                "\"end\":\"2026-10-16T09:00:00Z\"}\n",
                "end is before start");
    expectError("{\"title\":\"A\",\"start\":\"2026-10-16T09:00:00Z\","
                "\"end\":\"2026-10-16T10:00:00Z\",\"type\":\"wat\"}\n",
                "type must be");
    expectError("{\"title\":\"A\",\"start\":\"2026-10-16T09:00:00Z\","
                "\"end\":\"2026-10-16T10:00:00Z\",\"importance\":1.5}\n",
                "importance must be an integer");
    expectError("{\"title\":\"A\",\"start\":\"2026-10-16T09:00:00Z\","
                "\"end\":\"2026-10-16T10:00:00Z\",\"hidden\":1}\n",
                "hidden must be");
    expectError("{\"title\":\"A\",\"title\":\"B\",\"start\":\"2026-10-16T09:00:00Z\","
                "\"end\":\"2026-10-16T10:00:00Z\"}\n",
                "duplicate key");
    expectError("{\"title\":\"A\",\"start\":\"2026-10-16T09:00:00Z\","
                "\"end\":\"2026-10-16T10:00:00Z\",\"extra\":{}}\n",
                "nested JSON");
    expectError("{\"title\":\"A\",\"start\":\"2026-10-16T09:00:00Z\","
                "\"end\":\"2026-10-16T10:00:00Z\"\n",
                "unterminated object");
}

void testUnknownJsonScalarIsIgnored() {
    const std::string text =
        "{\"title\":\"A\",\"start\":\"2026-10-16T09:00:00Z\","
        "\"end\":\"2026-10-16T10:00:00Z\",\"future\":true,\"n\":1}\n";
    auto parsed = parseImportedEvents(text, ImportFormat::Jsonl);
    assert(parsed.ok());
    assert(parsed.value().size() == 1);
}

void testDbImportAppendReplaceAndIds() {
    auto db = Db::openMemory();
    assert(db.ok());

    auto events = parseImportedEvents(exportEvents(fixture(), ExportFormat::Csv),
                                      ImportFormat::Csv);
    assert(events.ok());

    auto imported = db.value().importEvents(events.value(), false, false);
    assert(imported.ok());
    assert(imported.value() == 2);

    auto all = db.value().queryEvents(EventQuery{});
    assert(all.ok());
    assert(all.value().size() == 2);
    assert(all.value()[0].id != 0 && all.value()[1].id != 0);
    assert(all.value()[0].title == "Dinner, \"friends\"");

    // Append is append, not an upsert.
    assert(db.value().importEvents(events.value(), false, false).ok());
    all = db.value().queryEvents(EventQuery{});
    assert(all.value().size() == 4);

    // Replace is transactional and leaves only the new set.
    Event replacement = makeEvent(0, "Only", "2026-10-17T09:00:00Z",
                                  "2026-10-17T10:00:00Z", EventType::Wake, false);
    assert(db.value().importEvents({replacement}, true, false).ok());
    all = db.value().queryEvents(EventQuery{});
    assert(all.value().size() == 1);
    assert(all.value()[0].title == "Only");
    assert(all.value()[0].type == EventType::Wake);

    // Preserve ids is explicit, and works with a replace.
    Event first = makeEvent(10, "Ten", "2026-10-17T09:00:00Z", "2026-10-17T10:00:00Z",
                            EventType::Event, false);
    Event second = makeEvent(11, "Eleven", "2026-10-17T11:00:00Z",
                             "2026-10-17T12:00:00Z", EventType::Event, false);
    first.createdUtc = 123;
    first.updatedUtc = 456;
    assert(db.value().importEvents({first, second}, true, true).ok());
    all = db.value().queryEvents(EventQuery{});
    assert(all.value().size() == 2);
    assert(all.value()[0].id == 10);
    assert(all.value()[0].createdUtc == 123);
    assert(all.value()[0].updatedUtc == 456);
    assert(all.value()[1].id == 11);

    // A collision on append rolls back, leaving the previous rows intact.
    auto collision = db.value().importEvents({first}, false, true);
    assert(!collision.ok());
    all = db.value().queryEvents(EventQuery{});
    assert(all.value().size() == 2);

    // Preserve ids cannot invent an id.
    Event noId = makeEvent(0, "No id", "2026-10-17T13:00:00Z",
                           "2026-10-17T14:00:00Z", EventType::Event, false);
    assert(!db.value().importEvents({noId}, false, true).ok());
    all = db.value().queryEvents(EventQuery{});
    assert(all.value().size() == 2);

    // A replace that fails halfway must restore the rows it deleted.
    Event duplicate = first;
    duplicate.title = "Collision";
    assert(!db.value().importEvents({first, duplicate}, true, true).ok());
    all = db.value().queryEvents(EventQuery{});
    assert(all.value().size() == 2);
    assert(all.value()[0].id == 10);
    assert(all.value()[0].title == "Ten");
    assert(all.value()[1].id == 11);
}

void testFormatNames() {
    ImportFormat format = ImportFormat::Csv;
    assert(importFormatFromName("csv", format) && format == ImportFormat::Csv);
    assert(importFormatFromName("jsonl", format) && format == ImportFormat::Jsonl);
    assert(importFormatFromName("json", format) && format == ImportFormat::Jsonl);
    assert(!importFormatFromName("ics", format));
    assert(std::string(importFormatName(ImportFormat::Jsonl)) == "jsonl");
}

} // namespace

int main() {
    testRoundTrip(fixture());
    testCsvHeaderByColumnAndDefaults();
    testCsvBomAndCrlf();
    testCsvErrors();
    testJsonlDefaultsAndEscapes();
    testJsonlErrors();
    testUnknownJsonScalarIsIgnored();
    testDbImportAppendReplaceAndIds();
    testFormatNames();
    std::printf("import_test: all assertions passed\n");
    return 0;
}
