#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>

#include "json.hh"
#include "server.hh"

#include "klend_ario/db.hh"

using namespace klend_ario::mcp;

namespace {

Json parse(const std::string &text) {
    Json json;
    std::string error;
    assert(parseJson(text, json, error));
    (void)error;
    return json;
}

// The tool's payload: result.content[0].text is itself a JSON string. It is
// what OpenCode feeds to the model, so it must stay parseable.
std::string textOf(const Json &response) {
    const Json *result = response.find("result");
    assert(result != nullptr);
    const Json *content = result->find("content");
    assert(content != nullptr && !content->items().empty());
    return content->items()[0].find("text")->asString();
}

// The same payload for hosts that render it instead of re-parsing text.
Json structuredOf(const Json &response) {
    const Json *result = response.find("result");
    assert(result != nullptr);
    const Json *structured = result->find("structuredContent");
    assert(structured != nullptr);
    return *structured;
}

bool isError(const Json &response) {
    const Json *result = response.find("result");
    assert(result != nullptr);
    const Json *flag = result->find("isError");
    return flag != nullptr && flag->asBool();
}

long long errorCode(const Json &response) {
    const Json *error = response.find("error");
    assert(error != nullptr);
    return error->find("code")->asInteger();
}

std::string callBody(int id, const std::string &tool, const std::string &arguments) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
           ",\"method\":\"tools/call\",\"params\":{\"name\":\"" + tool +
           "\",\"arguments\":" + arguments + "}}";
}

Json call(Server &server, int id, const std::string &tool, const std::string &arguments) {
    return parse(server.handle(callBody(id, tool, arguments)));
}

const Json *toolByName(const Json &toolsList, const std::string &name) {
    const Json *tools = toolsList.find("tools");
    assert(tools != nullptr && tools->isArray());
    for (const Json &tool : tools->items()) {
        const Json *candidate = tool.find("name");
        assert(candidate != nullptr);
        if (candidate->asString() == name) {
            return &tool;
        }
    }
    return nullptr;
}

void testInitialize(Server &server) {
    Json response = parse(server.handle(
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}})"));
    const Json *result = response.find("result");
    assert(result != nullptr);
    assert(result->find("protocolVersion")->asString() == "2025-06-18");
    const Json *instructions = result->find("instructions");
    assert(instructions != nullptr && instructions->isString());
    assert(!instructions->asString().empty());
    const Json *capabilities = result->find("capabilities");
    assert(capabilities != nullptr);
    assert(capabilities->find("tools")->find("listChanged")->asBool() == false);
    assert(result->find("serverInfo")->find("name")->asString() == "klend_ario");

    // A notification gets no reply.
    assert(server.handle(R"({"jsonrpc":"2.0","method":"notifications/initialized"})").empty());

    // We only implement 2025-06-18; that is the version we counter with.
    Json other = parse(server.handle(
        R"({"jsonrpc":"2.0","id":2,"method":"initialize","params":{"protocolVersion":"2026-07-28"}})"));
    assert(other.find("result")->find("protocolVersion")->asString() == "2025-06-18");

    Json pong = parse(server.handle(R"({"jsonrpc":"2.0","id":3,"method":"ping"})"));
    assert(pong.find("result") != nullptr);
}

void testToolMetadata(Server &server) {
    Json response = parse(server.handle(R"({"jsonrpc":"2.0","id":10,"method":"tools/list"})"));
    const Json *result = response.find("result");
    assert(result != nullptr);
    assert(result->find("tools")->items().size() == 5);

    const Json *list = toolByName(*result, "list_events");
    assert(list != nullptr);
    assert(list->find("title")->asString() == "List events");
    assert(list->find("inputSchema")->find("additionalProperties")->asBool() == false);
    const Json *annotations = list->find("annotations");
    assert(annotations != nullptr);
    assert(annotations->find("readOnlyHint")->asBool() == true);
    assert(annotations->find("destructiveHint")->asBool() == false);
    assert(annotations->find("idempotentHint")->asBool() == true);
    assert(annotations->find("openWorldHint")->asBool() == false);
    const Json *visibility = list->find("inputSchema")->find("properties")->find("visibility");
    assert(visibility != nullptr && visibility->find("enum")->items().size() == 3);

    const Json *create = toolByName(*result, "create_event");
    assert(create != nullptr);
    assert(create->find("annotations")->find("idempotentHint")->asBool() == false);
    const Json *required = create->find("inputSchema")->find("required");
    assert(required != nullptr && required->items().size() == 1);
    assert(required->items()[0].asString() == "title");

    const Json *update = toolByName(*result, "update_event");
    assert(update != nullptr);
    assert(update->find("annotations")->find("destructiveHint")->asBool() == true);
    assert(update->find("annotations")->find("idempotentHint")->asBool() == true);

    const Json *remove = toolByName(*result, "delete_event");
    assert(remove != nullptr);
    assert(remove->find("annotations")->find("destructiveHint")->asBool() == true);
}

long long testCreate(Server &server) {
    Json response = call(server, 20, "create_event",
                         R"({"title":"Standup","start":"2030-01-15T10:00:00Z","duration":"30m","importance":80})");
    assert(!isError(response));
    Json created = parse(textOf(response));
    const long long id = created.find("id")->asInteger();
    assert(id > 0);

    // text and structuredContent are the same value: the model reads text, a
    // host can render structuredContent, and neither can drift.
    Json structured = structuredOf(response);
    assert(structured.dump() == textOf(response));
    assert(structured.find("id")->asInteger() == id);

    Json fetched = call(server, 21, "get_event", "{\"id\":" + std::to_string(id) + "}");
    assert(!isError(fetched));
    Json event = parse(textOf(fetched));
    assert(event.find("title")->asString() == "Standup");
    assert(event.find("start")->asString() == "2030-01-15T10:00:00Z");
    assert(event.find("end")->asString() == "2030-01-15T10:30:00Z");
    assert(event.find("importance")->asInteger() == 80);
    assert(structuredOf(fetched).dump() == textOf(fetched));
    return id;
}

void testQuery(Server &server, long long id) {
    Json day = call(server, 30, "list_events",
                    R"({"from":"2030-01-15","to":"2030-01-16"})");
    assert(!isError(day));
    assert(parse(textOf(day)).items().size() == 1);
    Json dayStructured = structuredOf(day);
    assert(dayStructured.find("events") != nullptr);
    assert(dayStructured.find("events")->items().size() == 1);

    Json search = call(server, 31, "list_events",
                       R"({"from":"2030-01-15","to":"2030-01-16","search":"  stand  "})");
    assert(!isError(search));
    assert(parse(textOf(search)).items().size() == 1);

    Json empty = call(server, 32, "list_events",
                      R"({"from":"2030-01-15","to":"2030-01-16","limit":0})");
    assert(!isError(empty));
    assert(parse(textOf(empty)).items().empty());

    // A hidden sleep row is data, not a deletion: `any` sees it, `visible`
    // does not, and `hidden` finds it again.
    Json sleep = call(server, 33, "create_event",
                      R"({"title":"Sleep","start":"2030-01-15T22:00:00Z","duration":"8h","type":"sleep","hidden":true})");
    assert(!isError(sleep));
    const long long sleepId = parse(textOf(sleep)).find("id")->asInteger();

    Json any = call(server, 34, "list_events", R"({"from":"2030-01-15","to":"2030-01-16"})");
    assert(parse(textOf(any)).items().size() == 2);
    Json visible = call(server, 35, "list_events",
                        R"({"from":"2030-01-15","to":"2030-01-16","visibility":"visible"})");
    assert(parse(textOf(visible)).items().size() == 1);
    Json hidden = call(server, 36, "list_events",
                       R"({"from":"2030-01-15","to":"2030-01-16","visibility":"hidden"})");
    assert(parse(textOf(hidden)).items().size() == 1);
    Json type = call(server, 37, "list_events",
                     R"({"from":"2030-01-15","to":"2030-01-16","type":"sleep"})");
    assert(parse(textOf(type)).items().size() == 1);
    Json sorted = call(server, 38, "list_events",
                       R"({"from":"2030-01-15","to":"2030-01-16","sort":"-start"})");
    assert(parse(textOf(sorted)).items()[0].find("id")->asInteger() == sleepId);

    Json deleted = call(server, 39, "delete_event", "{\"id\":" + std::to_string(sleepId) + "}");
    assert(!isError(deleted));
    (void)id;
}

void testUpdate(Server &server, long long id) {
    // start-only keeps the stored duration.
    Json moved = call(server, 40, "update_event",
                      "{\"id\":" + std::to_string(id) +
                          ",\"start\":\"2030-01-15T11:00:00Z\"}");
    assert(!isError(moved));
    Json event = parse(textOf(moved));
    assert(event.find("start")->asString() == "2030-01-15T11:00:00Z");
    assert(event.find("end")->asString() == "2030-01-15T11:30:00Z");

    // duration recomputes the end from the stored start.
    Json resized = call(server, 41, "update_event",
                        "{\"id\":" + std::to_string(id) + ",\"duration\":\"45m\"}");
    assert(!isError(resized));
    event = parse(textOf(resized));
    assert(event.find("end")->asString() == "2030-01-15T11:45:00Z");

    // A partial update leaves the fields it was not given alone.
    Json renamed = call(server, 42, "update_event",
                        "{\"id\":" + std::to_string(id) + ",\"title\":\"Renamed\"}");
    assert(!isError(renamed));
    event = parse(textOf(renamed));
    assert(event.find("title")->asString() == "Renamed");
    assert(event.find("start")->asString() == "2030-01-15T11:00:00Z");
    assert(event.find("end")->asString() == "2030-01-15T11:45:00Z");
}

void testStrictInputs(Server &server, long long id) {
    auto expectError = [&](int idNumber, const std::string &tool, const std::string &arguments,
                           const std::string &needle) {
        Json response = call(server, idNumber, tool, arguments);
        assert(isError(response));
        const std::string text = textOf(response);
        assert(text.find(needle) != std::string::npos);
    };

    expectError(50, "create_event",
                R"({"title":"Backwards","start":"2030-01-15T10:00:00Z","end":"2030-01-15T09:00:00Z"})",
                "end must be at or after start");
    expectError(51, "create_event",
                R"({"title":"Loose","start":"2030-01-15T10:00:00Z","duration":"1h","hidden":"no"})",
                "hidden must be true or false");
    expectError(52, "create_event",
                R"({"title":"Typo","start":"2030-01-15T10:00:00Z","duration":"1h","starts":"x"})",
                "unknown argument");
    expectError(53, "get_event", R"({"id":"1"})", "must be an integer");
    expectError(54, "create_event",
                R"({"title":"Range","start":"2030-01-15T10:00:00Z","duration":"1h","importance":101})",
                "between 0 and 100");
    expectError(55, "list_events", R"({"from":123})", "must be a UTC instant string");
    expectError(56, "list_events", R"({"search":"   "})", "search must not be empty");
    expectError(57, "create_event",
                R"({"title":"Both","start":"2030-01-15T10:00:00Z","end":"2030-01-15T11:00:00Z","duration":"1h"})",
                "end or duration");
    expectError(58, "update_event", "{\"id\":" + std::to_string(id) + "}",
                "at least one field");
}

void testProtocolErrors(Server &server) {
    Json unknown = call(server, 60, "frobnicate", "{}");
    assert(unknown.find("error") != nullptr);
    assert(errorCode(unknown) == -32602);

    Json badArguments =
        parse(server.handle(R"({"jsonrpc":"2.0","id":61,"method":"tools/call","params":{"name":"get_event","arguments":[]}})"));
    assert(badArguments.find("error") != nullptr);
    assert(errorCode(badArguments) == -32602);

    Json unknownMethod = parse(server.handle(R"({"jsonrpc":"2.0","id":62,"method":"bogus"})"));
    assert(errorCode(unknownMethod) == -32601);

    Json broken = parse(server.handle("{not json"));
    assert(errorCode(broken) == -32700);
}

void testReadOnly() {
    auto sealed = klend_ario::Db::openMemory();
    assert(sealed.ok());
    Permissions readOnly;
    readOnly.write = false;
    readOnly.destructive = false;
    Server guarded(sealed.take(), readOnly);

    Json list = parse(guarded.handle(R"({"jsonrpc":"2.0","id":70,"method":"tools/list"})"));
    const Json *tools = list.find("result")->find("tools");
    assert(tools != nullptr && tools->items().size() == 2);
    assert(toolByName(*list.find("result"), "list_events") != nullptr);
    assert(toolByName(*list.find("result"), "get_event") != nullptr);
    assert(toolByName(*list.find("result"), "create_event") == nullptr);
    assert(toolByName(*list.find("result"), "delete_event") == nullptr);

    Json denied = call(guarded, 71, "create_event",
                       R"({"title":"x","start":"2030-01-15T10:00:00Z","duration":"30m"})");
    assert(isError(denied));
    assert(textOf(denied).find("permission denied") != std::string::npos);
}

void testDelete(Server &server, long long id) {
    Json removed = call(server, 80, "delete_event", "{\"id\":" + std::to_string(id) + "}");
    assert(!isError(removed));
    assert(parse(textOf(removed)).find("deleted")->asInteger() == id);

    Json gone = call(server, 81, "get_event", "{\"id\":" + std::to_string(id) + "}");
    assert(isError(gone));

    // Idempotent by core contract: a missing id is a no-op success.
    Json again = call(server, 82, "delete_event", "{\"id\":999999}");
    assert(!isError(again));
    assert(parse(textOf(again)).find("deleted")->asInteger() == 999999);
}

} // namespace

int main() {
    auto db = klend_ario::Db::openMemory();
    assert(db.ok());
    Server server(db.take(), Permissions{});

    testInitialize(server);
    testToolMetadata(server);
    const long long id = testCreate(server);
    testQuery(server, id);
    testUpdate(server, id);
    testStrictInputs(server, id);
    testProtocolErrors(server);
    testReadOnly();
    testDelete(server, id);

    std::printf("server_test: all assertions passed\n");
    return 0;
}
