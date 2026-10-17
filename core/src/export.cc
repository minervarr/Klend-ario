#include "klend_ario/export.hh"

#include <algorithm>
#include <cstdio>
#include <string>

namespace klend_ario {
namespace {

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
                out.push_back(c);
            }
        }
    }
    return out;
}

// RFC 5545 TEXT: backslash, semicolon, comma and newline are escaped. CRLF is
// one logical newline, not two.
std::string icsEscape(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case ';':
            out += "\\;";
            break;
        case ',':
            out += "\\,";
            break;
        case '\r':
            if (i + 1 < in.size() && in[i + 1] == '\n') {
                ++i;
            }
            out += "\\n";
            break;
        case '\n':
            out += "\\n";
            break;
        default:
            out.push_back(c);
        }
    }
    return out;
}

std::string icsStamp(long long utcSeconds) {
    const std::string iso = formatIso8601(utcSeconds); // YYYY-MM-DDTHH:MM:SSZ
    std::string out;
    out.reserve(iso.size());
    for (const char c : iso) {
        if (c != '-' && c != ':') {
            out.push_back(c);
        }
    }
    return out;
}

// A UTF-8 continuation byte is 10xxxxxx. Backing up to a lead byte keeps a fold
// from slicing a codepoint in half, which some clients render as a replacement
// character.
size_t utf8SafeCut(std::string_view text, size_t limit) {
    if (text.size() <= limit) {
        return text.size();
    }
    size_t cut = limit;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
        --cut;
    }
    return cut == 0 ? limit : cut;
}

// RFC 5545 line folding. The first line may be 75 octets; each continuation
// starts with a space, so the payload after that space is at most 74 octets.
// Every returned line ends in CRLF.
std::string foldIcsLine(std::string_view line) {
    std::string out;
    size_t offset = 0;
    size_t payloadLimit = 75;
    while (offset < line.size()) {
        const std::string_view rest = line.substr(offset);
        const size_t cut = utf8SafeCut(rest, payloadLimit);
        out.append(rest.substr(0, cut));
        out += "\r\n";
        offset += cut;
        if (offset < line.size()) {
            out.push_back(' ');
            payloadLimit = 74;
        }
    }
    if (line.empty()) {
        out += "\r\n";
    }
    return out;
}

// ISO-8601 duration without the sign: 600 -> PT10M, 90061 -> P1DT1H1M1S.
std::string icsDuration(long long seconds) {
    if (seconds < 0) {
        seconds = -seconds;
    }
    if (seconds == 0) {
        return "PT0S";
    }

    const long long days = seconds / 86400;
    seconds %= 86400;
    const long long hours = seconds / 3600;
    seconds %= 3600;
    const long long minutes = seconds / 60;
    const long long secs = seconds % 60;

    std::string out = "P";
    if (days != 0) {
        out += std::to_string(days) + "D";
    }
    if (hours != 0 || minutes != 0 || secs != 0) {
        out += "T";
        if (hours != 0) {
            out += std::to_string(hours) + "H";
        }
        if (minutes != 0) {
            out += std::to_string(minutes) + "M";
        }
        if (secs != 0) {
            out += std::to_string(secs) + "S";
        }
    }
    return out;
}

std::string eventJson(const Event &event) {
    std::string out = "{\"id\":";
    out += std::to_string(event.id);
    out += ",\"title\":\"";
    out += jsonEscape(event.title);
    out += "\",\"start\":\"";
    out += formatIso8601(event.startUtc);
    out += "\",\"end\":\"";
    out += formatIso8601(event.endUtc);
    out += "\",\"type\":\"";
    out += eventTypeName(event.type);
    out += "\",\"importance\":";
    out += std::to_string(event.importance);
    out += ",\"urgency\":";
    out += std::to_string(event.urgency);
    out += ",\"alarm_offset\":";
    out += std::to_string(event.alarmOffset);
    out += ",\"hidden\":";
    out += event.hidden ? "true" : "false";
    out += ",\"notes\":\"";
    out += jsonEscape(event.notes);
    out += "\",\"created_utc\":";
    out += std::to_string(event.createdUtc);
    out += ",\"updated_utc\":";
    out += std::to_string(event.updatedUtc);
    out.push_back('}');
    return out;
}

std::string eventCsvRow(const Event &event) {
    return csvField(std::to_string(event.id)) + "," + csvField(event.title) + "," +
           csvField(formatIso8601(event.startUtc)) + "," + csvField(formatIso8601(event.endUtc)) +
           "," + csvField(eventTypeName(event.type)) + "," +
           std::to_string(event.importance) + "," + std::to_string(event.urgency) + "," +
           std::to_string(event.alarmOffset) + "," + (event.hidden ? "true" : "false") + "," +
           csvField(event.notes) + "," + std::to_string(event.createdUtc) + "," +
           std::to_string(event.updatedUtc) + "\n";
}

std::string icsEvent(const Event &event) {
    std::string out;
    out += foldIcsLine("BEGIN:VEVENT");
    out += foldIcsLine("UID:klend-ario-" + std::to_string(event.id) + "@klend-ario.invalid");
    out += foldIcsLine("DTSTAMP:" +
                       icsStamp(event.updatedUtc != 0 ? event.updatedUtc : event.startUtc));
    out += foldIcsLine("DTSTART:" + icsStamp(event.startUtc));
    out += foldIcsLine("DTEND:" + icsStamp(event.endUtc));
    out += foldIcsLine("SUMMARY:" + icsEscape(event.title));
    if (!event.notes.empty()) {
        out += foldIcsLine("DESCRIPTION:" + icsEscape(event.notes));
    }
    out += foldIcsLine("X-KLEND-ARIO-TYPE:" + std::string(eventTypeName(event.type)));
    out += foldIcsLine("X-KLEND-ARIO-IMPORTANCE:" + std::to_string(event.importance));
    out += foldIcsLine("X-KLEND-ARIO-URGENCY:" + std::to_string(event.urgency));
    out += foldIcsLine(std::string("X-KLEND-ARIO-HIDDEN:") + (event.hidden ? "TRUE" : "FALSE"));

    if (event.alarmOffset != 0) {
        out += foldIcsLine("BEGIN:VALARM");
        out += foldIcsLine("ACTION:DISPLAY");
        out += foldIcsLine("TRIGGER:" + std::string(event.alarmOffset > 0 ? "-" : "") +
                           icsDuration(event.alarmOffset));
        out += foldIcsLine("DESCRIPTION:" + icsEscape(event.title));
        out += foldIcsLine("END:VALARM");
    }
    out += foldIcsLine("END:VEVENT");
    return out;
}

} // namespace

const char *exportFormatName(ExportFormat format) {
    switch (format) {
    case ExportFormat::Csv:
        return "csv";
    case ExportFormat::Jsonl:
        return "jsonl";
    case ExportFormat::Ics:
        return "ics";
    }
    return "csv";
}

bool exportFormatFromName(std::string_view name, ExportFormat &out) {
    if (name == "csv") {
        out = ExportFormat::Csv;
        return true;
    }
    if (name == "jsonl" || name == "json") {
        out = ExportFormat::Jsonl;
        return true;
    }
    if (name == "ics") {
        out = ExportFormat::Ics;
        return true;
    }
    return false;
}

std::string exportEvents(const std::vector<Event> &events, ExportFormat format) {
    if (format == ExportFormat::Csv) {
        std::string out =
            "id,title,start,end,type,importance,urgency,alarm_offset,hidden,notes,"
            "created_utc,updated_utc\n";
        for (const Event &event : events) {
            out += eventCsvRow(event);
        }
        return out;
    }
    if (format == ExportFormat::Jsonl) {
        std::string out;
        for (const Event &event : events) {
            out += eventJson(event);
            out.push_back('\n');
        }
        return out;
    }

    std::string out;
    out += foldIcsLine("BEGIN:VCALENDAR");
    out += foldIcsLine("VERSION:2.0");
    out += foldIcsLine("PRODID:-//Klend-ario//Klend-ario CLI//EN");
    out += foldIcsLine("CALSCALE:GREGORIAN");
    out += foldIcsLine("X-WR-CALNAME:Klend-ario");
    for (const Event &event : events) {
        out += icsEvent(event);
    }
    out += foldIcsLine("END:VCALENDAR");
    return out;
}

} // namespace klend_ario
