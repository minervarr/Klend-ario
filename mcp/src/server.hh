#pragma once

#include <string>

#include "json.hh"
#include "klend_ario/db.hh"

// A local MCP (Model Context Protocol) server over stdio — the calendar's
// programmatic surface (merged.md §7). It is a FRONTEND: it links core and core
// knows nothing about it (essentialAgent/AGENTS.md §2.2).
//
// The tool surface is deliberately small (five tools) and the transport is one
// JSON-RPC message per line, because MCP's whole cost model is context tokens.
// The surface is also a prompt: `instructions`, tool titles/descriptions and
// annotations are what a model reads before it touches the calendar.

namespace klend_ario::mcp {

// Permission tiers (merged.md §7). READ covers list/get; WRITE covers
// create/update; DESTRUCTIVE covers delete. A tier that is off is not
// advertised in tools/list and is refused in tools/call.
enum class Tier { Read, Write, Destructive };

struct Permissions {
    bool read = true;
    bool write = true;
    bool destructive = true;

    bool allows(Tier tier) const;
};

class Server {
public:
    Server(klend_ario::Db db, Permissions permissions);

    // Handles one JSON-RPC message. Returns the response line to write, or an
    // empty string when the message was a notification (which gets no reply).
    std::string handle(const std::string &message);

private:
    Json callTool(const std::string &name, const Json *arguments);
    Json toolsList() const;

    Json listEvents(const Json &arguments);
    Json getEvent(const Json &arguments);
    Json createEvent(const Json &arguments);
    Json updateEvent(const Json &arguments);
    Json deleteEvent(const Json &arguments);

    klend_ario::Db db_;
    Permissions permissions_;
};

} // namespace klend_ario::mcp
