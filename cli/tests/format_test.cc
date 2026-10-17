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

long long instant(const char *text) {
    const auto value = parseIso8601(text);
    assert(value.has_value());
    return *value;
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

void testIntelligenceFormats() {
    const Conflict conflict{1, "Standup", 4, "Nested", instant("2026-10-16T09:30:00Z"),
                            instant("2026-10-16T09:45:00Z")};
    assert(formatConflicts({conflict}, OutputFormat::Text, {}) ==
           "1 <-> 4  2026-10-16T09:30:00Z  2026-10-16T09:45:00Z  Standup / Nested\n");
    assert(formatConflicts({conflict}, OutputFormat::Json, {}) ==
           "[{\"first_id\":1,\"first_title\":\"Standup\",\"second_id\":4,"
           "\"second_title\":\"Nested\",\"start\":\"2026-10-16T09:30:00Z\","
           "\"end\":\"2026-10-16T09:45:00Z\"}]\n");
    const std::string conflictCsv = formatConflicts({conflict}, OutputFormat::Csv, {});
    assert(conflictCsv.rfind("first_id,first_title,second_id,second_title,start,end\n", 0) == 0);
    assert(conflictCsv.find("1,Standup,4,Nested,2026-10-16T09:30:00Z,"
                            "2026-10-16T09:45:00Z\n") != std::string::npos);

    const FreeSlot slot{instant("2026-10-16T09:00:00Z"), instant("2026-10-16T09:30:00Z")};
    assert(formatFreeSlots({slot}, OutputFormat::Text, {}) ==
           "2026-10-16T09:00:00Z  2026-10-16T09:30:00Z  30m\n");
    assert(formatFreeSlots({slot}, OutputFormat::Json, {}) ==
           "[{\"start\":\"2026-10-16T09:00:00Z\",\"end\":\"2026-10-16T09:30:00Z\","
           "\"duration_seconds\":1800}]\n");
    const std::string freeCsv = formatFreeSlots({slot}, OutputFormat::Csv, {});
    assert(freeCsv.rfind("start,end,duration_seconds\n", 0) == 0);
    assert(freeCsv.find("2026-10-16T09:00:00Z,2026-10-16T09:30:00Z,1800\n") !=
           std::string::npos);

    const AlarmTrigger offset{1, instant("2026-10-16T08:50:00Z"), true, "Standup",
                              instant("2026-10-16T09:00:00Z")};
    const AlarmTrigger start{1, instant("2026-10-16T09:00:00Z"), false, "Standup",
                             instant("2026-10-16T09:00:00Z")};
    const AlarmTrigger after{1, instant("2026-10-16T09:10:00Z"), true, "Standup",
                             instant("2026-10-16T09:00:00Z")};
    assert(formatAlarms({offset, start, after}, OutputFormat::Text, {}) ==
           "2026-10-16T08:50:00Z  1  Standup  T-10m\n"
           "2026-10-16T09:00:00Z  1  Standup  T=0\n"
           "2026-10-16T09:10:00Z  1  Standup  T+10m\n");
    assert(formatAlarms({offset}, OutputFormat::Json, {}) ==
           "[{\"fire\":\"2026-10-16T08:50:00Z\",\"event_id\":1,\"title\":\"Standup\","
           "\"kind\":\"T-10m\",\"offset_seconds\":600}]\n");
    const std::string alarmCsv = formatAlarms({offset}, OutputFormat::Csv, {});
    assert(alarmCsv.rfind("fire,event_id,title,kind,offset_seconds\n", 0) == 0);
    assert(alarmCsv.find("2026-10-16T08:50:00Z,1,Standup,T-10m,600\n") !=
           std::string::npos);
}

void testStatsAndDiagnosticsFormats() {
    CalendarStats stats;
    stats.total = 5;
    stats.visible = 4;
    stats.hidden = 1;
    stats.byType = {2, 1, 1, 1};
    stats.upcoming = 2;
    stats.ongoing = 1;
    stats.past = 2;
    stats.scheduledSeconds = 12 * 60 * 60;
    stats.conflictPairs = 1;
    stats.earliestStartUtc = instant("2026-10-16T09:00:00Z");
    stats.latestEndUtc = instant("2026-10-17T07:00:00Z");

    assert(formatStats(stats, OutputFormat::Text, {}) ==
           "total       5\n"
           "visible     4\n"
           "hidden      1\n"
           "event       2\n"
           "sleep       1\n"
           "wake        1\n"
           "alarm       1\n"
           "upcoming    2\n"
           "ongoing     1\n"
           "past        2\n"
           "scheduled   12h\n"
           "conflicts   1\n"
           "earliest    2026-10-16T09:00:00Z\n"
           "latest      2026-10-17T07:00:00Z\n");
    assert(formatStats(stats, OutputFormat::Json, {}) ==
           "{\"total\":5,\"visible\":4,\"hidden\":1,\"by_type\":{\"event\":2,"
           "\"sleep\":1,\"wake\":1,\"alarm\":1},\"upcoming\":2,\"ongoing\":1,"
           "\"past\":2,\"scheduled_seconds\":43200,\"conflict_pairs\":1,"
           "\"earliest\":\"2026-10-16T09:00:00Z\","
           "\"latest\":\"2026-10-17T07:00:00Z\"}\n");
    const std::string statsCsv = formatStats(stats, OutputFormat::Csv, {});
    assert(statsCsv.rfind("metric,value\n", 0) == 0);
    assert(statsCsv.find("scheduled_seconds,43200\n") != std::string::npos);
    assert(statsCsv.find("latest,2026-10-17T07:00:00Z\n") != std::string::npos);

    CalendarStats empty;
    assert(formatStats(empty, OutputFormat::Json, {}).find("\"earliest\":null") !=
           std::string::npos);

    Diagnostics report;
    report.checks = {
        DiagnosticCheck{"integrity", CheckStatus::Ok, "ok"},
        DiagnosticCheck{"modified", CheckStatus::Warn, "meta.modified_utc is missing"},
    };
    assert(formatDiagnostics(report, OutputFormat::Text, {}) ==
           "[ok] integrity: ok\n"
           "[warn] modified: meta.modified_utc is missing\n"
           "healthy: no\n");
    assert(formatDiagnostics(report, OutputFormat::Json, {}) ==
           "{\"healthy\":false,\"checks\":["
           "{\"name\":\"integrity\",\"status\":\"ok\",\"detail\":\"ok\"},"
           "{\"name\":\"modified\",\"status\":\"warn\","
           "\"detail\":\"meta.modified_utc is missing\"}]}\n");
    const std::string diagnosticsCsv = formatDiagnostics(report, OutputFormat::Csv, {});
    assert(diagnosticsCsv.rfind("status,check,detail\n", 0) == 0);
    assert(diagnosticsCsv.find("warn,modified,meta.modified_utc is missing\n") !=
           std::string::npos);

    assert(formatImportSummary(ImportSummary{7, 7, false}) == "imported 7 events\n");
    assert(formatImportSummary(ImportSummary{7, 0, true}) ==
           "parsed 7 events (dry run)\n");
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
    testIntelligenceFormats();
    testStatsAndDiagnosticsFormats();
    std::printf("format_test: all assertions passed\n");
    return 0;
}
