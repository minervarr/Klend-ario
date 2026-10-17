#include "args.hh"

namespace klend_ario::cli {
namespace {

bool isOption(std::string_view token) { return token.size() >= 2 && token[0] == '-'; }

bool parseLong(std::string_view text, long long &out) {
    if (text.empty()) {
        return false;
    }
    size_t i = 0;
    bool negative = false;
    if (text[0] == '-') {
        negative = true;
        i = 1;
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
        value = value * 10 + (c - '0');
    }
    out = negative ? -value : value;
    return true;
}

} // namespace

Args parseArgs(const std::vector<std::string_view> &argv) {
    Args a;
    size_t i = 0;

    if (argv.empty()) {
        a.verb = Verb::Help;
        return a;
    }

    // The verb is the first non-option token; a leading -h/--help also works.
    if (argv[0] == "-h" || argv[0] == "--help") {
        a.verb = Verb::Help;
        i = 1;
    } else if (!isOption(argv[0])) {
        const std::string_view verb = argv[i++];
        if (verb == "add") {
            a.verb = Verb::Add;
        } else if (verb == "list") {
            a.verb = Verb::List;
        } else if (verb == "show") {
            a.verb = Verb::Show;
        } else if (verb == "rm" || verb == "remove") {
            a.verb = Verb::Remove;
        } else if (verb == "holidays") {
            a.verb = Verb::Holidays;
        } else if (verb == "help") {
            a.verb = Verb::Help;
        } else {
            a.error = "unknown command: " + std::string(verb);
            return a;
        }
    } else {
        a.error = "unknown option: " + std::string(argv[0]);
        return a;
    }

    for (; i < argv.size(); ++i) {
        const std::string_view token = argv[i];

        auto takeValue = [&](std::string &slot, const char *name) -> bool {
            if (i + 1 >= argv.size()) {
                a.error = std::string(name) + " needs a value";
                return false;
            }
            slot = std::string(argv[++i]);
            return true;
        };
        auto takeInt = [&](int &slot, const char *name) -> bool {
            if (i + 1 >= argv.size()) {
                a.error = std::string(name) + " needs a value";
                return false;
            }
            long long parsed = 0;
            if (!parseLong(argv[++i], parsed)) {
                a.error = std::string("bad value for ") + name + ": " + std::string(argv[i]);
                return false;
            }
            slot = static_cast<int>(parsed);
            return true;
        };

        if (token == "--json") {
            a.json = true;
        } else if (token == "--db") {
            if (!takeValue(a.dbPath, "--db")) {
                return a;
            }
        } else if (token == "--key-file") {
            if (!takeValue(a.keyFile, "--key-file")) {
                return a;
            }
        } else if (token == "--title") {
            if (!takeValue(a.title, "--title")) {
                return a;
            }
        } else if (token == "--start") {
            if (!takeValue(a.start, "--start")) {
                return a;
            }
        } else if (token == "--end") {
            if (!takeValue(a.end, "--end")) {
                return a;
            }
        } else if (token == "--type") {
            if (!takeValue(a.type, "--type")) {
                return a;
            }
        } else if (token == "--from") {
            if (!takeValue(a.from, "--from")) {
                return a;
            }
        } else if (token == "--to") {
            if (!takeValue(a.to, "--to")) {
                return a;
            }
        } else if (token == "--country") {
            if (!takeValue(a.country, "--country")) {
                return a;
            }
        } else if (token == "--year") {
            if (!takeInt(a.year, "--year")) {
                return a;
            }
            a.yearGiven = true;
        } else if (token == "--importance") {
            if (!takeInt(a.importance, "--importance")) {
                return a;
            }
        } else if (token == "--urgency") {
            if (!takeInt(a.urgency, "--urgency")) {
                return a;
            }
        } else if (token == "--alarm-offset") {
            if (!takeInt(a.alarmOffset, "--alarm-offset")) {
                return a;
            }
        } else if (token == "--notes") {
            if (!takeValue(a.notes, "--notes")) {
                return a;
            }
        } else if (token == "--hidden") {
            a.hidden = true;
        } else if (token == "-h" || token == "--help") {
            a.verb = Verb::Help;
        } else if (isOption(token)) {
            a.error = "unknown option: " + std::string(token);
            return a;
        } else {
            // A positional argument. Only show/rm take one: the id.
            if (a.verb == Verb::Show || a.verb == Verb::Remove) {
                long long parsed = 0;
                if (!parseLong(token, parsed)) {
                    a.error = "not an id: " + std::string(token);
                    return a;
                }
                a.id = parsed;
            } else {
                a.error = "unexpected argument: " + std::string(token);
                return a;
            }
        }
    }

    return a;
}

std::string usage() {
    return "klend_ario_cli — a local, private calendar\n"
           "\n"
           "usage: klend_ario_cli <command> [options]\n"
           "\n"
           "commands:\n"
           "  add     create an event\n"
           "  list    list events overlapping a time range\n"
           "  show    show one event by id\n"
           "  rm      delete one event by id\n"
           "  holidays  list a country's public holidays for a year\n"
           "\n"
           "add options:\n"
           "  --title T          event title (required)\n"
           "  --start ISO        start, e.g. 2026-10-16T09:00:00Z (required)\n"
           "  --end ISO          end (required)\n"
           "  --type T           event|sleep|wake|alarm (default event)\n"
           "  --importance N     0..100, hue (default 50)\n"
           "  --urgency N        0..100, border (default 50)\n"
           "  --alarm-offset N   seconds before start (default 0)\n"
           "  --notes TEXT       a free-form note\n"
           "  --hidden           hide it in the weekly view (sleep blocks)\n"
           "\n"
           "list options:\n"
           "  --from ISO         lower bound (default: now)\n"
           "  --to ISO           upper bound (default: from + 7 days)\n"
           "\n"
           "holidays options:\n"
           "  --country CC       ISO country code (default PE)\n"
           "  --year N           year 1..9999 (default: current UTC year)\n"
           "\n"
           "common options:\n"
           "  --db PATH          database file (default: the app's state dir)\n"
           "  --key-file PATH    read the database passphrase from this file\n"
           "                     (else $KLEND_ARIO_KEY; else plaintext)\n"
           "  --json             machine-readable output\n"
           "  -h, --help         this text\n";
}

} // namespace klend_ario::cli
