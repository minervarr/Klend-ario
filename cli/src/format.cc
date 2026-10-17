#include "format.hh"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace klend_ario::cli {
namespace {

std::string jsonEscape(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (const char c : in) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x",
                              static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += buffer;
            } else {
                out += c;
            }
        }
    }
    return out;
}

// RFC 4180: quote only when needed, and double an embedded quote.
std::string csvField(std::string_view in) {
    const bool needsQuotes = in.find_first_of(",\"\r\n") != std::string_view::npos;
    if (!needsQuotes) {
        return std::string(in);
    }
    std::string out;
    out.reserve(in.size() + 2);
    out.push_back('"');
    for (const char c : in) {
        if (c == '"') {
            out += "\"\"";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

// TSV has no quoting convention that spreadsheets agree on, so a literal tab or
// newline is escaped rather than emitted raw. A consumer un-escapes the same
// way; the output stays one record per line.
std::string tsvField(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (const char c : in) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        default:
            out.push_back(c);
        }
    }
    return out;
}

// Byte length is enough for the ASCII surface this CLI emits; count UTF-8
// codepoints so a title with accents does not drag a column out of line.
size_t displayWidth(std::string_view text) {
    size_t width = 0;
    for (const char c : text) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
            ++width;
        }
    }
    return width;
}

std::string padRight(std::string_view text, size_t width) {
    std::string out(text);
    const size_t current = displayWidth(text);
    if (current < width) {
        out.append(width - current, ' ');
    }
    return out;
}

std::string padLeft(std::string_view text, size_t width) {
    const size_t current = displayWidth(text);
    if (current >= width) {
        return std::string(text);
    }
    return std::string(width - current, ' ') + std::string(text);
}

using Cells = std::vector<std::string>;

// The flag says whether a column is numeric and therefore right-aligned. It is
// passed alongside the cells rather than guessed from them: a title of "42"
// must not move to the right-hand edge.
struct TableRow {
    Cells cells;
    std::vector<bool> rightAligned;
};

std::string renderTable(const Cells &headers, const std::vector<TableRow> &rows,
                        const std::vector<bool> &rightAligned) {
    std::vector<size_t> widths;
    widths.reserve(headers.size());
    for (size_t column = 0; column < headers.size(); ++column) {
        size_t width = displayWidth(headers[column]);
        for (const TableRow &row : rows) {
            if (column < row.cells.size()) {
                width = std::max(width, displayWidth(row.cells[column]));
            }
        }
        widths.push_back(width);
    }

    std::string out;
    auto appendRow = [&](const Cells &cells, const std::vector<bool> &alignRight) {
        for (size_t column = 0; column < headers.size(); ++column) {
            const std::string_view cell = column < cells.size() ? std::string_view(cells[column])
                                                               : std::string_view();
            if (column != 0) {
                out += "  ";
            }
            const bool right = column < alignRight.size() ? alignRight[column] : rightAligned[column];
            out += right ? padLeft(cell, widths[column]) : padRight(cell, widths[column]);
        }
        out.push_back('\n');
    };

    appendRow(headers, rightAligned);
    for (const TableRow &row : rows) {
        appendRow(row.cells, row.rightAligned);
    }
    return out;
}

std::string eventTypeOf(const Event &event) { return eventTypeName(event.type); }

Cells eventCells(const Event &event) {
    return {std::to_string(event.id),         formatIso8601(event.startUtc),
            formatIso8601(event.endUtc),      event.title,
            eventTypeOf(event),               std::to_string(event.importance),
            std::to_string(event.urgency),    std::to_string(event.alarmOffset),
            event.hidden ? "yes" : "no"};
}

const Cells &eventHeaders() {
    static const Cells headers = {"ID",   "START", "END",   "TITLE", "TYPE",
                                  "IMP",  "URG",   "ALM",   "HID"};
    return headers;
}

const std::vector<bool> &eventAlignment() {
    // ID, IMP, URG, ALM are numbers. START/END/TITLE/TYPE/HID read left to
    // right like prose.
    static const std::vector<bool> right = {true, false, false, false, false,
                                            true, true,  true,  false};
    return right;
}

Cells holidayCells(const holidays::Holiday &holiday) {
    return {holiday.date.toString(), std::string(holiday.name),
            holiday.nonWorking ? "yes" : "no",
            holiday.observed ? holiday.observed->toString() : std::string("-")};
}

const Cells &holidayHeaders() {
    static const Cells headers = {"DATE", "NAME", "NON-WORKING", "OBSERVED"};
    return headers;
}

std::string eventJson(const Event &event) {
    char buffer[64];
    std::string out = "{\"id\":";
    out += std::to_string(event.id);
    out += ",\"title\":\"";
    out += jsonEscape(event.title);
    out += "\",\"start\":\"";
    out += formatIso8601(event.startUtc);
    out += "\",\"end\":\"";
    out += formatIso8601(event.endUtc);
    out += "\",\"type\":\"";
    out += eventTypeOf(event);
    out += "\",\"importance\":";
    std::snprintf(buffer, sizeof(buffer), "%d", event.importance);
    out += buffer;
    out += ",\"urgency\":";
    std::snprintf(buffer, sizeof(buffer), "%d", event.urgency);
    out += buffer;
    out += ",\"alarm_offset\":";
    out += std::to_string(event.alarmOffset);
    out += ",\"hidden\":";
    out += event.hidden ? "true" : "false";
    out += ",\"notes\":\"";
    out += jsonEscape(event.notes);
    out += "\"}";
    return out;
}

std::string holidayJson(const holidays::Holiday &holiday) {
    std::string out = "{\"date\":\"";
    out += holiday.date.toString();
    out += "\",\"name\":\"";
    out += jsonEscape(holiday.name);
    out += "\",\"non_working\":";
    out += holiday.nonWorking ? "true" : "false";
    out += ",\"observed\":";
    if (holiday.observed) {
        out += "\"";
        out += holiday.observed->toString();
        out += "\"";
    } else {
        out += "null";
    }
    out.push_back('}');
    return out;
}

std::string eventCsvRow(const Event &event) {
    return csvField(std::to_string(event.id)) + "," + csvField(event.title) + "," +
           csvField(formatIso8601(event.startUtc)) + "," + csvField(formatIso8601(event.endUtc)) +
           "," + csvField(eventTypeOf(event)) + "," + std::to_string(event.importance) + "," +
           std::to_string(event.urgency) + "," + std::to_string(event.alarmOffset) + "," +
           (event.hidden ? "true" : "false") + "," + csvField(event.notes) + "\n";
}

std::string eventTsvRow(const Event &event) {
    return tsvField(std::to_string(event.id)) + "\t" + tsvField(event.title) + "\t" +
           tsvField(formatIso8601(event.startUtc)) + "\t" + tsvField(formatIso8601(event.endUtc)) +
           "\t" + tsvField(eventTypeOf(event)) + "\t" + std::to_string(event.importance) + "\t" +
           std::to_string(event.urgency) + "\t" + std::to_string(event.alarmOffset) + "\t" +
           (event.hidden ? "true" : "false") + "\t" + tsvField(event.notes) + "\n";
}

std::string eventHeaderRow(OutputFormat format) {
    const std::string separator = format == OutputFormat::Csv ? "," : "\t";
    return "id" + separator + "title" + separator + "start" + separator + "end" + separator +
           "type" + separator + "importance" + separator + "urgency" + separator +
           "alarm_offset" + separator + "hidden" + separator + "notes\n";
}

std::string holidayHeaderRow(OutputFormat format) {
    const std::string separator = format == OutputFormat::Csv ? "," : "\t";
    return "date" + separator + "name" + separator + "non_working" + separator + "observed\n";
}

std::string holidayRow(const holidays::Holiday &holiday, OutputFormat format) {
    const std::string observed = holiday.observed ? holiday.observed->toString() : "";
    if (format == OutputFormat::Csv) {
        return csvField(holiday.date.toString()) + "," + csvField(holiday.name) + "," +
               (holiday.nonWorking ? "true" : "false") + "," + csvField(observed) + "\n";
    }
    return tsvField(holiday.date.toString()) + "\t" + tsvField(holiday.name) + "\t" +
           (holiday.nonWorking ? "true" : "false") + "\t" + tsvField(observed) + "\n";
}

// Exact, compact, and unambiguous: 5400 -> "1h30m", 90061 -> "1d1h1m1s".
// Seconds are included rather than rounded away, which matters for alarm
// offsets and for a free slot that is 29m59s rather than 30m.
std::string humanDuration(long long seconds) {
    if (seconds <= 0) {
        return "0s";
    }
    std::string out;
    auto append = [&](long long value, char unit) {
        if (value == 0) {
            return;
        }
        out += std::to_string(value);
        out.push_back(unit);
    };

    const long long days = seconds / 86400;
    seconds %= 86400;
    const long long hours = seconds / 3600;
    seconds %= 3600;
    const long long minutes = seconds / 60;
    const long long secs = seconds % 60;

    append(days, 'd');
    append(hours, 'h');
    append(minutes, 'm');
    append(secs, 's');
    return out.empty() ? "0s" : out;
}

std::string alarmKind(const AlarmTrigger &trigger) {
    const long long delta = trigger.eventStartUtc - trigger.fireUtc;
    if (delta == 0) {
        return "T=0";
    }
    if (delta > 0) {
        return "T-" + humanDuration(delta);
    }
    return "T+" + humanDuration(-delta);
}

} // namespace

bool parseOutputFormat(std::string_view name, OutputFormat &out) {
    if (name == "text") {
        out = OutputFormat::Text;
        return true;
    }
    if (name == "table") {
        out = OutputFormat::Table;
        return true;
    }
    if (name == "json") {
        out = OutputFormat::Json;
        return true;
    }
    if (name == "csv") {
        out = OutputFormat::Csv;
        return true;
    }
    if (name == "tsv") {
        out = OutputFormat::Tsv;
        return true;
    }
    return false;
}

const char *outputFormatName(OutputFormat format) {
    switch (format) {
    case OutputFormat::Text:
        return "text";
    case OutputFormat::Table:
        return "table";
    case OutputFormat::Json:
        return "json";
    case OutputFormat::Csv:
        return "csv";
    case OutputFormat::Tsv:
        return "tsv";
    }
    return "text";
}

std::string formatEventLine(const Event &event, OutputFormat format) {
    switch (format) {
    case OutputFormat::Text: {
        std::string out = std::to_string(event.id) + "  " + formatIso8601(event.startUtc) + "  " +
                          formatIso8601(event.endUtc) + "  " + event.title;
        if (event.type != EventType::Event) {
            out += "  [";
            out += eventTypeOf(event);
            out += "]";
        }
        if (event.hidden) {
            out += "  [hidden]";
        }
        out.push_back('\n');
        return out;
    }
    case OutputFormat::Table: {
        std::vector<TableRow> rows = {{eventCells(event), eventAlignment()}};
        return renderTable(eventHeaders(), rows, eventAlignment());
    }
    case OutputFormat::Json:
        return eventJson(event) + "\n";
    case OutputFormat::Csv:
        return eventHeaderRow(format) + eventCsvRow(event);
    case OutputFormat::Tsv:
        return eventHeaderRow(format) + eventTsvRow(event);
    }
    return {};
}

std::string formatEventDetail(const Event &event, OutputFormat format, const FormatOptions &options) {
    if (format == OutputFormat::Text) {
        std::string out = formatEventLine(event, format);
        if (!event.notes.empty()) {
            out += "    ";
            out += event.notes;
            out.push_back('\n');
        }
        return out;
    }
    if (format == OutputFormat::Table) {
        std::string out = formatEvents({event}, format, options);
        if (!event.notes.empty()) {
            out += "notes: ";
            out += event.notes;
            out.push_back('\n');
        }
        return out;
    }
    if (format == OutputFormat::Json) {
        return formatEventLine(event, format);
    }
    // csv/tsv: the detail is a one-row table, so the header is honoured.
    std::string out = options.header ? eventHeaderRow(format) : "";
    out += format == OutputFormat::Csv ? eventCsvRow(event) : eventTsvRow(event);
    return out;
}

std::string formatEvents(const std::vector<Event> &events, OutputFormat format,
                         const FormatOptions &options) {
    if (format == OutputFormat::Text) {
        std::string out;
        for (const Event &event : events) {
            out += formatEventLine(event, format);
        }
        return out;
    }
    if (format == OutputFormat::Table) {
        std::vector<TableRow> rows;
        rows.reserve(events.size());
        for (const Event &event : events) {
            rows.push_back({eventCells(event), eventAlignment()});
        }
        return options.header ? renderTable(eventHeaders(), rows, eventAlignment())
                              : [&] {
                                    // Headerless table: keep the widths, drop
                                    // the header line. The rows are the first
                                    // N-1 lines of the full render.
                                    const std::string full =
                                        renderTable(eventHeaders(), rows, eventAlignment());
                                    const size_t firstNewline = full.find('\n');
                                    return firstNewline == std::string::npos
                                               ? std::string()
                                               : full.substr(firstNewline + 1);
                                }();
    }
    if (format == OutputFormat::Json) {
        std::string out = "[";
        for (size_t i = 0; i < events.size(); ++i) {
            if (i != 0) {
                out += ",";
            }
            out += eventJson(events[i]);
        }
        out += "]\n";
        return out;
    }

    std::string out = options.header ? eventHeaderRow(format) : "";
    for (const Event &event : events) {
        out += format == OutputFormat::Csv ? eventCsvRow(event) : eventTsvRow(event);
    }
    return out;
}

std::string formatHolidays(const std::vector<holidays::Holiday> &holidays, OutputFormat format,
                           const FormatOptions &options) {
    if (format == OutputFormat::Text) {
        std::string out;
        for (const holidays::Holiday &holiday : holidays) {
            out += holiday.toString();
            out.push_back('\n');
        }
        return out;
    }
    if (format == OutputFormat::Table) {
        std::vector<TableRow> rows;
        rows.reserve(holidays.size());
        const std::vector<bool> alignment = {false, false, false, false};
        for (const holidays::Holiday &holiday : holidays) {
            rows.push_back({holidayCells(holiday), alignment});
        }
        const std::string full = renderTable(holidayHeaders(), rows, alignment);
        if (options.header) {
            return full;
        }
        const size_t firstNewline = full.find('\n');
        return firstNewline == std::string::npos ? std::string() : full.substr(firstNewline + 1);
    }
    if (format == OutputFormat::Json) {
        std::string out = "[";
        for (size_t i = 0; i < holidays.size(); ++i) {
            if (i != 0) {
                out += ",";
            }
            out += holidayJson(holidays[i]);
        }
        out += "]\n";
        return out;
    }

    std::string out = options.header ? holidayHeaderRow(format) : "";
    for (const holidays::Holiday &holiday : holidays) {
        out += holidayRow(holiday, format);
    }
    return out;
}

std::string formatConflicts(const std::vector<Conflict> &conflicts, OutputFormat format,
                            const FormatOptions &options) {
    if (format == OutputFormat::Text) {
        std::string out;
        for (const Conflict &conflict : conflicts) {
            out += std::to_string(conflict.firstId) + " <-> " +
                   std::to_string(conflict.secondId) + "  " +
                   formatIso8601(conflict.startUtc) + "  " + formatIso8601(conflict.endUtc) +
                   "  " + conflict.firstTitle + " / " + conflict.secondTitle + "\n";
        }
        return out;
    }
    if (format == OutputFormat::Table) {
        static const Cells headers = {"A", "B", "START", "END", "A TITLE", "B TITLE"};
        static const std::vector<bool> alignment = {true, true, false, false, false, false};
        std::vector<TableRow> rows;
        rows.reserve(conflicts.size());
        for (const Conflict &conflict : conflicts) {
            rows.push_back({{std::to_string(conflict.firstId), std::to_string(conflict.secondId),
                             formatIso8601(conflict.startUtc), formatIso8601(conflict.endUtc),
                             conflict.firstTitle, conflict.secondTitle},
                            alignment});
        }
        const std::string full = renderTable(headers, rows, alignment);
        if (options.header) {
            return full;
        }
        const size_t firstNewline = full.find('\n');
        return firstNewline == std::string::npos ? std::string() : full.substr(firstNewline + 1);
    }
    if (format == OutputFormat::Json) {
        std::string out = "[";
        for (size_t i = 0; i < conflicts.size(); ++i) {
            const Conflict &conflict = conflicts[i];
            if (i != 0) {
                out += ",";
            }
            out += "{\"first_id\":" + std::to_string(conflict.firstId) + ",\"first_title\":\"" +
                   jsonEscape(conflict.firstTitle) + "\",\"second_id\":" +
                   std::to_string(conflict.secondId) + ",\"second_title\":\"" +
                   jsonEscape(conflict.secondTitle) + "\",\"start\":\"" +
                   formatIso8601(conflict.startUtc) + "\",\"end\":\"" +
                   formatIso8601(conflict.endUtc) + "\"}";
        }
        out += "]\n";
        return out;
    }

    const std::string separator = format == OutputFormat::Csv ? "," : "\t";
    std::string out = options.header
                          ? "first_id" + separator + "first_title" + separator + "second_id" +
                                separator + "second_title" + separator + "start" + separator +
                                "end\n"
                          : "";
    for (const Conflict &conflict : conflicts) {
        if (format == OutputFormat::Csv) {
            out += csvField(std::to_string(conflict.firstId)) + "," +
                   csvField(conflict.firstTitle) + "," +
                   csvField(std::to_string(conflict.secondId)) + "," +
                   csvField(conflict.secondTitle) + "," +
                   csvField(formatIso8601(conflict.startUtc)) + "," +
                   csvField(formatIso8601(conflict.endUtc)) + "\n";
        } else {
            out += tsvField(std::to_string(conflict.firstId)) + "\t" +
                   tsvField(conflict.firstTitle) + "\t" +
                   tsvField(std::to_string(conflict.secondId)) + "\t" +
                   tsvField(conflict.secondTitle) + "\t" +
                   tsvField(formatIso8601(conflict.startUtc)) + "\t" +
                   tsvField(formatIso8601(conflict.endUtc)) + "\n";
        }
    }
    return out;
}

std::string formatFreeSlots(const std::vector<FreeSlot> &slots, OutputFormat format,
                            const FormatOptions &options) {
    if (format == OutputFormat::Text) {
        std::string out;
        for (const FreeSlot &slot : slots) {
            out += formatIso8601(slot.startUtc) + "  " + formatIso8601(slot.endUtc) + "  " +
                   humanDuration(slot.endUtc - slot.startUtc) + "\n";
        }
        return out;
    }
    if (format == OutputFormat::Table) {
        static const Cells headers = {"START", "END", "DURATION"};
        static const std::vector<bool> alignment = {false, false, false};
        std::vector<TableRow> rows;
        rows.reserve(slots.size());
        for (const FreeSlot &slot : slots) {
            rows.push_back({{formatIso8601(slot.startUtc), formatIso8601(slot.endUtc),
                             humanDuration(slot.endUtc - slot.startUtc)},
                            alignment});
        }
        const std::string full = renderTable(headers, rows, alignment);
        if (options.header) {
            return full;
        }
        const size_t firstNewline = full.find('\n');
        return firstNewline == std::string::npos ? std::string() : full.substr(firstNewline + 1);
    }
    if (format == OutputFormat::Json) {
        std::string out = "[";
        for (size_t i = 0; i < slots.size(); ++i) {
            if (i != 0) {
                out += ",";
            }
            out += "{\"start\":\"" + formatIso8601(slots[i].startUtc) + "\",\"end\":\"" +
                   formatIso8601(slots[i].endUtc) + "\",\"duration_seconds\":" +
                   std::to_string(slots[i].endUtc - slots[i].startUtc) + "}";
        }
        out += "]\n";
        return out;
    }

    const std::string separator = format == OutputFormat::Csv ? "," : "\t";
    std::string out = options.header
                          ? "start" + separator + "end" + separator + "duration_seconds\n"
                          : "";
    for (const FreeSlot &slot : slots) {
        if (format == OutputFormat::Csv) {
            out += csvField(formatIso8601(slot.startUtc)) + "," +
                   csvField(formatIso8601(slot.endUtc)) + "," +
                   std::to_string(slot.endUtc - slot.startUtc) + "\n";
        } else {
            out += tsvField(formatIso8601(slot.startUtc)) + "\t" +
                   tsvField(formatIso8601(slot.endUtc)) + "\t" +
                   std::to_string(slot.endUtc - slot.startUtc) + "\n";
        }
    }
    return out;
}

std::string formatAlarms(const std::vector<AlarmTrigger> &triggers, OutputFormat format,
                         const FormatOptions &options) {
    if (format == OutputFormat::Text) {
        std::string out;
        for (const AlarmTrigger &trigger : triggers) {
            out += formatIso8601(trigger.fireUtc) + "  " + std::to_string(trigger.eventId) +
                   "  " + trigger.title + "  " + alarmKind(trigger) + "\n";
        }
        return out;
    }
    if (format == OutputFormat::Table) {
        static const Cells headers = {"FIRE", "EVENT", "TITLE", "KIND"};
        static const std::vector<bool> alignment = {false, true, false, false};
        std::vector<TableRow> rows;
        rows.reserve(triggers.size());
        for (const AlarmTrigger &trigger : triggers) {
            rows.push_back({{formatIso8601(trigger.fireUtc), std::to_string(trigger.eventId),
                             trigger.title, alarmKind(trigger)},
                            alignment});
        }
        const std::string full = renderTable(headers, rows, alignment);
        if (options.header) {
            return full;
        }
        const size_t firstNewline = full.find('\n');
        return firstNewline == std::string::npos ? std::string() : full.substr(firstNewline + 1);
    }
    if (format == OutputFormat::Json) {
        std::string out = "[";
        for (size_t i = 0; i < triggers.size(); ++i) {
            const AlarmTrigger &trigger = triggers[i];
            if (i != 0) {
                out += ",";
            }
            const long long offsetSeconds = trigger.eventStartUtc - trigger.fireUtc;
            out += "{\"fire\":\"" + formatIso8601(trigger.fireUtc) + "\",\"event_id\":" +
                   std::to_string(trigger.eventId) + ",\"title\":\"" +
                   jsonEscape(trigger.title) + "\",\"kind\":\"" + alarmKind(trigger) +
                   "\",\"offset_seconds\":" + std::to_string(offsetSeconds) + "}";
        }
        out += "]\n";
        return out;
    }

    const std::string separator = format == OutputFormat::Csv ? "," : "\t";
    std::string out = options.header
                          ? "fire" + separator + "event_id" + separator + "title" + separator +
                                "kind" + separator + "offset_seconds\n"
                          : "";
    for (const AlarmTrigger &trigger : triggers) {
        const long long offsetSeconds = trigger.eventStartUtc - trigger.fireUtc;
        if (format == OutputFormat::Csv) {
            out += csvField(formatIso8601(trigger.fireUtc)) + "," +
                   csvField(std::to_string(trigger.eventId)) + "," +
                   csvField(trigger.title) + "," + csvField(alarmKind(trigger)) + "," +
                   std::to_string(offsetSeconds) + "\n";
        } else {
            out += tsvField(formatIso8601(trigger.fireUtc)) + "\t" +
                   tsvField(std::to_string(trigger.eventId)) + "\t" +
                   tsvField(trigger.title) + "\t" + tsvField(alarmKind(trigger)) + "\t" +
                   std::to_string(offsetSeconds) + "\n";
        }
    }
    return out;
}

namespace {

// Kept local so format_test does not have to link the diagnostics module (and
// sqlite) just to render a status word.
const char *statusLabel(CheckStatus status) {
    switch (status) {
    case CheckStatus::Ok:
        return "ok";
    case CheckStatus::Warn:
        return "warn";
    case CheckStatus::Fail:
        return "fail";
    }
    return "fail";
}

} // namespace

std::string formatStats(const CalendarStats &stats, OutputFormat format,
                        const FormatOptions &options) {
    const std::string scheduled = humanDuration(stats.scheduledSeconds);
    const std::string earliest =
        stats.earliestStartUtc ? formatIso8601(*stats.earliestStartUtc) : "-";
    const std::string latest = stats.latestEndUtc ? formatIso8601(*stats.latestEndUtc) : "-";

    const std::vector<std::pair<std::string, std::string>> metrics = {
        {"total", std::to_string(stats.total)},
        {"visible", std::to_string(stats.visible)},
        {"hidden", std::to_string(stats.hidden)},
        {"event", std::to_string(stats.byType[static_cast<size_t>(EventType::Event)])},
        {"sleep", std::to_string(stats.byType[static_cast<size_t>(EventType::Sleep)])},
        {"wake", std::to_string(stats.byType[static_cast<size_t>(EventType::Wake)])},
        {"alarm", std::to_string(stats.byType[static_cast<size_t>(EventType::Alarm)])},
        {"upcoming", std::to_string(stats.upcoming)},
        {"ongoing", std::to_string(stats.ongoing)},
        {"past", std::to_string(stats.past)},
        {"scheduled", scheduled},
        {"conflicts", std::to_string(stats.conflictPairs)},
        {"earliest", earliest},
        {"latest", latest},
    };

    if (format == OutputFormat::Text) {
        std::string out;
        for (const auto &[name, value] : metrics) {
            out += name;
            if (name.size() < 12) {
                out.append(12 - name.size(), ' ');
            }
            out += value;
            out.push_back('\n');
        }
        return out;
    }
    if (format == OutputFormat::Table) {
        static const Cells headers = {"METRIC", "VALUE"};
        static const std::vector<bool> alignment = {false, false};
        std::vector<TableRow> rows;
        rows.reserve(metrics.size());
        for (const auto &[name, value] : metrics) {
            rows.push_back({{name, value}, alignment});
        }
        const std::string full = renderTable(headers, rows, alignment);
        if (options.header) {
            return full;
        }
        const size_t firstNewline = full.find('\n');
        return firstNewline == std::string::npos ? std::string() : full.substr(firstNewline + 1);
    }
    if (format == OutputFormat::Json) {
        std::string out = "{\"total\":" + std::to_string(stats.total) +
                          ",\"visible\":" + std::to_string(stats.visible) +
                          ",\"hidden\":" + std::to_string(stats.hidden) + ",\"by_type\":{" +
                          "\"event\":" +
                          std::to_string(stats.byType[static_cast<size_t>(EventType::Event)]) +
                          ",\"sleep\":" +
                          std::to_string(stats.byType[static_cast<size_t>(EventType::Sleep)]) +
                          ",\"wake\":" +
                          std::to_string(stats.byType[static_cast<size_t>(EventType::Wake)]) +
                          ",\"alarm\":" +
                          std::to_string(stats.byType[static_cast<size_t>(EventType::Alarm)]) +
                          "},\"upcoming\":" + std::to_string(stats.upcoming) +
                          ",\"ongoing\":" + std::to_string(stats.ongoing) +
                          ",\"past\":" + std::to_string(stats.past) +
                          ",\"scheduled_seconds\":" + std::to_string(stats.scheduledSeconds) +
                          ",\"conflict_pairs\":" + std::to_string(stats.conflictPairs) +
                          ",\"earliest\":";
        out += stats.earliestStartUtc ? "\"" + formatIso8601(*stats.earliestStartUtc) + "\"" : "null";
        out += ",\"latest\":";
        out += stats.latestEndUtc ? "\"" + formatIso8601(*stats.latestEndUtc) + "\"" : "null";
        out += "}\n";
        return out;
    }

    const std::string separator = format == OutputFormat::Csv ? "," : "\t";
    std::string out = options.header ? "metric" + separator + "value\n" : "";
    const std::vector<std::pair<std::string, std::string>> machineMetrics = {
        {"total", std::to_string(stats.total)},
        {"visible", std::to_string(stats.visible)},
        {"hidden", std::to_string(stats.hidden)},
        {"event", std::to_string(stats.byType[static_cast<size_t>(EventType::Event)])},
        {"sleep", std::to_string(stats.byType[static_cast<size_t>(EventType::Sleep)])},
        {"wake", std::to_string(stats.byType[static_cast<size_t>(EventType::Wake)])},
        {"alarm", std::to_string(stats.byType[static_cast<size_t>(EventType::Alarm)])},
        {"upcoming", std::to_string(stats.upcoming)},
        {"ongoing", std::to_string(stats.ongoing)},
        {"past", std::to_string(stats.past)},
        {"scheduled_seconds", std::to_string(stats.scheduledSeconds)},
        {"conflict_pairs", std::to_string(stats.conflictPairs)},
        {"earliest", stats.earliestStartUtc ? formatIso8601(*stats.earliestStartUtc) : ""},
        {"latest", stats.latestEndUtc ? formatIso8601(*stats.latestEndUtc) : ""},
    };
    for (const auto &[name, value] : machineMetrics) {
        if (format == OutputFormat::Csv) {
            out += csvField(name) + "," + csvField(value) + "\n";
        } else {
            out += tsvField(name) + "\t" + tsvField(value) + "\n";
        }
    }
    return out;
}

std::string formatDiagnostics(const Diagnostics &diagnostics, OutputFormat format,
                              const FormatOptions &options) {
    // Same rule as Diagnostics::healthy(), computed locally so the renderer
    // does not need to link the diagnostics module (and sqlite) just to print.
    bool healthy = true;
    for (const DiagnosticCheck &entry : diagnostics.checks) {
        if (entry.status != CheckStatus::Ok) {
            healthy = false;
            break;
        }
    }

    if (format == OutputFormat::Text) {
        std::string out;
        for (const DiagnosticCheck &entry : diagnostics.checks) {
            out += "[";
            out += statusLabel(entry.status);
            out += "] ";
            out += entry.name;
            out += ": ";
            out += entry.detail;
            out.push_back('\n');
        }
        out += healthy ? "healthy: yes\n" : "healthy: no\n";
        return out;
    }
    if (format == OutputFormat::Table) {
        static const Cells headers = {"STATUS", "CHECK", "DETAIL"};
        static const std::vector<bool> alignment = {false, false, false};
        std::vector<TableRow> rows;
        rows.reserve(diagnostics.checks.size());
        for (const DiagnosticCheck &entry : diagnostics.checks) {
            rows.push_back({{statusLabel(entry.status), entry.name, entry.detail}, alignment});
        }
        const std::string full = renderTable(headers, rows, alignment);
        if (options.header) {
            return full;
        }
        const size_t firstNewline = full.find('\n');
        return firstNewline == std::string::npos ? std::string() : full.substr(firstNewline + 1);
    }
    if (format == OutputFormat::Json) {
        std::string out = healthy ? "{\"healthy\":true,\"checks\":["
                                  : "{\"healthy\":false,\"checks\":[";
        for (size_t i = 0; i < diagnostics.checks.size(); ++i) {
            const DiagnosticCheck &entry = diagnostics.checks[i];
            if (i != 0) {
                out += ",";
            }
            out += "{\"name\":\"" + jsonEscape(entry.name) + "\",\"status\":\"" +
                   statusLabel(entry.status) + "\",\"detail\":\"" + jsonEscape(entry.detail) +
                   "\"}";
        }
        out += "]}\n";
        return out;
    }

    const std::string separator = format == OutputFormat::Csv ? "," : "\t";
    std::string out = options.header ? "status" + separator + "check" + separator + "detail\n" : "";
    for (const DiagnosticCheck &entry : diagnostics.checks) {
        if (format == OutputFormat::Csv) {
            out += csvField(statusLabel(entry.status)) + "," + csvField(entry.name) + "," +
                   csvField(entry.detail) + "\n";
        } else {
            out += tsvField(statusLabel(entry.status)) + "\t" + tsvField(entry.name) + "\t" +
                   tsvField(entry.detail) + "\n";
        }
    }
    return out;
}

std::string formatImportSummary(const ImportSummary &summary) {
    if (summary.dryRun) {
        return "parsed " + std::to_string(summary.parsed) + " events (dry run)\n";
    }
    return "imported " + std::to_string(summary.inserted) + " events\n";
}

std::string formatId(long long id, OutputFormat format) {
    if (format == OutputFormat::Json) {
        return "{\"id\":" + std::to_string(id) + "}\n";
    }
    return std::to_string(id) + "\n";
}

} // namespace klend_ario::cli
