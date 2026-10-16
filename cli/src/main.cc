// klend_ario_cli — the MVP frontend.
//
// A THIN shell: parse args, call one core function, print. No business logic
// lives here (essentialAgent/AGENTS.md §4). When a rule needs enforcing, it
// belongs in core/ where the tests can reach it.

#include <cstdio>
#include <string>
#include <vector>

#include "arc/fs/paths.hh"
#include "klend_ario/db.hh"
#include "klend_ario/event.hh"

#include "args.hh"

namespace {

using klend_ario::Db;
using klend_ario::Event;
using klend_ario::EventType;
namespace cli = klend_ario::cli;

constexpr long long kWeekSeconds = 7 * 86400;

// The database lives in the app's STATE directory — machine data, written
// constantly — never home/data and never beside the executable
// (essentialAgent/AGENTS.md §2.3).
std::string defaultDbPath() {
    const arc::fs::Layout layout = arc::fs::layout("klend_ario");
    arc::fs::ensureLayout(layout);
    return arc::fs::join(layout.state, "klend_ario.db");
}

int reportError(const arc::Error &error) {
    std::fprintf(stderr, "error: %s\n", error.message.c_str());
    return 1;
}

int usageError(const std::string &message) {
    std::fprintf(stderr, "error: %s\n\n", message.c_str());
    std::fputs(cli::usage().c_str(), stderr);
    return 2;
}

std::string jsonEscape(const std::string &in) {
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
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                out += buffer;
            } else {
                out += c;
            }
        }
    }
    return out;
}

void printEventText(const Event &e) {
    std::printf("%lld  %s  %s  %s", e.id, klend_ario::formatIso8601(e.startUtc).c_str(),
                klend_ario::formatIso8601(e.endUtc).c_str(), e.title.c_str());
    if (e.type != EventType::Event) {
        std::printf("  [%s]", klend_ario::eventTypeName(e.type));
    }
    if (e.hidden) {
        std::printf("  [hidden]");
    }
    std::printf("\n");
}

void printEventJson(const Event &e) {
    std::printf("{\"id\":%lld,\"title\":\"%s\",\"start\":\"%s\",\"end\":\"%s\","
                "\"type\":\"%s\",\"importance\":%d,\"urgency\":%d,\"alarm_offset\":%d,"
                "\"hidden\":%s,\"notes\":\"%s\"}",
                e.id, jsonEscape(e.title).c_str(), klend_ario::formatIso8601(e.startUtc).c_str(),
                klend_ario::formatIso8601(e.endUtc).c_str(), klend_ario::eventTypeName(e.type),
                e.importance, e.urgency, e.alarmOffset, e.hidden ? "true" : "false",
                jsonEscape(e.notes).c_str());
}

int openDb(const cli::Args &args, Db &out) {
    const std::string path = args.dbPath.empty() ? defaultDbPath() : args.dbPath;
    auto opened = Db::open(path);
    if (!opened) {
        return reportError(opened.error());
    }
    out = opened.take();
    return 0;
}

int commandAdd(const cli::Args &args) {
    if (args.title.empty()) {
        return usageError("add needs --title");
    }
    if (args.start.empty() || args.end.empty()) {
        return usageError("add needs --start and --end");
    }
    if (args.importance < 0 || args.importance > 100) {
        return usageError("--importance must be 0..100");
    }
    if (args.urgency < 0 || args.urgency > 100) {
        return usageError("--urgency must be 0..100");
    }
    const auto start = klend_ario::parseIso8601(args.start);
    if (!start) {
        return usageError("--start is not ISO-8601 UTC (e.g. 2026-10-16T09:00:00Z)");
    }
    const auto end = klend_ario::parseIso8601(args.end);
    if (!end) {
        return usageError("--end is not ISO-8601 UTC (e.g. 2026-10-16T10:00:00Z)");
    }
    EventType type = EventType::Event;
    if (!klend_ario::eventTypeFromName(args.type, type)) {
        return usageError("--type must be event|sleep|wake|alarm");
    }

    Db db;
    if (const int rc = openDb(args, db); rc != 0) {
        return rc;
    }

    Event e;
    e.title = args.title;
    e.startUtc = *start;
    e.endUtc = *end;
    e.type = type;
    e.importance = args.importance;
    e.urgency = args.urgency;
    e.alarmOffset = args.alarmOffset;

    auto id = db.insertEvent(e);
    if (!id) {
        return reportError(id.error());
    }
    if (args.json) {
        std::printf("{\"id\":%lld}\n", id.value());
    } else {
        std::printf("%lld\n", id.value());
    }
    return 0;
}

int commandList(const cli::Args &args) {
    long long from = klend_ario::utcNowSeconds();
    if (!args.from.empty()) {
        const auto parsed = klend_ario::parseIso8601(args.from);
        if (!parsed) {
            return usageError("--from is not ISO-8601 UTC");
        }
        from = *parsed;
    }
    long long to = from + kWeekSeconds;
    if (!args.to.empty()) {
        const auto parsed = klend_ario::parseIso8601(args.to);
        if (!parsed) {
            return usageError("--to is not ISO-8601 UTC");
        }
        to = *parsed;
    }

    Db db;
    if (const int rc = openDb(args, db); rc != 0) {
        return rc;
    }
    auto events = db.listEvents(from, to);
    if (!events) {
        return reportError(events.error());
    }
    if (args.json) {
        std::printf("[");
        for (size_t i = 0; i < events.value().size(); ++i) {
            if (i != 0) {
                std::printf(",");
            }
            printEventJson(events.value()[i]);
        }
        std::printf("]\n");
    } else {
        for (const Event &e : events.value()) {
            printEventText(e);
        }
    }
    return 0;
}

int commandShow(const cli::Args &args) {
    if (args.id <= 0) {
        return usageError("show needs an event id");
    }
    Db db;
    if (const int rc = openDb(args, db); rc != 0) {
        return rc;
    }
    auto event = db.getEvent(args.id);
    if (!event) {
        return reportError(event.error());
    }
    if (args.json) {
        printEventJson(event.value());
        std::printf("\n");
    } else {
        printEventText(event.value());
        if (!event.value().notes.empty()) {
            std::printf("    %s\n", event.value().notes.c_str());
        }
    }
    return 0;
}

int commandRemove(const cli::Args &args) {
    if (args.id <= 0) {
        return usageError("rm needs an event id");
    }
    Db db;
    if (const int rc = openDb(args, db); rc != 0) {
        return rc;
    }
    auto removed = db.removeEvent(args.id);
    if (!removed) {
        return reportError(removed.error());
    }
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    const std::vector<std::string_view> tokens(argv + 1, argv + argc);
    const cli::Args args = cli::parseArgs(tokens);

    if (!args.error.empty()) {
        return usageError(args.error);
    }

    switch (args.verb) {
    case cli::Verb::Help:
        std::fputs(cli::usage().c_str(), stdout);
        return 0;
    case cli::Verb::Add:
        return commandAdd(args);
    case cli::Verb::List:
        return commandList(args);
    case cli::Verb::Show:
        return commandShow(args);
    case cli::Verb::Remove:
        return commandRemove(args);
    case cli::Verb::None:
        break;
    }
    return usageError("no command given");
}
