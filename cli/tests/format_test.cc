#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>

#include "format.hh"

using namespace klend_ario;
using namespace klend_ario::cli;

namespace {

Event makeEvent() {
    Event e;
    e.id = 7;
    e.title = "Standup";
    e.startUtc = *parseIso8601("2026-10-16T09:00:00Z");
    e.endUtc = *parseIso8601("2026-10-16T09:30:00Z");
    e.importance = 80;
    e.urgency = 10;
    e.alarmOffset = -600;
    e.notes = "daily \"sync\"";
    return e;
}

void testExpressionParser() {
    OutputFormat format = OutputFormat::Text;
    assert(parseOutputFormat("json", format) && format == OutputFormat::Json);
    assert(parseOutputFormat("csv", format) && format == OutputFormat::Csv);
    assert(parseOutputFormat("tsv", format) && format == OutputFormat::Tsv);
    assert(parseOutputFormat("table", format) && format == OutputFormat::Table);
    assert(parseOutputFormat("text", format) && format == OutputFormat::Text);
    assert(!parseOutputFormat("xml", format));
    assert(std::string(outputFormatName(OutputFormat::Tsv)) == "tsv");
}

void testText() {
    const Event e = makeEvent();
    assert(formatEventLine(e, OutputFormat::Text) ==
           "7  2026-10-16T09:00:00Z  2026-10-16T09:30:00Z  Standup\n");
    // The detail form adds notes; the list form stays one line per event.
    assert(formatEventDetail(e, OutputFormat::Text, {}) ==
           "7  2026-10-16T09:00:00Z  2026-10-16T09:30:00Z  Standup\n"
           "    daily \"sync\"\n");

    Event sleep = e;
    sleep.type = EventType::Sleep;
    sleep.hidden = true;
    assert(formatEventLine(sleep, OutputFormat::Text) ==
           "7  2026-10-16T09:00:00Z  2026-10-16T09:30:00Z  Standup  [sleep]  [hidden]\n");
}

void testJson() {
    const Event e = makeEvent();
    assert(formatEventLine(e, OutputFormat::Json) ==
           "{\"id\":7,\"title\":\"Standup\",\"start\":\"2026-10-16T09:00:00Z\","
           "\"end\":\"2026-10-16T09:30:00Z\",\"type\":\"event\",\"importance\":80,"
           "\"urgency\":10,\"alarm_offset\":-600,\"hidden\":false,"
           "\"notes\":\"daily \\\"sync\\\"\"}\n");
    assert(formatEvents({e, e}, OutputFormat::Json, {}) ==
           "[" + formatEventLine(e, OutputFormat::Json).substr(0,
                                                                formatEventLine(e, OutputFormat::Json).size() - 1) +
               "," +
               formatEventLine(e, OutputFormat::Json).substr(0,
                                                              formatEventLine(e, OutputFormat::Json).size() - 1) +
               "]\n");
    assert(formatEvents({}, OutputFormat::Json, {}) == "[]\n");
    assert(formatId(9, OutputFormat::Json) == "{\"id\":9}\n");
}

void testCsvQuoting() {
    Event e = makeEvent();
    e.title = "Standup, daily";
    const FormatOptions options;
    const std::string out = formatEvents({e}, OutputFormat::Csv, options);
    assert(out.rfind("id,title,start,end,type,importance,urgency,alarm_offset,hidden,notes\n", 0) ==
           0);
    // A comma forces quotes; the embedded quotes in notes are doubled.
    assert(out.find("\"Standup, daily\"") != std::string::npos);
    assert(out.find("\"daily \"\"sync\"\"\"") != std::string::npos);
    assert(out.find("\n") != std::string::npos);

    const std::string headless = formatEvents({e}, OutputFormat::Csv, FormatOptions{false});
    assert(headless.rfind("7,", 0) == 0);
}

void testTsvEscapes() {
    Event e = makeEvent();
    e.title = "a\tb";
    e.notes = "line1\nline2";
    const std::string out = formatEvents({e}, OutputFormat::Tsv, FormatOptions{false});
    assert(out.rfind("7\t", 0) == 0);
    assert(out.find("a\\tb") != std::string::npos);
    assert(out.find("line1\\nline2") != std::string::npos);
    // One record per line, even though the note contains a newline.
    assert(out.find('\n') == out.size() - 1);
}

void testTable() {
    const Event e = makeEvent();
    const std::string out = formatEvents({e}, OutputFormat::Table, {});
    assert(out ==
           "ID  START                 END                   TITLE    TYPE   IMP  URG   ALM  HID\n"
           " 7  2026-10-16T09:00:00Z  2026-10-16T09:30:00Z  Standup  event   80   10  -600  no \n");

    // Headerless keeps the same column widths, only dropping the header line.
    const std::string headless = formatEvents({e}, OutputFormat::Table, FormatOptions{false});
    assert(headless.rfind(" 7  ", 0) == 0);
    assert(headless.find("START") == std::string::npos);

    // The detail form honours --no-header too, and still shows the notes.
    const std::string detail = formatEventDetail(e, OutputFormat::Table, FormatOptions{false});
    assert(detail.rfind(" 7  ", 0) == 0);
    assert(detail.find("START") == std::string::npos);
    assert(detail.find("notes: daily") != std::string::npos);
}

void testHolidays() {
    using klend_ario::holidays::Date;
    using klend_ario::holidays::Holiday;
    std::vector<Holiday> days = {Holiday(Date::create(2026, 10, 16), "Test day"),
                                 Holiday::nonWorkingDay(Date::create(2026, 10, 17), "Bridge")};
    assert(formatHolidays(days, OutputFormat::Text, {}) ==
           "2026-10-16 Test day\n2026-10-17 Bridge [día no laborable]\n");
    const std::string csv = formatHolidays(days, OutputFormat::Csv, {});
    assert(csv.rfind("date,name,non_working,observed\n", 0) == 0);
    assert(csv.find("2026-10-16,Test day,true,\n") != std::string::npos);
    assert(csv.find("2026-10-17,Bridge,false,\n") != std::string::npos);
    const std::string table = formatHolidays(days, OutputFormat::Table, {});
    assert(table.rfind("DATE        NAME      NON-WORKING  OBSERVED\n", 0) == 0);
    // --no-header keeps the records, not an empty page.
    const std::string headless = formatHolidays(days, OutputFormat::Table, FormatOptions{false});
    assert(headless.rfind("2026-10-16", 0) == 0);
    assert(headless.find("DATE") == std::string::npos);
}

} // namespace

int main() {
    testExpressionParser();
    testText();
    testJson();
    testCsvQuoting();
    testTsvEscapes();
    testTable();
    testHolidays();
    std::printf("format_test: all assertions passed\n");
    return 0;
}
