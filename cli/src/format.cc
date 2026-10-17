#include "format.hh"

#include <algorithm>
#include <cstdio>
#include <string>
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

std::string formatId(long long id, OutputFormat format) {
    if (format == OutputFormat::Json) {
        return "{\"id\":" + std::to_string(id) + "}\n";
    }
    return std::to_string(id) + "\n";
}

} // namespace klend_ario::cli
