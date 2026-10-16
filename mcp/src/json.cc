#include "json.hh"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace klend_ario::mcp {
namespace {

// Guards against a hostile deeply-nested payload blowing the C++ stack: past
// this depth the parse fails rather than recursing forever.
constexpr int kMaxDepth = 64;

void appendEscaped(std::string &out, const std::string &value) {
    out += '"';
    for (const unsigned char c : value) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
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
            if (c < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                out += buffer;
            } else {
                out += static_cast<char>(c); // UTF-8 bytes pass through
            }
        }
    }
    out += '"';
}

void appendNumber(std::string &out, double value) {
    // Integers print without a decimal point, which matters: an MCP client
    // reading an event id should see 42, not 42.0.
    if (std::isfinite(value) && value == std::floor(value) && std::fabs(value) < 9.0e15) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
        out += buffer;
    } else {
        char buffer[40];
        std::snprintf(buffer, sizeof(buffer), "%.17g", value);
        out += buffer;
    }
}

// Encodes a Unicode code point as UTF-8, so a \u escape parses to real text.
void appendUtf8(std::string &out, uint32_t code) {
    if (code <= 0x7F) {
        out += static_cast<char>(code);
    } else if (code <= 0x7FF) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code <= 0xFFFF) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

class Parser {
public:
    Parser(std::string_view text, std::string &error) : text_(text), error_(error) {}

    bool parse(Json &out) {
        skipWhitespace();
        if (!parseValue(out, 0)) {
            return false;
        }
        skipWhitespace();
        if (pos_ != text_.size()) {
            return fail("trailing characters after JSON value");
        }
        return true;
    }

private:
    bool fail(const std::string &message) {
        if (error_.empty()) {
            error_ = message + " at offset " + std::to_string(pos_);
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

    bool parseValue(Json &out, int depth) {
        if (depth > kMaxDepth) {
            return fail("nesting too deep");
        }
        if (pos_ >= text_.size()) {
            return fail("unexpected end of input");
        }
        switch (text_[pos_]) {
        case '{':
            return parseObject(out, depth);
        case '[':
            return parseArray(out, depth);
        case '"': {
            std::string value;
            if (!parseString(value)) {
                return false;
            }
            out = Json::string(std::move(value));
            return true;
        }
        case 't':
            return parseLiteral("true", Json::boolean(true), out);
        case 'f':
            return parseLiteral("false", Json::boolean(false), out);
        case 'n':
            return parseLiteral("null", Json(), out);
        default:
            return parseNumber(out);
        }
    }

    bool parseLiteral(std::string_view literal, Json value, Json &out) {
        if (text_.substr(pos_, literal.size()) != literal) {
            return fail("invalid literal");
        }
        pos_ += literal.size();
        out = std::move(value);
        return true;
    }

    bool parseObject(Json &out, int depth) {
        ++pos_; // '{'
        out = Json::object();
        skipWhitespace();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }
        for (;;) {
            skipWhitespace();
            std::string key;
            if (pos_ >= text_.size() || text_[pos_] != '"') {
                return fail("expected object key string");
            }
            if (!parseString(key)) {
                return false;
            }
            skipWhitespace();
            if (pos_ >= text_.size() || text_[pos_] != ':') {
                return fail("expected ':' after object key");
            }
            ++pos_;
            skipWhitespace();
            Json value;
            if (!parseValue(value, depth + 1)) {
                return false;
            }
            out.set(std::move(key), std::move(value));
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
            return fail("expected ',' or '}' in object");
        }
    }

    bool parseArray(Json &out, int depth) {
        ++pos_; // '['
        out = Json::array();
        skipWhitespace();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return true;
        }
        for (;;) {
            skipWhitespace();
            Json value;
            if (!parseValue(value, depth + 1)) {
                return false;
            }
            out.push(std::move(value));
            skipWhitespace();
            if (pos_ >= text_.size()) {
                return fail("unterminated array");
            }
            const char c = text_[pos_++];
            if (c == ',') {
                continue;
            }
            if (c == ']') {
                return true;
            }
            return fail("expected ',' or ']' in array");
        }
    }

    bool parseString(std::string &out) {
        ++pos_; // opening quote
        out.clear();
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') {
                return true;
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= text_.size()) {
                return fail("unterminated escape");
            }
            const char escape = text_[pos_++];
            switch (escape) {
            case '"':
                out += '"';
                break;
            case '\\':
                out += '\\';
                break;
            case '/':
                out += '/';
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u': {
                uint32_t code = 0;
                if (!parseHex4(code)) {
                    return false;
                }
                // A high surrogate is followed by \uXXXX low surrogate.
                if (code >= 0xD800 && code <= 0xDBFF && text_.substr(pos_, 2) == "\\u") {
                    const size_t save = pos_;
                    pos_ += 2;
                    uint32_t low = 0;
                    if (parseHex4(low) && low >= 0xDC00 && low <= 0xDFFF) {
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    } else {
                        pos_ = save; // not a valid pair; keep the high one alone
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

    bool parseNumber(Json &out) {
        const size_t start = pos_;
        if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) {
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
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                ++pos_;
            }
        }
        if (!digits) {
            return fail("invalid number");
        }
        out = Json::number(std::strtod(std::string(text_.substr(start, pos_ - start)).c_str(), nullptr));
        return true;
    }

    std::string_view text_;
    std::string &error_;
    size_t pos_ = 0;
};

} // namespace

Json Json::boolean(bool value) {
    Json json;
    json.type_ = Type::Bool;
    json.bool_ = value;
    return json;
}

Json Json::number(double value) {
    Json json;
    json.type_ = Type::Number;
    json.number_ = value;
    return json;
}

Json Json::integer(long long value) { return number(static_cast<double>(value)); }

Json Json::string(std::string value) {
    Json json;
    json.type_ = Type::String;
    json.string_ = std::move(value);
    return json;
}

Json Json::array() {
    Json json;
    json.type_ = Type::Array;
    return json;
}

Json Json::object() {
    Json json;
    json.type_ = Type::Object;
    return json;
}

bool Json::asBool(bool fallback) const { return type_ == Type::Bool ? bool_ : fallback; }

double Json::asNumber(double fallback) const { return type_ == Type::Number ? number_ : fallback; }

long long Json::asInteger(long long fallback) const {
    return type_ == Type::Number ? static_cast<long long>(number_) : fallback;
}

const std::string &Json::asString() const { return string_; }

void Json::push(Json value) { array_.push_back(std::move(value)); }

const Json *Json::find(std::string_view key) const {
    for (const auto &member : object_) {
        if (member.first == key) {
            return &member.second;
        }
    }
    return nullptr;
}

void Json::set(std::string key, Json value) {
    for (auto &member : object_) {
        if (member.first == key) {
            member.second = std::move(value);
            return;
        }
    }
    object_.emplace_back(std::move(key), std::move(value));
}

std::string Json::dump() const {
    std::string out;
    switch (type_) {
    case Type::Null:
        out += "null";
        break;
    case Type::Bool:
        out += bool_ ? "true" : "false";
        break;
    case Type::Number:
        appendNumber(out, number_);
        break;
    case Type::String:
        appendEscaped(out, string_);
        break;
    case Type::Array:
        out += '[';
        for (size_t i = 0; i < array_.size(); ++i) {
            if (i != 0) {
                out += ',';
            }
            out += array_[i].dump();
        }
        out += ']';
        break;
    case Type::Object:
        out += '{';
        for (size_t i = 0; i < object_.size(); ++i) {
            if (i != 0) {
                out += ',';
            }
            appendEscaped(out, object_[i].first);
            out += ':';
            out += object_[i].second.dump();
        }
        out += '}';
        break;
    }
    return out;
}

bool parseJson(std::string_view text, Json &out, std::string &error) {
    error.clear();
    Parser parser(text, error);
    return parser.parse(out);
}

} // namespace klend_ario::mcp
