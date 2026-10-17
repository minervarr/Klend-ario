#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "klend_ario/event.hh"
#include "klend_ario/export.hh"

using namespace klend_ario;

namespace {

long long at(const char *text) {
    const auto value = parseIso8601(text);
    assert(value.has_value());
    return *value;
}

Event makeEvent() {
    Event event;
    event.id = 7;
    event.title = "Standup";
    event.startUtc = at("2026-10-16T09:00:00Z");
    event.endUtc = at("2026-10-16T09:30:00Z");
    event.type = EventType::Event;
    event.importance = 80;
    event.urgency = 10;
    event.alarmOffset = 600;
    event.hidden = false;
    event.notes = "daily \"sync\", ok";
    event.createdUtc = at("2026-10-16T08:00:00Z");
    event.updatedUtc = at("2026-10-16T08:30:00Z");
    return event;
}

void testFormatNames() {
    ExportFormat format = ExportFormat::Csv;
    assert(exportFormatFromName("csv", format) && format == ExportFormat::Csv);
    assert(exportFormatFromName("jsonl", format) && format == ExportFormat::Jsonl);
    assert(exportFormatFromName("json", format) && format == ExportFormat::Jsonl);
    assert(exportFormatFromName("ics", format) && format == ExportFormat::Ics);
    assert(!exportFormatFromName("xml", format));
    assert(std::string(exportFormatName(ExportFormat::Ics)) == "ics");
}

void testCsv() {
    const std::string csv = exportEvents({makeEvent()}, ExportFormat::Csv);
    assert(csv.rfind("id,title,start,end,type,importance,urgency,alarm_offset,hidden,notes,"
                     "created_utc,updated_utc\n",
                     0) == 0);
    assert(csv.find("7,Standup,2026-10-16T09:00:00Z,2026-10-16T09:30:00Z,"
                    "event,80,10,600,false,\"daily \"\"sync\"\", ok\","
                    "1792137600,1792139400\n") != std::string::npos);
}

void testJsonl() {
    const std::string jsonl = exportEvents({makeEvent()}, ExportFormat::Jsonl);
    assert(jsonl ==
           "{\"id\":7,\"title\":\"Standup\",\"start\":\"2026-10-16T09:00:00Z\","
           "\"end\":\"2026-10-16T09:30:00Z\",\"type\":\"event\",\"importance\":80,"
           "\"urgency\":10,\"alarm_offset\":600,\"hidden\":false,"
           "\"notes\":\"daily \\\"sync\\\", ok\",\"created_utc\":"
           "1792137600,\"updated_utc\":1792139400}\n");
}

void testIcs() {
    const std::string ics = exportEvents({makeEvent()}, ExportFormat::Ics);
    const std::string expected =
        "BEGIN:VCALENDAR\r\n"
        "VERSION:2.0\r\n"
        "PRODID:-//Klend-ario//Klend-ario CLI//EN\r\n"
        "CALSCALE:GREGORIAN\r\n"
        "X-WR-CALNAME:Klend-ario\r\n"
        "BEGIN:VEVENT\r\n"
        "UID:klend-ario-7@klend-ario.invalid\r\n"
        "DTSTAMP:20261016T083000Z\r\n"
        "DTSTART:20261016T090000Z\r\n"
        "DTEND:20261016T093000Z\r\n"
        "SUMMARY:Standup\r\n"
        "DESCRIPTION:daily \"sync\"\\, ok\r\n"
        "X-KLEND-ARIO-TYPE:event\r\n"
        "X-KLEND-ARIO-IMPORTANCE:80\r\n"
        "X-KLEND-ARIO-URGENCY:10\r\n"
        "X-KLEND-ARIO-HIDDEN:FALSE\r\n"
        "BEGIN:VALARM\r\n"
        "ACTION:DISPLAY\r\n"
        "TRIGGER:-PT10M\r\n"
        "DESCRIPTION:Standup\r\n"
        "END:VALARM\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n";
    assert(ics == expected);
}

void testIcsHiddenNegativeOffsetAndTypes() {
    Event event = makeEvent();
    event.id = 9;
    event.title = "Nap";
    event.type = EventType::Sleep;
    event.hidden = true;
    event.alarmOffset = -300;
    event.notes.clear();
    event.updatedUtc = 0; // falls back to the start
    const std::string ics = exportEvents({event}, ExportFormat::Ics);
    assert(ics.find("DTSTAMP:20261016T090000Z\r\n") != std::string::npos);
    assert(ics.find("X-KLEND-ARIO-TYPE:sleep\r\n") != std::string::npos);
    assert(ics.find("X-KLEND-ARIO-HIDDEN:TRUE\r\n") != std::string::npos);
    assert(ics.find("TRIGGER:PT5M\r\n") != std::string::npos);
    assert(ics.find("DESCRIPTION:daily") == std::string::npos); // empty notes are omitted

    event.alarmOffset = 0;
    const std::string noAlarm = exportEvents({event}, ExportFormat::Ics);
    assert(noAlarm.find("BEGIN:VALARM") == std::string::npos);

    for (const EventType type :
         {EventType::Event, EventType::Sleep, EventType::Wake, EventType::Alarm}) {
        event.type = type;
        event.alarmOffset = 60;
        const std::string typeIcs = exportEvents({event}, ExportFormat::Ics);
        assert(typeIcs.find("X-KLEND-ARIO-TYPE:" + std::string(eventTypeName(type)) + "\r\n") !=
               std::string::npos);
    }
}

bool validUtf8(std::string_view text) {
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        size_t extra = 0;
        if (c < 0x80) {
            extra = 0;
        } else if ((c & 0xE0) == 0xC0) {
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
        } else {
            return false;
        }
        if (i + extra >= text.size()) {
            return false;
        }
        for (size_t j = 1; j <= extra; ++j) {
            if ((static_cast<unsigned char>(text[i + j]) & 0xC0) != 0x80) {
                return false;
            }
        }
        i += extra + 1;
    }
    return true;
}

void testIcsFolding() {
    Event event = makeEvent();
    event.title = std::string(100, 'A');
    event.notes = "line one\nline two; with, punctuation \\ and more";
    event.alarmOffset = 0;
    const std::string ics = exportEvents({event}, ExportFormat::Ics);

    size_t start = 0;
    std::string unfolded;
    while (start < ics.size()) {
        const size_t end = ics.find("\r\n", start);
        assert(end != std::string::npos);
        const std::string line = ics.substr(start, end - start);
        assert(line.size() <= 75);
        assert(validUtf8(line));
        // Continuation lines begin with a space; putting the logical line back
        // together is how a client reads it.
        if (!unfolded.empty() && !line.empty() && line[0] == ' ') {
            unfolded += line.substr(1);
        } else {
            unfolded += line;
        }
        start = end + 2;
    }
    assert(unfolded.find("SUMMARY:" + std::string(100, 'A')) != std::string::npos);
    assert(unfolded.find("DESCRIPTION:line one\\nline two\\; with\\, punctuation \\\\ and more") !=
           std::string::npos);

    // A multibyte title must fold on a codepoint boundary: every line has to
    // remain valid UTF-8 on its own.
    event.title.clear();
    for (int i = 0; i < 40; ++i) {
        event.title += "\xC3\xA9"; // é
    }
    const std::string utf8Ics = exportEvents({event}, ExportFormat::Ics);
    start = 0;
    while (start < utf8Ics.size()) {
        const size_t end = utf8Ics.find("\r\n", start);
        assert(end != std::string::npos);
        assert(validUtf8(utf8Ics.substr(start, end - start)));
        start = end + 2;
    }
}

void testEmptyExports() {
    assert(exportEvents({}, ExportFormat::Csv) ==
           "id,title,start,end,type,importance,urgency,alarm_offset,hidden,notes,"
           "created_utc,updated_utc\n");
    assert(exportEvents({}, ExportFormat::Jsonl).empty());
    const std::string ics = exportEvents({}, ExportFormat::Ics);
    assert(ics.rfind("BEGIN:VCALENDAR\r\n", 0) == 0);
    assert(ics.find("END:VCALENDAR\r\n") != std::string::npos);
    assert(ics.find("BEGIN:VEVENT") == std::string::npos);
}

} // namespace

int main() {
    testFormatNames();
    testCsv();
    testJsonl();
    testIcs();
    testIcsHiddenNegativeOffsetAndTypes();
    testIcsFolding();
    testEmptyExports();
    std::printf("export_test: all assertions passed\n");
    return 0;
}
