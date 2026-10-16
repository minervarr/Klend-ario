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

// The tool's payload: result.content[0].text is itself a JSON string.
std::string textOf(const Json &response) {
    const Json *result = response.find("result");
    assert(result != nullptr);
    const Json *content = result->find("content");
    assert(content != nullptr && !content->items().empty());
    return content->items()[0].find("text")->asString();
}

bool isError(const Json &response) {
    const Json *result = response.find("result");
    assert(result != nullptr);
    const Json *flag = result->find("isError");
    return flag != nullptr && flag->asBool();
}

std::string callBody(int id, const std::string &tool, const std::string &arguments) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
           ",\"method\":\"tools/call\",\"params\":{\"name\":\"" + tool +
           "\",\"arguments\":" + arguments + "}}";
}

} // namespace

int main() {
    auto db = klend_ario::Db::openMemory();
    assert(db.ok());
    Server server(db.take(), Permissions{});

    // initialize
    {
        Json r = parse(server.handle(
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})"));
        assert(r.find("result")->find("serverInfo")->find("name")->asString() == "klend_ario");
        assert(r.find("result")->find("protocolVersion")->isString());
    }

    // a notification gets no reply
    assert(server.handle(R"({"jsonrpc":"2.0","method":"notifications/initialized"})").empty());

    // tools/list advertises the five tools
    {
        Json r = parse(server.handle(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})"));
        assert(r.find("result")->find("tools")->items().size() == 5);
    }

    // create_event
    long long id = 0;
    {
        Json r = parse(server.handle(callBody(
            3, "create_event",
            R"({"title":"Standup","start":"2026-10-16T09:00:00Z","end":"2026-10-16T09:30:00Z","importance":80})")));
        assert(!isError(r));
        id = parse(textOf(r)).find("id")->asInteger();
        assert(id > 0);
    }

    // list_events sees it
    {
        Json r = parse(server.handle(callBody(
            4, "list_events",
            R"({"from":"2026-10-16T00:00:00Z","to":"2026-10-17T00:00:00Z"})")));
        assert(!isError(r));
        Json events = parse(textOf(r));
        assert(events.isArray() && events.items().size() == 1);
        assert(events.items()[0].find("title")->asString() == "Standup");
    }

    // update_event
    {
        Json r = parse(server.handle(callBody(
            5, "update_event", "{\"id\":" + std::to_string(id) + ",\"title\":\"Renamed\"}")));
        assert(!isError(r));
    }

    // get_event reflects the rename
    {
        Json r = parse(
            server.handle(callBody(6, "get_event", "{\"id\":" + std::to_string(id) + "}")));
        assert(!isError(r));
        assert(parse(textOf(r)).find("title")->asString() == "Renamed");
    }

    // create_event with a missing field is a tool error, not a protocol error
    {
        Json r = parse(server.handle(callBody(7, "create_event", R"({"title":"NoTime"})")));
        assert(isError(r));
    }

    // delete_event, then get_event fails
    {
        Json r = parse(
            server.handle(callBody(8, "delete_event", "{\"id\":" + std::to_string(id) + "}")));
        assert(!isError(r));
        Json after =
            parse(server.handle(callBody(9, "get_event", "{\"id\":" + std::to_string(id) + "}")));
        assert(isError(after));
    }

    // unknown method -> JSON-RPC error
    {
        Json r = parse(server.handle(R"({"jsonrpc":"2.0","id":10,"method":"bogus"})"));
        assert(r.find("error")->find("code")->asInteger() == -32601);
    }

    // broken JSON -> parse error
    {
        Json r = parse(server.handle("{not json"));
        assert(r.find("error")->find("code")->asInteger() == -32700);
    }

    // the permission tier is enforced: write disabled refuses create_event
    {
        auto sealed = klend_ario::Db::openMemory();
        assert(sealed.ok());
        Permissions readOnly;
        readOnly.write = false;
        Server guarded(sealed.take(), readOnly);
        Json r = parse(guarded.handle(callBody(
            11, "create_event",
            R"({"title":"x","start":"2026-10-16T09:00:00Z","end":"2026-10-16T09:30:00Z"})")));
        assert(isError(r));
    }

    std::printf("server_test: all assertions passed\n");
    return 0;
}
