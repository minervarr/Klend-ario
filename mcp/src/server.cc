#include "server.hh"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "klend_ario/event.hh"
#include "klend_ario/query.hh"
#include "klend_ario/timeexpr.hh"

namespace klend_ario::mcp {
namespace {

// The revision this server implements. The spec's dates are revision names,
// not feature flags: returning the one we actually speak is the honest answer
// when a client proposes a different one. Adopting 2026-07-28 (stateless
// requests, server/discover) is a separate decision, not a default.
constexpr const char *kProtocolVersion = "2025-06-18";

constexpr long long kWeekSeconds = 7 * 86400;
constexpr long long kDefaultLimit = 100;
constexpr long long kMaxLimit = 500;

// The model reads this once per session. Keep it short, concrete, and about
// the things a fresh assistant gets wrong: UTC, half-open ranges, hidden rows,
// and the difference between changing a field and changing a duration.
constexpr const char *kInstructions =
    "Klend-ario is the user's personal calendar. All stored times are UTC; "
    "convert to the user's local zone only for display, and keep UTC clear.\n"
    "- list_events defaults to 00:00 UTC today through +7 days, includes hidden "
    "rows, and returns at most 100 rows. Its range is half-open: `to` is "
    "exclusive. A week starts Sunday.\n"
    "- `hidden` is a weekly-view flag, not deletion. Sleep/wake blocks may be "
    "hidden, but their alarms still fire. Use visibility=visible for a clean "
    "agenda and visibility=hidden to find those rows.\n"
    "- `alarm_offset` is seconds before start; T=0 always fires. importance "
    "0..100 is hue; urgency 0..100 is border thickness.\n"
    "- create_event needs a title and either end or duration. update_event "
    "changes only supplied fields; a start-only update keeps the existing "
    "duration.\n"
    "- Ids are stable integers. Call get_event before updating or deleting. "
    "delete_event is idempotent and does not report whether the id existed.\n"
    "- Times accept ISO-8601 UTC, date-only, or relative forms: now, today, "
    "tomorrow, yesterday, week, next-week, last-week, sunday..saturday, +90m, "
    "today+2h. Durations: 90m, 2h, 1h30m, 1d, 1w, or bare seconds.\n"
    "Treat this as the user's real calendar: be conservative with destructive "
    "changes and ask before bulk edits.";

Json result(const Json &id, Json value) {
    Json response = Json::object();
    response.set("jsonrpc", Json::string("2.0"));
    response.set("id", id);
    response.set("result", std::move(value));
    return response;
}

Json rpcError(const Json &id, int code, const std::string &message) {
    Json error = Json::object();
    error.set("code", Json::integer(code));
    error.set("message", Json::string(message));
    Json response = Json::object();
    response.set("jsonrpc", Json::string("2.0"));
    response.set("id", id);
    response.set("error", std::move(error));
    return response;
}

Json textToolResult(const std::string &text, bool isError = false) {
    Json part = Json::object();
    part.set("type", Json::string("text"));
    part.set("text", Json::string(text));
    Json content = Json::array();
    content.push(std::move(part));

    Json out = Json::object();
    out.set("content", std::move(content));
    if (isError) {
        out.set("isError", Json::boolean(true));
    }
    return out;
}

// A successful data tool returns the same compact JSON in `text` (which is what
// OpenCode feeds to the model) and in `structuredContent` (which hosts and a
// future UI can render without re-parsing prose). MCP's 2025-era
// `structuredContent` is an OBJECT, so a list result wraps its array as
// `{"events": [...]}` while `text` stays the bare array the CLI also emits.
// Output schemas are deferred: they would be paid for in every tools/list
// prompt before a host validates them (essentialAgent/MEMORY.md §8).
Json dataToolResult(const Json &textValue, Json structured) {
    const std::string text = textValue.dump();
    Json part = Json::object();
    part.set("type", Json::string("text"));
    part.set("text", Json::string(text));
    Json content = Json::array();
    content.push(std::move(part));

    Json out = Json::object();
    out.set("content", std::move(content));
    out.set("structuredContent", std::move(structured));
    return out;
}

Json dataToolResult(const Json &value) { return dataToolResult(value, value); }

Json eventToJson(const klend_ario::Event &e) {
    Json j = Json::object();
    j.set("id", Json::integer(e.id));
    j.set("title", Json::string(e.title));
    j.set("start", Json::string(klend_ario::formatIso8601(e.startUtc)));
    j.set("end", Json::string(klend_ario::formatIso8601(e.endUtc)));
    j.set("type", Json::string(klend_ario::eventTypeName(e.type)));
    j.set("importance", Json::integer(e.importance));
    j.set("urgency", Json::integer(e.urgency));
    j.set("alarm_offset", Json::integer(e.alarmOffset));
    j.set("hidden", Json::boolean(e.hidden));
    j.set("notes", Json::string(e.notes));
    return j;
}

Json property(const char *type, const char *description) {
    Json p = Json::object();
    p.set("type", Json::string(type));
    p.set("description", Json::string(description));
    return p;
}

Json enumProperty(const char *description, std::initializer_list<const char *> values) {
    Json p = property("string", description);
    Json choices = Json::array();
    for (const char *value : values) {
        choices.push(Json::string(value));
    }
    p.set("enum", std::move(choices));
    return p;
}

Json integerProperty(const char *description, long long minimum, long long maximum) {
    Json p = property("integer", description);
    p.set("minimum", Json::integer(minimum));
    p.set("maximum", Json::integer(maximum));
    return p;
}

Json annotations(bool readOnly, bool destructive, bool idempotent) {
    Json a = Json::object();
    a.set("readOnlyHint", Json::boolean(readOnly));
    a.set("destructiveHint", Json::boolean(destructive));
    a.set("idempotentHint", Json::boolean(idempotent));
    a.set("openWorldHint", Json::boolean(false));
    return a;
}

Json toolSchema(const char *name, const char *title, const char *description, Json properties,
                std::vector<std::string> required, Json annotationSet) {
    Json schema = Json::object();
    schema.set("type", Json::string("object"));
    schema.set("properties", std::move(properties));
    schema.set("additionalProperties", Json::boolean(false));
    if (!required.empty()) {
        Json req = Json::array();
        for (const std::string &key : required) {
            req.push(Json::string(key));
        }
        schema.set("required", std::move(req));
    }
    Json tool = Json::object();
    tool.set("name", Json::string(name));
    tool.set("title", Json::string(title));
    tool.set("description", Json::string(description));
    tool.set("inputSchema", std::move(schema));
    tool.set("annotations", std::move(annotationSet));
    return tool;
}

std::string trimCopy(const std::string &text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool addChecked(long long a, long long b, long long &out) {
    if ((b > 0 && a > std::numeric_limits<long long>::max() - b) ||
        (b < 0 && a < std::numeric_limits<long long>::min() - b)) {
        return false;
    }
    out = a + b;
    return true;
}

bool rejectUnknown(const Json &arguments, std::initializer_list<const char *> allowed,
                   std::string &error) {
    std::string allowedList;
    for (const char *key : allowed) {
        if (!allowedList.empty()) {
            allowedList += ", ";
        }
        allowedList += key;
    }
    for (const auto &member : arguments.members()) {
        bool known = false;
        for (const char *key : allowed) {
            if (member.first == key) {
                known = true;
                break;
            }
        }
        if (!known) {
            error = "unknown argument \"" + member.first + "\" (allowed: " + allowedList + ")";
            return false;
        }
    }
    return true;
}

bool requireString(const Json &arguments, const char *key, std::string &target,
                   std::string &error) {
    const Json *value = arguments.find(key);
    if (value == nullptr) {
        return true;
    }
    if (!value->isString()) {
        error = std::string(key) + " must be a string";
        return false;
    }
    target = value->asString();
    return true;
}

bool requireInstant(const Json &arguments, const char *key, long long now, long long &target,
                    std::string &error) {
    const Json *value = arguments.find(key);
    if (value == nullptr) {
        return true;
    }
    if (!value->isString()) {
        error = std::string(key) + " must be a UTC instant string";
        return false;
    }
    const auto parsed = klend_ario::parseInstantExpr(value->asString(), now);
    if (!parsed) {
        error = std::string(key) +
                " is not a UTC instant (use ISO-8601, today, tomorrow, week, or +2h)";
        return false;
    }
    target = *parsed;
    return true;
}

bool requireDuration(const Json &arguments, const char *key, long long &target,
                     std::string &error) {
    const Json *value = arguments.find(key);
    if (value == nullptr) {
        return true;
    }
    if (!value->isString()) {
        error = std::string(key) + " must be a duration string";
        return false;
    }
    const auto parsed = klend_ario::parseDurationExpr(value->asString());
    if (!parsed) {
        error = std::string(key) + " is not a duration (use 90m, 2h, 1h30m, or 1d)";
        return false;
    }
    target = *parsed;
    return true;
}

bool requireInteger(const Json &arguments, const char *key, long long minimum, long long maximum,
                    long long &target, std::string &error) {
    const Json *value = arguments.find(key);
    if (value == nullptr) {
        return true;
    }
    if (!value->isNumber()) {
        error = std::string(key) + " must be an integer";
        return false;
    }
    const double number = value->asNumber();
    if (!std::isfinite(number) || number != std::floor(number)) {
        error = std::string(key) + " must be an integer";
        return false;
    }
    if (number < static_cast<double>(minimum) || number > static_cast<double>(maximum)) {
        error = std::string(key) + " must be between " + std::to_string(minimum) + " and " +
                std::to_string(maximum);
        return false;
    }
    target = static_cast<long long>(number);
    return true;
}

bool readId(const Json &arguments, long long &id, std::string &error) {
    if (arguments.find("id") == nullptr) {
        error = "id is required and must be a positive integer";
        return false;
    }
    return requireInteger(arguments, "id", 1, 9000000000000000LL, id, error);
}

// Applies the fields that mean the same thing on create and update. `changed`
// lets update_event tell "only the id was supplied" from a real edit.
bool applyScalarFields(const Json &arguments, klend_ario::Event &event, std::string &error,
                       bool &changed) {
    if (arguments.find("type") != nullptr) {
        const Json *value = arguments.find("type");
        if (!value->isString() ||
            !klend_ario::eventTypeFromName(value->asString(), event.type)) {
            error = "type must be event|sleep|wake|alarm";
            return false;
        }
        changed = true;
    }
    if (arguments.find("importance") != nullptr) {
        long long value = 0;
        if (!requireInteger(arguments, "importance", 0, 100, value, error)) {
            return false;
        }
        event.importance = static_cast<int>(value);
        changed = true;
    }
    if (arguments.find("urgency") != nullptr) {
        long long value = 0;
        if (!requireInteger(arguments, "urgency", 0, 100, value, error)) {
            return false;
        }
        event.urgency = static_cast<int>(value);
        changed = true;
    }
    if (arguments.find("alarm_offset") != nullptr) {
        long long value = 0;
        if (!requireInteger(arguments, "alarm_offset", std::numeric_limits<int>::min(),
                            std::numeric_limits<int>::max(), value, error)) {
            return false;
        }
        event.alarmOffset = static_cast<int>(value);
        changed = true;
    }
    if (arguments.find("hidden") != nullptr) {
        const Json *value = arguments.find("hidden");
        if (value->type() != Json::Type::Bool) {
            error = "hidden must be true or false";
            return false;
        }
        event.hidden = value->asBool();
        changed = true;
    }
    if (arguments.find("notes") != nullptr) {
        if (!requireString(arguments, "notes", event.notes, error)) {
            return false;
        }
        changed = true;
    }
    return true;
}

const char *tierName(Tier tier) {
    switch (tier) {
    case Tier::Read:
        return "read";
    case Tier::Write:
        return "write";
    case Tier::Destructive:
        return "destructive";
    }
    return "unknown";
}

bool isKnownTool(const std::string &name) {
    return name == "list_events" || name == "get_event" || name == "create_event" ||
           name == "update_event" || name == "delete_event";
}

} // namespace

bool Permissions::allows(Tier tier) const {
    switch (tier) {
    case Tier::Read:
        return read;
    case Tier::Write:
        return write;
    case Tier::Destructive:
        return destructive;
    }
    return false;
}

Server::Server(klend_ario::Db db, Permissions permissions)
    : db_(std::move(db)), permissions_(permissions) {}

std::string Server::handle(const std::string &message) {
    Json msg;
    std::string error;
    if (!parseJson(message, msg, error)) {
        return rpcError(Json(), -32700, "parse error: " + error).dump();
    }
    if (!msg.isObject()) {
        return rpcError(Json(), -32600, "invalid request").dump();
    }

    const Json *method = msg.find("method");
    if (method == nullptr || !method->isString()) {
        return rpcError(Json(), -32600, "invalid request: no method").dump();
    }
    const Json *id = msg.find("id");
    const bool isRequest = id != nullptr;
    const std::string name = method->asString();
    const Json *params = msg.find("params");

    // Notifications never get a reply.
    if (name.rfind("notifications/", 0) == 0) {
        return "";
    }
    if (!isRequest) {
        return "";
    }

    const Json requestId = *id;

    if (name == "initialize") {
        Json toolsCapability = Json::object();
        toolsCapability.set("listChanged", Json::boolean(false));
        Json capabilities = Json::object();
        capabilities.set("tools", std::move(toolsCapability));
        Json info = Json::object();
        info.set("name", Json::string("klend_ario"));
        info.set("version", Json::string("0.1.0"));
        Json resultValue = Json::object();
        resultValue.set("protocolVersion", Json::string(kProtocolVersion));
        resultValue.set("capabilities", std::move(capabilities));
        resultValue.set("serverInfo", std::move(info));
        resultValue.set("instructions", Json::string(kInstructions));
        return result(requestId, std::move(resultValue)).dump();
    }

    if (name == "tools/list") {
        return result(requestId, toolsList()).dump();
    }

    if (name == "ping") {
        return result(requestId, Json::object()).dump();
    }

    if (name == "tools/call") {
        if (params == nullptr || !params->isObject()) {
            return rpcError(requestId, -32602, "invalid params: tools/call needs an object")
                .dump();
        }
        const Json *toolName = params->find("name");
        if (toolName == nullptr || !toolName->isString()) {
            return rpcError(requestId, -32602, "invalid params: tools/call needs a name").dump();
        }
        const std::string tool = toolName->asString();
        if (!isKnownTool(tool)) {
            // An unknown tool is a protocol error, not a tool result: the model
            // asked for something this server does not have.
            return rpcError(requestId, -32602, "unknown tool: " + tool).dump();
        }
        const Json *arguments = params->find("arguments");
        if (arguments != nullptr && !arguments->isObject()) {
            return rpcError(requestId, -32602, "invalid params: arguments must be an object")
                .dump();
        }
        return result(requestId, callTool(tool, arguments)).dump();
    }

    return rpcError(requestId, -32601, "method not found: " + name).dump();
}

Json Server::toolsList() const {
    Json tools = Json::array();

    if (permissions_.allows(Tier::Read)) {
        Json properties = Json::object();
        properties.set("from", property(
            "string", "ISO-8601 UTC or relative lower bound (default: 00:00 UTC today)"));
        properties.set("to", property(
            "string", "exclusive ISO-8601 UTC or relative upper bound (default: from + 7 days)"));
        properties.set("search", property("string", "literal case-insensitive title/notes search"));
        properties.set("type", enumProperty("event type", {"event", "sleep", "wake", "alarm"}));
        properties.set("visibility",
                       enumProperty("hidden-row filter (default any)", {"any", "visible", "hidden"}));
        properties.set("sort", enumProperty("result order (default start)",
                                           {"start", "-start", "importance", "urgency", "updated",
                                            "title"}));
        properties.set("limit", integerProperty("maximum rows, 0..500 (default 100)", 0, kMaxLimit));
        properties.set("offset", integerProperty("rows to skip (default 0)", 0, INT_MAX));
        tools.push(toolSchema(
            "list_events", "List events",
            "List events overlapping a half-open UTC range [from, to). Defaults: today 00:00 "
            "UTC through +7 days, visibility any, start order, 100 rows. Times accept ISO-8601 "
            "UTC, date-only, or relative forms (today, tomorrow, week, +2h). `to` is exclusive; "
            "hidden rows are sleep/view flags, not deletions.",
            std::move(properties), {}, annotations(true, false, true)));

        properties = Json::object();
        properties.set("id", integerProperty("event id", 1, 9000000000000000LL));
        tools.push(toolSchema("get_event", "Get event",
                              "Fetch one event by id, including notes and timestamps. Use it "
                              "before updating or deleting.",
                              std::move(properties), {"id"}, annotations(true, false, true)));
    }

    if (permissions_.allows(Tier::Write)) {
        Json properties = Json::object();
        properties.set("title", property("string", "event title (required, non-empty)"));
        properties.set("start", property(
            "string", "ISO-8601 UTC or relative start (default: now)"));
        properties.set("end", property("string", "exclusive ISO-8601 UTC or relative end"));
        properties.set("duration", property("string", "length such as 90m, 2h or 1h30m (end xor "
                                                      "duration)"));
        properties.set("type", enumProperty("event type (default event)",
                                           {"event", "sleep", "wake", "alarm"}));
        properties.set("importance", integerProperty("0..100, hue (default 50)", 0, 100));
        properties.set("urgency", integerProperty("0..100, border (default 50)", 0, 100));
        properties.set("alarm_offset",
                       integerProperty("seconds before start; 0 = no T-offset", INT_MIN, INT_MAX));
        properties.set("hidden", property("boolean", "hide in the weekly view (default false)"));
        properties.set("notes", property("string", "free-form note"));
        tools.push(toolSchema(
            "create_event", "Create event",
            "Create one event. Give `end` or `duration` (not both); start defaults to now. "
            "Returns {id}. Times accept ISO-8601 UTC, date-only, or relative forms; duration "
            "accepts 90m/2h/1h30m.",
            std::move(properties), {"title"}, annotations(false, false, false)));

        properties = Json::object();
        properties.set("id", integerProperty("event id to update", 1, 9000000000000000LL));
        properties.set("title", property("string", "new title (non-empty)"));
        properties.set("start", property(
            "string", "new start; alone it keeps the existing duration"));
        properties.set("end", property("string", "new exclusive end (end xor duration)"));
        properties.set("duration", property("string", "new length (end xor duration)"));
        properties.set("type", enumProperty("event type", {"event", "sleep", "wake", "alarm"}));
        properties.set("importance", integerProperty("0..100", 0, 100));
        properties.set("urgency", integerProperty("0..100", 0, 100));
        properties.set("alarm_offset", integerProperty("seconds before start", INT_MIN, INT_MAX));
        properties.set("hidden", property("boolean", "hide in the weekly view"));
        properties.set("notes", property("string", "new note"));
        tools.push(toolSchema(
            "update_event", "Update event",
            "Change only the fields supplied. A start-only update keeps the existing duration; "
            "duration recomputes the end; end and duration are mutually exclusive. Returns the "
            "updated event.",
            std::move(properties), {"id"}, annotations(false, true, true)));
    }

    if (permissions_.allows(Tier::Destructive)) {
        Json properties = Json::object();
        properties.set("id", integerProperty("event id to delete", 1, 9000000000000000LL));
        tools.push(toolSchema(
            "delete_event", "Delete event",
            "Delete one event by id. Deleting a missing id is a no-op success (idempotent); "
            "confirm the id with get_event first.",
            std::move(properties), {"id"}, annotations(false, true, true)));
    }

    Json resultValue = Json::object();
    resultValue.set("tools", std::move(tools));
    return resultValue;
}

Json Server::callTool(const std::string &name, const Json *arguments) {
    const Json args = arguments != nullptr ? *arguments : Json::object();

    Tier tier = Tier::Read;
    if (name == "create_event" || name == "update_event") {
        tier = Tier::Write;
    } else if (name == "delete_event") {
        tier = Tier::Destructive;
    }
    if (!permissions_.allows(tier)) {
        return textToolResult("permission denied: " + name + " needs the " +
                                  std::string(tierName(tier)) +
                                  " tier, which is disabled for this server",
                              true);
    }

    if (name == "list_events") {
        return listEvents(args);
    }
    if (name == "get_event") {
        return getEvent(args);
    }
    if (name == "create_event") {
        return createEvent(args);
    }
    if (name == "update_event") {
        return updateEvent(args);
    }
    if (name == "delete_event") {
        return deleteEvent(args);
    }
    return textToolResult("unknown tool: " + name, true); // handle() filters this first
}

Json Server::listEvents(const Json &arguments) {
    std::string error;
    if (!rejectUnknown(arguments,
                       {"from", "to", "search", "type", "visibility", "sort", "limit", "offset"},
                       error)) {
        return textToolResult(error, true);
    }

    const long long now = klend_ario::utcNowSeconds();
    long long from = klend_ario::startOfDayUtc(now);
    if (!requireInstant(arguments, "from", now, from, error)) {
        return textToolResult(error, true);
    }

    long long to = 0;
    if (arguments.find("to") != nullptr) {
        if (!requireInstant(arguments, "to", now, to, error)) {
            return textToolResult(error, true);
        }
    } else if (!addChecked(from, kWeekSeconds, to)) {
        return textToolResult("from is out of range", true);
    }
    if (from > to) {
        return textToolResult("from must be at or before to", true);
    }

    klend_ario::EventQuery query;
    query.fromUtc = from;
    query.toUtc = to;

    if (arguments.find("search") != nullptr) {
        std::string search;
        if (!requireString(arguments, "search", search, error)) {
            return textToolResult(error, true);
        }
        search = trimCopy(search);
        if (search.empty()) {
            return textToolResult("search must not be empty", true);
        }
        query.text = search;
    }

    if (arguments.find("type") != nullptr) {
        const Json *value = arguments.find("type");
        klend_ario::EventType type = klend_ario::EventType::Event;
        if (!value->isString() || !klend_ario::eventTypeFromName(value->asString(), type)) {
            return textToolResult("type must be event|sleep|wake|alarm", true);
        }
        query.type = type;
    }

    if (arguments.find("visibility") != nullptr) {
        const Json *value = arguments.find("visibility");
        const std::string text = value->isString() ? value->asString() : "";
        if (text == "any") {
            query.visibility = klend_ario::Visibility::Any;
        } else if (text == "visible") {
            query.visibility = klend_ario::Visibility::Visible;
        } else if (text == "hidden") {
            query.visibility = klend_ario::Visibility::Hidden;
        } else {
            return textToolResult("visibility must be any|visible|hidden", true);
        }
    }

    if (arguments.find("sort") != nullptr) {
        const Json *value = arguments.find("sort");
        klend_ario::EventSort sort = klend_ario::EventSort::Start;
        if (!value->isString() || !klend_ario::eventSortFromName(value->asString(), sort)) {
            return textToolResult(
                "sort must be start|-start|importance|urgency|updated|title", true);
        }
        query.sort = sort;
    }

    long long limit = kDefaultLimit;
    if (!requireInteger(arguments, "limit", 0, kMaxLimit, limit, error)) {
        return textToolResult(error, true);
    }
    query.limit = static_cast<int>(limit);

    long long offset = 0;
    if (!requireInteger(arguments, "offset", 0, INT_MAX, offset, error)) {
        return textToolResult(error, true);
    }
    query.offset = static_cast<int>(offset);

    auto events = db_.queryEvents(query);
    if (!events) {
        return textToolResult(events.error().message, true);
    }
    Json array = Json::array();
    for (const klend_ario::Event &event : events.value()) {
        array.push(eventToJson(event));
    }
    // text is the bare array (same shape as `list --format json`); the
    // 2025-era structuredContent field must be an object, so it wraps the rows.
    Json wrapper = Json::object();
    wrapper.set("events", array);
    return dataToolResult(array, std::move(wrapper));
}

Json Server::getEvent(const Json &arguments) {
    std::string error;
    if (!rejectUnknown(arguments, {"id"}, error)) {
        return textToolResult(error, true);
    }
    long long id = 0;
    if (!readId(arguments, id, error)) {
        return textToolResult(error, true);
    }
    auto event = db_.getEvent(id);
    if (!event) {
        return textToolResult(event.error().message, true);
    }
    return dataToolResult(eventToJson(event.value()));
}

Json Server::createEvent(const Json &arguments) {
    std::string error;
    if (!rejectUnknown(arguments,
                       {"title", "start", "end", "duration", "type", "importance", "urgency",
                        "alarm_offset", "hidden", "notes"},
                       error)) {
        return textToolResult(error, true);
    }

    const Json *title = arguments.find("title");
    if (title == nullptr || !title->isString() || title->asString().empty()) {
        return textToolResult("title is required and must be a non-empty string", true);
    }

    const long long now = klend_ario::utcNowSeconds();
    klend_ario::Event event;
    event.title = title->asString();
    event.startUtc = now;
    if (!requireInstant(arguments, "start", now, event.startUtc, error)) {
        return textToolResult(error, true);
    }

    const bool hasEnd = arguments.find("end") != nullptr;
    const bool hasDuration = arguments.find("duration") != nullptr;
    if (hasEnd && hasDuration) {
        return textToolResult("use end or duration, not both", true);
    }
    if (!hasEnd && !hasDuration) {
        return textToolResult("create_event needs end or duration", true);
    }

    bool changed = false;
    if (hasDuration) {
        long long duration = 0;
        if (!requireDuration(arguments, "duration", duration, error)) {
            return textToolResult(error, true);
        }
        if (!addChecked(event.startUtc, duration, event.endUtc)) {
            return textToolResult("duration is out of range", true);
        }
    } else if (!requireInstant(arguments, "end", now, event.endUtc, error)) {
        return textToolResult(error, true);
    }

    if (event.endUtc < event.startUtc) {
        return textToolResult("end must be at or after start", true);
    }
    if (!applyScalarFields(arguments, event, error, changed)) {
        return textToolResult(error, true);
    }

    auto id = db_.insertEvent(event);
    if (!id) {
        return textToolResult(id.error().message, true);
    }
    Json out = Json::object();
    out.set("id", Json::integer(id.value()));
    return dataToolResult(std::move(out));
}

Json Server::updateEvent(const Json &arguments) {
    std::string error;
    if (!rejectUnknown(arguments,
                       {"id", "title", "start", "end", "duration", "type", "importance", "urgency",
                        "alarm_offset", "hidden", "notes"},
                       error)) {
        return textToolResult(error, true);
    }

    long long id = 0;
    if (!readId(arguments, id, error)) {
        return textToolResult(error, true);
    }
    auto existing = db_.getEvent(id);
    if (!existing) {
        return textToolResult(existing.error().message, true);
    }

    klend_ario::Event event = existing.value();
    bool changed = false;

    if (arguments.find("title") != nullptr) {
        std::string title;
        if (!requireString(arguments, "title", title, error)) {
            return textToolResult(error, true);
        }
        if (title.empty()) {
            return textToolResult("title must not be empty", true);
        }
        event.title = std::move(title);
        changed = true;
    }

    const Json *startValue = arguments.find("start");
    const Json *endValue = arguments.find("end");
    const Json *durationValue = arguments.find("duration");
    if (endValue != nullptr && durationValue != nullptr) {
        return textToolResult("use end or duration, not both", true);
    }
    if (startValue != nullptr || endValue != nullptr || durationValue != nullptr) {
        const long long now = klend_ario::utcNowSeconds();
        long long newStart = event.startUtc;
        if (startValue != nullptr &&
            !requireInstant(arguments, "start", now, newStart, error)) {
            return textToolResult(error, true);
        }

        long long newEnd = event.endUtc;
        if (durationValue != nullptr) {
            long long duration = 0;
            if (!requireDuration(arguments, "duration", duration, error)) {
                return textToolResult(error, true);
            }
            if (!addChecked(newStart, duration, newEnd)) {
                return textToolResult("duration is out of range", true);
            }
        } else if (endValue != nullptr) {
            if (!requireInstant(arguments, "end", now, newEnd, error)) {
                return textToolResult(error, true);
            }
        } else {
            // start-only keeps the existing duration, matching `edit --start`.
            const long long duration = event.endUtc - event.startUtc;
            if (!addChecked(newStart, duration, newEnd)) {
                return textToolResult("start is out of range", true);
            }
        }

        if (newEnd < newStart) {
            return textToolResult("end must be at or after start", true);
        }
        event.startUtc = newStart;
        event.endUtc = newEnd;
        changed = true;
    }

    if (!applyScalarFields(arguments, event, error, changed)) {
        return textToolResult(error, true);
    }
    if (!changed) {
        return textToolResult("update_event needs at least one field to change", true);
    }

    event.id = id;
    auto updated = db_.updateEvent(event);
    if (!updated) {
        return textToolResult(updated.error().message, true);
    }
    return dataToolResult(eventToJson(event));
}

Json Server::deleteEvent(const Json &arguments) {
    std::string error;
    if (!rejectUnknown(arguments, {"id"}, error)) {
        return textToolResult(error, true);
    }
    long long id = 0;
    if (!readId(arguments, id, error)) {
        return textToolResult(error, true);
    }
    auto removed = db_.removeEvent(id);
    if (!removed) {
        return textToolResult(removed.error().message, true);
    }
    Json out = Json::object();
    out.set("deleted", Json::integer(id));
    return dataToolResult(std::move(out));
}

} // namespace klend_ario::mcp
