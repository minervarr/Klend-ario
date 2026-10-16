#include "server.hh"

#include <string>

#include "klend_ario/event.hh"

namespace klend_ario::mcp {
namespace {

constexpr long long kWeekSeconds = 7 * 86400;

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

// The `result` of a tools/call. MCP carries the tool's output as text content,
// so the payload is a JSON string — kept small on purpose.
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

Json toolSchema(const char *name, const char *description, Json properties,
                std::vector<std::string> required) {
    Json schema = Json::object();
    schema.set("type", Json::string("object"));
    schema.set("properties", std::move(properties));
    if (!required.empty()) {
        Json req = Json::array();
        for (const std::string &key : required) {
            req.push(Json::string(key));
        }
        schema.set("required", std::move(req));
    }
    Json tool = Json::object();
    tool.set("name", Json::string(name));
    tool.set("description", Json::string(description));
    tool.set("inputSchema", std::move(schema));
    return tool;
}

// Applies the fields PRESENT in `arguments` onto `event`. Absent fields are
// left alone, which is what makes one function serve both create (defaults
// already in a fresh Event) and partial update (an existing row).
bool applyEventFields(const Json &arguments, klend_ario::Event &event, std::string &error) {
    auto requireString = [&](const char *key, std::string &target) -> bool {
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
    };
    auto requireTime = [&](const char *key, long long &target) -> bool {
        const Json *value = arguments.find(key);
        if (value == nullptr) {
            return true;
        }
        if (!value->isString()) {
            error = std::string(key) + " must be an ISO-8601 string";
            return false;
        }
        const auto parsed = klend_ario::parseIso8601(value->asString());
        if (!parsed) {
            error = std::string(key) + " is not ISO-8601 UTC";
            return false;
        }
        target = *parsed;
        return true;
    };
    auto requireInt = [&](const char *key, int &target, int lo, int hi) -> bool {
        const Json *value = arguments.find(key);
        if (value == nullptr) {
            return true;
        }
        if (!value->isNumber()) {
            error = std::string(key) + " must be a number";
            return false;
        }
        const long long parsed = value->asInteger();
        if (parsed < lo || parsed > hi) {
            error = std::string(key) + " must be between " + std::to_string(lo) + " and " +
                    std::to_string(hi);
            return false;
        }
        target = static_cast<int>(parsed);
        return true;
    };

    if (!requireString("title", event.title)) {
        return false;
    }
    if (!requireTime("start", event.startUtc) || !requireTime("end", event.endUtc)) {
        return false;
    }
    if (!requireInt("importance", event.importance, 0, 100) ||
        !requireInt("urgency", event.urgency, 0, 100)) {
        return false;
    }
    if (const Json *value = arguments.find("type")) {
        if (!value->isString() || !klend_ario::eventTypeFromName(value->asString(), event.type)) {
            error = "type must be event|sleep|wake|alarm";
            return false;
        }
    }
    if (const Json *value = arguments.find("alarm_offset")) {
        if (!value->isNumber()) {
            error = "alarm_offset must be a number";
            return false;
        }
        event.alarmOffset = static_cast<int>(value->asInteger());
    }
    if (const Json *value = arguments.find("hidden")) {
        if (!value->isString() && !value->isNumber() && value->type() != Json::Type::Bool) {
            error = "hidden must be a boolean";
            return false;
        }
        event.hidden = value->asBool();
    }
    if (!requireString("notes", event.notes)) {
        return false;
    }
    return true;
}

bool readId(const Json &arguments, long long &id, std::string &error) {
    const Json *value = arguments.find("id");
    if (value == nullptr || !value->isNumber()) {
        error = "id is required and must be a number";
        return false;
    }
    id = value->asInteger();
    return true;
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
        Json capabilities = Json::object();
        capabilities.set("tools", Json::object());
        Json info = Json::object();
        info.set("name", Json::string("klend_ario"));
        info.set("version", Json::string("0.1.0"));
        Json resultValue = Json::object();
        resultValue.set("protocolVersion", Json::string("2025-06-18"));
        resultValue.set("capabilities", std::move(capabilities));
        resultValue.set("serverInfo", std::move(info));
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
            return rpcError(requestId, -32602, "invalid params: tools/call needs an object").dump();
        }
        const Json *toolName = params->find("name");
        if (toolName == nullptr || !toolName->isString()) {
            return rpcError(requestId, -32602, "invalid params: tools/call needs a name").dump();
        }
        const Json *arguments = params->find("arguments");
        return result(requestId, callTool(toolName->asString(), arguments)).dump();
    }

    return rpcError(requestId, -32601, "method not found: " + name).dump();
}

Json Server::toolsList() const {
    Json properties;
    Json tools = Json::array();

    properties = Json::object();
    properties.set("from", property("string", "ISO-8601 UTC lower bound (default: now)"));
    properties.set("to", property("string", "ISO-8601 UTC upper bound (default: from + 7 days)"));
    tools.push(toolSchema("list_events", "List events overlapping a UTC time range.", properties, {}));

    properties = Json::object();
    properties.set("id", property("number", "event id"));
    tools.push(toolSchema("get_event", "Fetch one event by id.", properties, {"id"}));

    properties = Json::object();
    properties.set("title", property("string", "event title"));
    properties.set("start", property("string", "ISO-8601 UTC start"));
    properties.set("end", property("string", "ISO-8601 UTC end"));
    properties.set("type", property("string", "event|sleep|wake|alarm (default event)"));
    properties.set("importance", property("number", "0..100, hue"));
    properties.set("urgency", property("number", "0..100, border"));
    properties.set("alarm_offset", property("number", "seconds before start"));
    properties.set("hidden", property("boolean", "hide in the weekly view"));
    properties.set("notes", property("string", "free-form note"));
    tools.push(toolSchema("create_event", "Create an event and return its id.", properties,
                          {"title", "start", "end"}));

    properties = Json::object();
    properties.set("id", property("number", "event id to update"));
    properties.set("title", property("string", "new title"));
    properties.set("start", property("string", "new ISO-8601 UTC start"));
    properties.set("end", property("string", "new ISO-8601 UTC end"));
    properties.set("type", property("string", "event|sleep|wake|alarm"));
    properties.set("importance", property("number", "0..100"));
    properties.set("urgency", property("number", "0..100"));
    properties.set("alarm_offset", property("number", "seconds before start"));
    properties.set("hidden", property("boolean", "hide in the weekly view"));
    properties.set("notes", property("string", "new note"));
    tools.push(toolSchema("update_event", "Update the fields present for one event.", properties,
                          {"id"}));

    properties = Json::object();
    properties.set("id", property("number", "event id to delete"));
    tools.push(toolSchema("delete_event", "Delete one event by id.", properties, {"id"}));

    Json resultValue = Json::object();
    resultValue.set("tools", std::move(tools));
    return resultValue;
}

Json Server::callTool(const std::string &name, const Json *arguments) {
    const Json args = arguments != nullptr && arguments->isObject() ? *arguments : Json::object();

    Tier tier = Tier::Read;
    if (name == "create_event" || name == "update_event") {
        tier = Tier::Write;
    } else if (name == "delete_event") {
        tier = Tier::Destructive;
    } else if (name != "list_events" && name != "get_event") {
        return textToolResult("unknown tool: " + name, true);
    }
    if (!permissions_.allows(tier)) {
        return textToolResult("permission denied: the " + name + " tool needs a tier that is "
                              "disabled for this server",
                              true);
    }

    if (name == "list_events") {
        long long from = klend_ario::utcNowSeconds();
        if (const Json *value = args.find("from")) {
            const auto parsed = klend_ario::parseIso8601(value->asString());
            if (!parsed) {
                return textToolResult("from is not ISO-8601 UTC", true);
            }
            from = *parsed;
        }
        long long to = from + kWeekSeconds;
        if (const Json *value = args.find("to")) {
            const auto parsed = klend_ario::parseIso8601(value->asString());
            if (!parsed) {
                return textToolResult("to is not ISO-8601 UTC", true);
            }
            to = *parsed;
        }
        auto events = db_.listEvents(from, to);
        if (!events) {
            return textToolResult(events.error().message, true);
        }
        Json array = Json::array();
        for (const klend_ario::Event &e : events.value()) {
            array.push(eventToJson(e));
        }
        return textToolResult(array.dump());
    }

    if (name == "get_event") {
        long long id = 0;
        std::string error;
        if (!readId(args, id, error)) {
            return textToolResult(error, true);
        }
        auto event = db_.getEvent(id);
        if (!event) {
            return textToolResult(event.error().message, true);
        }
        return textToolResult(eventToJson(event.value()).dump());
    }

    if (name == "create_event") {
        if (args.find("title") == nullptr || args.find("start") == nullptr ||
            args.find("end") == nullptr) {
            return textToolResult("create_event needs title, start and end", true);
        }
        klend_ario::Event event;
        std::string error;
        if (!applyEventFields(args, event, error)) {
            return textToolResult(error, true);
        }
        if (event.title.empty()) {
            return textToolResult("title must not be empty", true);
        }
        auto id = db_.insertEvent(event);
        if (!id) {
            return textToolResult(id.error().message, true);
        }
        Json out = Json::object();
        out.set("id", Json::integer(id.value()));
        return textToolResult(out.dump());
    }

    if (name == "update_event") {
        long long id = 0;
        std::string error;
        if (!readId(args, id, error)) {
            return textToolResult(error, true);
        }
        auto existing = db_.getEvent(id);
        if (!existing) {
            return textToolResult(existing.error().message, true);
        }
        klend_ario::Event event = existing.value();
        if (!applyEventFields(args, event, error)) {
            return textToolResult(error, true);
        }
        event.id = id;
        auto updated = db_.updateEvent(event);
        if (!updated) {
            return textToolResult(updated.error().message, true);
        }
        return textToolResult(eventToJson(event).dump());
    }

    // delete_event
    long long id = 0;
    std::string error;
    if (!readId(args, id, error)) {
        return textToolResult(error, true);
    }
    auto removed = db_.removeEvent(id);
    if (!removed) {
        return textToolResult(removed.error().message, true);
    }
    Json out = Json::object();
    out.set("deleted", Json::integer(id));
    return textToolResult(out.dump());
}

} // namespace klend_ario::mcp
