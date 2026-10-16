#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

// A tiny JSON value, parser and serializer.
//
// MCP speaks JSON-RPC, and the project's dependency rule is std + sqlite3 +
// arc_fs/arc_util (essentialAgent/AGENTS.md §2.7), so JSON is written here
// rather than pulled in. It is deliberately a small subset of JavaScript's
// numbers and nothing more: MCP payloads are small and shaped, not arbitrary.
//
// One class with all members rather than a std::variant: a JSON payload is a
// few hundred bytes, the simplicity is worth more than the memory, and it
// avoids the incomplete-type dance a recursive variant would need.

namespace klend_ario::mcp {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;

    static Json boolean(bool value);
    static Json number(double value);
    static Json integer(long long value);
    static Json string(std::string value);
    static Json array();
    static Json object();

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isObject() const { return type_ == Type::Object; }
    bool isArray() const { return type_ == Type::Array; }
    bool isString() const { return type_ == Type::String; }
    bool isNumber() const { return type_ == Type::Number; }

    bool asBool(bool fallback = false) const;
    double asNumber(double fallback = 0.0) const;
    long long asInteger(long long fallback = 0) const;
    const std::string &asString() const; // empty for a non-string

    // Array access. `items()` is empty for a non-array.
    const std::vector<Json> &items() const { return array_; }
    std::vector<Json> &items() { return array_; }
    void push(Json value);

    // Object access. `find()` returns nullptr when absent; `set()` replaces an
    // existing key in place (order preserved) or appends.
    const Json *find(std::string_view key) const;
    void set(std::string key, Json value);
    const std::vector<std::pair<std::string, Json>> &members() const { return object_; }

    // Compact serialization.
    std::string dump() const;

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Json> array_;
    std::vector<std::pair<std::string, Json>> object_;
};

// Parses `text` into `out`. On failure returns false and sets `error`.
bool parseJson(std::string_view text, Json &out, std::string &error);

} // namespace klend_ario::mcp
