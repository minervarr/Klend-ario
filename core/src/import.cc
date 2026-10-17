#include "klend_ario/import.hh"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace klend_ario {
namespace {

arc::Error inputError(std::string message) {
    return arc::Error{arc::ErrorKind::Other, 0, std::move(message)};
}

std::string_view trimView(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' ||
                             text.front() == '\r' || text.front() == '\n')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' ||
                             text.back() == '\r' || text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return text;
}

bool allWhitespace(std::string_view text) { return trimView(text).empty(); }

bool parseLongLong(std::string_view text, long long &out) {
    text = trimView(text);
    if (text.empty()) {
        return false;
    }
    size_t i = 0;
    bool negative = false;
    if (text[i] == '-') {
        negative = true;
        ++i;
    }
    if (i >= text.size()) {
        return false;
    }
    long long value = 0;
    for (; i < text.size(); ++i) {
        const char c = text[i];
        if (c < '0' || c > '9') {
            return false;
        }
        const int digit = c - '0';
        if (value > (std::numeric_limits<long long>::max() - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }
    out = negative ? -value : value;
    return true;
}

bool parseBool(std::string_view text, bool &out) {
    std::string lower;
    lower.reserve(text.size());
    for (const char c : text) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (lower == "true" || lower == "1" || lower == "yes") {
        out = true;
        return true;
    }
    if (lower == "false" || lower == "0" || lower == "no") {
        out = false;
        return true;
    }
    return false;
}

bool knownCsvColumn(std::string_view name) {
    return name == "id" || name == "title" || name == "start" || name == "end" ||
           name == "type" || name == "importance" || name == "urgency" ||
           name == "alarm_offset" || name == "hidden" || name == "notes" ||
           name == "created_utc" || name == "updated_utc";
}

// ---------------------------------------------------------------------------
// CSV
// ---------------------------------------------------------------------------

struct CsvRow {
    std::vector<std::string> fields;
    int line = 1;
};

arc::Result<std::vector<CsvRow>> parseCsvRows(std::string_view text) {
    std::vector<CsvRow> rows;
    std::vector<std::string> row;
    std::string field;
    bool inQuotes = false;
    bool afterQuote = false;
    int line = 1;
    int rowLine = 1;

    auto endRow = [&]() {
        row.push_back(field);
        rows.push_back(CsvRow{row, rowLine});
        row.clear();
        field.clear();
        afterQuote = false;
        ++line;
        rowLine = line;
    };

    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field.push_back('"');
                    ++i;
                } else {
                    inQuotes = false;
                    afterQuote = true;
                }
            } else if (c == '\r') {
                if (i + 1 < text.size() && text[i + 1] == '\n') {
                    ++i;
                }
                field.push_back('\n');
                ++line;
            } else {
                field.push_back(c);
                if (c == '\n') {
                    ++line;
                }
            }
            continue;
        }

        if (afterQuote && c != ',' && c != '\n' && c != '\r') {
            return inputError("line " + std::to_string(rowLine) +
                              ": unexpected character after a quoted field");
        }
        if (c == '"') {
            if (!field.empty()) {
                return inputError("line " + std::to_string(rowLine) +
                                  ": quote inside an unquoted field");
            }
            inQuotes = true;
        } else if (c == ',') {
            row.push_back(field);
            field.clear();
            afterQuote = false;
        } else if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
            endRow();
        } else {
            field.push_back(c);
        }
    }

    if (inQuotes) {
        return inputError("line " + std::to_string(rowLine) + ": unterminated quoted field");
    }
    // A trailing newline leaves an empty row behind; do not invent a record.
    if (!field.empty() || !row.empty()) {
        row.push_back(field);
        rows.push_back(CsvRow{row, rowLine});
    }
    return rows;
}

struct ColumnMap {
    std::map<std::string, size_t, std::less<>> index;
};

std::string_view cell(const CsvRow &row, const ColumnMap &columns, std::string_view name) {
    const auto found = columns.index.find(name);
    if (found == columns.index.end() || found->second >= row.fields.size()) {
        return {};
    }
    return row.fields[found->second];
}

arc::Result<Event> eventFromCsv(const CsvRow &row, const ColumnMap &columns,
                                const std::vector<std::string> &header) {
    if (row.fields.size() != header.size()) {
        return inputError("line " + std::to_string(row.line) + ": expected " +
                          std::to_string(header.size()) + " fields, got " +
                          std::to_string(row.fields.size()));
    }

    const auto fail = [&](const std::string &message) {
        return inputError("line " + std::to_string(row.line) + ": " + message);
    };

    Event event;
    const std::string_view title = cell(row, columns, "title");
    if (trimView(title).empty()) {
        return fail("title must not be empty");
    }
    event.title = std::string(title);

    const auto start = parseIso8601(trimView(cell(row, columns, "start")));
    if (!start) {
        return fail("start is not an ISO-8601 UTC instant");
    }
    const auto end = parseIso8601(trimView(cell(row, columns, "end")));
    if (!end) {
        return fail("end is not an ISO-8601 UTC instant");
    }
    event.startUtc = *start;
    event.endUtc = *end;
    if (event.endUtc < event.startUtc) {
        return fail("end is before start");
    }

    const auto idText = trimView(cell(row, columns, "id"));
    if (!idText.empty()) {
        long long id = 0;
        if (!parseLongLong(idText, id) || id < 0) {
            return fail("id must be a non-negative integer");
        }
        event.id = id;
    }

    const auto typeText = trimView(cell(row, columns, "type"));
    if (!typeText.empty() && !eventTypeFromName(typeText, event.type)) {
        return fail("type must be event|sleep|wake|alarm");
    }

    const auto importanceText = trimView(cell(row, columns, "importance"));
    if (!importanceText.empty()) {
        long long value = 0;
        if (!parseLongLong(importanceText, value) || value < 0 || value > 100) {
            return fail("importance must be 0..100");
        }
        event.importance = static_cast<int>(value);
    }

    const auto urgencyText = trimView(cell(row, columns, "urgency"));
    if (!urgencyText.empty()) {
        long long value = 0;
        if (!parseLongLong(urgencyText, value) || value < 0 || value > 100) {
            return fail("urgency must be 0..100");
        }
        event.urgency = static_cast<int>(value);
    }

    const auto offsetText = trimView(cell(row, columns, "alarm_offset"));
    if (!offsetText.empty()) {
        long long value = 0;
        if (!parseLongLong(offsetText, value) ||
            value < std::numeric_limits<int>::min() ||
            value > std::numeric_limits<int>::max()) {
            return fail("alarm_offset must be an integer");
        }
        event.alarmOffset = static_cast<int>(value);
    }

    const auto hiddenText = trimView(cell(row, columns, "hidden"));
    if (!hiddenText.empty() && !parseBool(hiddenText, event.hidden)) {
        return fail("hidden must be true/false");
    }

    event.notes = std::string(cell(row, columns, "notes"));

    const auto createdText = trimView(cell(row, columns, "created_utc"));
    if (!createdText.empty()) {
        long long value = 0;
        if (!parseLongLong(createdText, value) || value < 0) {
            return fail("created_utc must be a non-negative integer");
        }
        event.createdUtc = value;
    }
    const auto updatedText = trimView(cell(row, columns, "updated_utc"));
    if (!updatedText.empty()) {
        long long value = 0;
        if (!parseLongLong(updatedText, value) || value < 0) {
            return fail("updated_utc must be a non-negative integer");
        }
        event.updatedUtc = value;
    }
    return event;
}

arc::Result<std::vector<Event>> parseCsv(std::string_view text) {
    auto rows = parseCsvRows(text);
    if (!rows) {
        return rows.error();
    }

    // Find the first non-blank row as the header.
    size_t headerIndex = 0;
    while (headerIndex < rows.value().size()) {
        bool blank = true;
        for (const std::string &field : rows.value()[headerIndex].fields) {
            if (!allWhitespace(field)) {
                blank = false;
                break;
            }
        }
        if (!blank) {
            break;
        }
        ++headerIndex;
    }
    if (headerIndex >= rows.value().size()) {
        return inputError("input is empty");
    }

    const CsvRow &headerRow = rows.value()[headerIndex];
    ColumnMap columns;
    std::vector<std::string> header;
    header.reserve(headerRow.fields.size());
    for (const std::string &raw : headerRow.fields) {
        const std::string name(trimView(raw));
        if (name.empty()) {
            return inputError("line " + std::to_string(headerRow.line) +
                              ": empty column name");
        }
        if (!knownCsvColumn(name)) {
            return inputError("line " + std::to_string(headerRow.line) +
                              ": unknown column: " + name);
        }
        if (columns.index.count(name) != 0) {
            return inputError("line " + std::to_string(headerRow.line) +
                              ": duplicate column: " + name);
        }
        columns.index.emplace(name, header.size());
        header.push_back(name);
    }
    for (const char *required : {"title", "start", "end"}) {
        if (columns.index.count(required) == 0) {
            return inputError("header is missing required column: " + std::string(required));
        }
    }

    std::vector<Event> events;
    for (size_t i = headerIndex + 1; i < rows.value().size(); ++i) {
        const CsvRow &row = rows.value()[i];
        // Only a truly empty record is a blank line. A title of spaces is data
        // and must reach validation, where it is rejected.
        bool blank = true;
        for (const std::string &field : row.fields) {
            if (!field.empty()) {
                blank = false;
                break;
            }
        }
        if (blank) {
            continue;
        }
        auto event = eventFromCsv(row, columns, header);
        if (!event) {
            return event.error();
        }
        events.push_back(event.take());
    }
    if (events.empty()) {
        return inputError("no events found");
    }
    return events;
}

// ---------------------------------------------------------------------------
// JSONL
// ---------------------------------------------------------------------------

struct JsonScalar {
    enum class Type { Null, Bool, Number, String };
    Type type = Type::Null;
    bool boolean = false;
    std::string text; // string value, or the exact number token
};

void appendUtf8(std::string &out, uint32_t code) {
    if (code <= 0x7F) {
        out.push_back(static_cast<char>(code));
    } else if (code <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

class JsonLineParser {
public:
    JsonLineParser(std::string_view text, std::string &error) : text_(text), error_(error) {}

    bool parse(std::vector<std::pair<std::string, JsonScalar>> &out) {
        skipWhitespace();
        if (!parseObject(out)) {
            return false;
        }
        skipWhitespace();
        if (pos_ != text_.size()) {
            return fail("trailing characters");
        }
        return true;
    }

private:
    bool fail(const std::string &message) {
        if (error_.empty()) {
            error_ = message + " at column " + std::to_string(pos_ + 1);
        }
        return false;
    }

    void skipWhitespace() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool parseObject(std::vector<std::pair<std::string, JsonScalar>> &out) {
        if (pos_ >= text_.size() || text_[pos_] != '{') {
            return fail("expected '{'");
        }
        ++pos_;
        skipWhitespace();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }
        for (;;) {
            skipWhitespace();
            std::string key;
            if (!parseString(key)) {
                return false;
            }
            for (const auto &member : out) {
                if (member.first == key) {
                    return fail("duplicate key: " + key);
                }
            }
            skipWhitespace();
            if (pos_ >= text_.size() || text_[pos_] != ':') {
                return fail("expected ':'");
            }
            ++pos_;
            skipWhitespace();
            JsonScalar value;
            if (!parseValue(value)) {
                return false;
            }
            out.emplace_back(std::move(key), std::move(value));
            skipWhitespace();
            if (pos_ >= text_.size()) {
                return fail("unterminated object");
            }
            const char c = text_[pos_++];
            if (c == ',') {
                continue;
            }
            if (c == '}') {
                return true;
            }
            return fail("expected ',' or '}'");
        }
    }

    bool parseValue(JsonScalar &out) {
        if (pos_ >= text_.size()) {
            return fail("unexpected end of input");
        }
        const char c = text_[pos_];
        if (c == '"') {
            out.type = JsonScalar::Type::String;
            return parseString(out.text);
        }
        if (c == 't') {
            return parseLiteral("true", true, out);
        }
        if (c == 'f') {
            return parseLiteral("false", false, out);
        }
        if (c == 'n') {
            if (text_.substr(pos_, 4) != "null") {
                return fail("invalid literal");
            }
            pos_ += 4;
            out.type = JsonScalar::Type::Null;
            return true;
        }
        if (c == '{' || c == '[') {
            return fail("nested JSON values are not supported");
        }
        return parseNumber(out);
    }

    bool parseLiteral(std::string_view literal, bool value, JsonScalar &out) {
        if (text_.substr(pos_, literal.size()) != literal) {
            return fail("invalid literal");
        }
        pos_ += literal.size();
        out.type = JsonScalar::Type::Bool;
        out.boolean = value;
        return true;
    }

    bool parseNumber(JsonScalar &out) {
        const size_t start = pos_;
        if (pos_ < text_.size() && text_[pos_] == '-') {
            ++pos_;
        }
        bool digits = false;
        while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
            ++pos_;
            digits = true;
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                ++pos_;
                digits = true;
            }
        }
        if (digits && pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) {
                ++pos_;
            }
            bool exponentDigits = false;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                ++pos_;
                exponentDigits = true;
            }
            if (!exponentDigits) {
                return fail("invalid exponent");
            }
        }
        if (!digits) {
            return fail("invalid value");
        }
        out.type = JsonScalar::Type::Number;
        out.text = std::string(text_.substr(start, pos_ - start));
        return true;
    }

    bool parseString(std::string &out) {
        if (pos_ >= text_.size() || text_[pos_] != '"') {
            return fail("expected string");
        }
        ++pos_;
        out.clear();
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') {
                return true;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos_ >= text_.size()) {
                return fail("unterminated escape");
            }
            const char escape = text_[pos_++];
            switch (escape) {
            case '"':
                out.push_back('"');
                break;
            case '\\':
                out.push_back('\\');
                break;
            case '/':
                out.push_back('/');
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u': {
                uint32_t code = 0;
                if (!parseHex4(code)) {
                    return false;
                }
                if (code >= 0xD800 && code <= 0xDBFF && text_.substr(pos_, 2) == "\\u") {
                    const size_t save = pos_;
                    pos_ += 2;
                    uint32_t low = 0;
                    if (parseHex4(low) && low >= 0xDC00 && low <= 0xDFFF) {
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    } else {
                        pos_ = save;
                    }
                }
                appendUtf8(out, code);
                break;
            }
            default:
                return fail("invalid escape");
            }
        }
        return fail("unterminated string");
    }

    bool parseHex4(uint32_t &out) {
        if (pos_ + 4 > text_.size()) {
            return fail("truncated \\u escape");
        }
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') {
                value |= static_cast<uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<uint32_t>(c - 'A' + 10);
            } else {
                return fail("invalid hex digit");
            }
        }
        out = value;
        return true;
    }

    std::string_view text_;
    std::string &error_;
    size_t pos_ = 0;
};

const JsonScalar *findJson(const std::vector<std::pair<std::string, JsonScalar>> &object,
                           std::string_view key) {
    for (const auto &member : object) {
        if (member.first == key) {
            return &member.second;
        }
    }
    return nullptr;
}

arc::Result<Event> eventFromJson(const std::vector<std::pair<std::string, JsonScalar>> &object,
                                 int line) {
    const auto fail = [&](const std::string &message) {
        return inputError("line " + std::to_string(line) + ": " + message);
    };

    Event event;
    const JsonScalar *title = findJson(object, "title");
    if (title == nullptr || title->type != JsonScalar::Type::String) {
        return fail("title must be a string");
    }
    if (allWhitespace(title->text)) {
        return fail("title must not be empty");
    }
    event.title = title->text;

    const JsonScalar *start = findJson(object, "start");
    if (start == nullptr || start->type != JsonScalar::Type::String) {
        return fail("start must be a string");
    }
    const auto startUtc = parseIso8601(start->text);
    if (!startUtc) {
        return fail("start is not an ISO-8601 UTC instant");
    }
    const JsonScalar *end = findJson(object, "end");
    if (end == nullptr || end->type != JsonScalar::Type::String) {
        return fail("end must be a string");
    }
    const auto endUtc = parseIso8601(end->text);
    if (!endUtc) {
        return fail("end is not an ISO-8601 UTC instant");
    }
    event.startUtc = *startUtc;
    event.endUtc = *endUtc;
    if (event.endUtc < event.startUtc) {
        return fail("end is before start");
    }

    const auto integer = [&](std::string_view key, long long minimum, long long maximum,
                             bool required, long long fallback,
                             arc::Result<long long> &out) -> bool {
        const JsonScalar *value = findJson(object, key);
        if (value == nullptr) {
            if (required) {
                out = fail(std::string(key) + " is required");
                return false;
            }
            out = fallback;
            return true;
        }
        if (value->type != JsonScalar::Type::Number) {
            out = fail(std::string(key) + " must be an integer");
            return false;
        }
        if (value->text.find_first_of(".eE") != std::string::npos) {
            out = fail(std::string(key) + " must be an integer");
            return false;
        }
        long long parsed = 0;
        if (!parseLongLong(value->text, parsed) || parsed < minimum || parsed > maximum) {
            out = fail(std::string(key) + " is out of range");
            return false;
        }
        out = parsed;
        return true;
    };

    auto id = arc::Result<long long>(0);
    if (!integer("id", 0, std::numeric_limits<long long>::max(), false, 0, id)) {
        return id.error();
    }
    event.id = id.value();

    const JsonScalar *type = findJson(object, "type");
    if (type != nullptr) {
        if (type->type == JsonScalar::Type::Null) {
            event.type = EventType::Event;
        } else if (type->type != JsonScalar::Type::String ||
                   !eventTypeFromName(type->text, event.type)) {
            return fail("type must be event|sleep|wake|alarm");
        }
    }

    auto importance = arc::Result<long long>(50);
    if (!integer("importance", 0, 100, false, 50, importance)) {
        return importance.error();
    }
    event.importance = static_cast<int>(importance.value());

    auto urgency = arc::Result<long long>(50);
    if (!integer("urgency", 0, 100, false, 50, urgency)) {
        return urgency.error();
    }
    event.urgency = static_cast<int>(urgency.value());

    auto alarmOffset = arc::Result<long long>(0);
    if (!integer("alarm_offset", std::numeric_limits<int>::min(),
                 std::numeric_limits<int>::max(), false, 0, alarmOffset)) {
        return alarmOffset.error();
    }
    event.alarmOffset = static_cast<int>(alarmOffset.value());

    const JsonScalar *hidden = findJson(object, "hidden");
    if (hidden != nullptr) {
        if (hidden->type != JsonScalar::Type::Bool) {
            return fail("hidden must be true/false");
        }
        event.hidden = hidden->boolean;
    }

    const JsonScalar *notes = findJson(object, "notes");
    if (notes != nullptr) {
        if (notes->type == JsonScalar::Type::String) {
            event.notes = notes->text;
        } else if (notes->type != JsonScalar::Type::Null) {
            return fail("notes must be a string or null");
        }
    }

    auto created = arc::Result<long long>(0);
    if (!integer("created_utc", 0, std::numeric_limits<long long>::max(), false, 0, created)) {
        return created.error();
    }
    event.createdUtc = created.value();

    auto updated = arc::Result<long long>(0);
    if (!integer("updated_utc", 0, std::numeric_limits<long long>::max(), false, 0, updated)) {
        return updated.error();
    }
    event.updatedUtc = updated.value();
    return event;
}

arc::Result<std::vector<Event>> parseJsonl(std::string_view text) {
    std::vector<Event> events;
    size_t start = 0;
    int lineNumber = 1;
    bool sawLine = false;

    while (start <= text.size()) {
        const size_t end = text.find('\n', start);
        std::string_view line = end == std::string_view::npos
                                    ? text.substr(start)
                                    : text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (!allWhitespace(line)) {
            sawLine = true;
            std::string error;
            JsonLineParser parser(line, error);
            std::vector<std::pair<std::string, JsonScalar>> object;
            if (!parser.parse(object)) {
                return inputError("line " + std::to_string(lineNumber) + ": " + error);
            }
            auto event = eventFromJson(object, lineNumber);
            if (!event) {
                return event.error();
            }
            events.push_back(event.take());
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
        ++lineNumber;
    }

    if (!sawLine) {
        return inputError("input is empty");
    }
    if (events.empty()) {
        return inputError("no events found");
    }
    return events;
}

std::string_view withoutBom(std::string_view text) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.remove_prefix(3);
    }
    return text;
}

} // namespace

const char *importFormatName(ImportFormat format) {
    switch (format) {
    case ImportFormat::Csv:
        return "csv";
    case ImportFormat::Jsonl:
        return "jsonl";
    }
    return "csv";
}

bool importFormatFromName(std::string_view name, ImportFormat &out) {
    if (name == "csv") {
        out = ImportFormat::Csv;
        return true;
    }
    if (name == "jsonl" || name == "json") {
        out = ImportFormat::Jsonl;
        return true;
    }
    return false;
}

arc::Result<std::vector<Event>> parseImportedEvents(std::string_view text,
                                                    ImportFormat format) {
    text = withoutBom(text);
    if (allWhitespace(text)) {
        return inputError("input is empty");
    }
    if (format == ImportFormat::Csv) {
        return parseCsv(text);
    }
    return parseJsonl(text);
}

} // namespace klend_ario
