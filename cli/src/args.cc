#include "args.hh"

#include <limits>

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
        const int digit = c - '0';
        if (value > (std::numeric_limits<long long>::max() - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }
    out = negative ? -value : value;
    return true;
}

const char *verbName(Verb verb) {
    switch (verb) {
    case Verb::Add:
        return "add";
    case Verb::Edit:
        return "edit";
    case Verb::Duplicate:
        return "duplicate";
    case Verb::List:
        return "list";
    case Verb::Today:
        return "today";
    case Verb::Week:
        return "week";
    case Verb::Next:
        return "next";
    case Verb::Agenda:
        return "agenda";
    case Verb::Search:
        return "search";
    case Verb::Conflicts:
        return "conflicts";
    case Verb::Free:
        return "free";
    case Verb::Alarms:
        return "alarms";
    case Verb::Show:
        return "show";
    case Verb::Remove:
        return "rm";
    case Verb::Holidays:
        return "holidays";
    case Verb::Help:
        return "help";
    case Verb::None:
        break;
    }
    return "";
}

// Which verbs take a positional id. Add takes a title; help takes a topic.
bool takesId(Verb verb) {
    return verb == Verb::Show || verb == Verb::Remove || verb == Verb::Edit ||
           verb == Verb::Duplicate;
}

bool isQueryVerb(Verb verb) {
    return verb == Verb::List || verb == Verb::Today || verb == Verb::Week ||
           verb == Verb::Next || verb == Verb::Agenda || verb == Verb::Search;
}

bool isIntelligenceVerb(Verb verb) {
    return verb == Verb::Conflicts || verb == Verb::Free || verb == Verb::Alarms;
}

bool isRangeVerb(Verb verb) { return isQueryVerb(verb) || isIntelligenceVerb(verb); }

bool isVisibilityVerb(Verb verb) { return isRangeVerb(verb); }

bool isLimitVerb(Verb verb) { return isQueryVerb(verb) || isIntelligenceVerb(verb); }

bool isTypeVerb(Verb verb) {
    return verb == Verb::Add || verb == Verb::Edit || isQueryVerb(verb) ||
           isIntelligenceVerb(verb);
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
        } else if (verb == "edit" || verb == "update") {
            a.verb = Verb::Edit;
        } else if (verb == "duplicate" || verb == "copy") {
            a.verb = Verb::Duplicate;
        } else if (verb == "list") {
            a.verb = Verb::List;
        } else if (verb == "today") {
            a.verb = Verb::Today;
        } else if (verb == "week") {
            a.verb = Verb::Week;
        } else if (verb == "next") {
            a.verb = Verb::Next;
        } else if (verb == "agenda") {
            a.verb = Verb::Agenda;
        } else if (verb == "search") {
            a.verb = Verb::Search;
        } else if (verb == "conflicts") {
            a.verb = Verb::Conflicts;
        } else if (verb == "free") {
            a.verb = Verb::Free;
        } else if (verb == "alarms") {
            a.verb = Verb::Alarms;
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
            if (!parseLong(argv[++i], parsed) ||
                parsed < std::numeric_limits<int>::min() ||
                parsed > std::numeric_limits<int>::max()) {
                a.error = std::string("bad value for ") + name + ": " + std::string(argv[i]);
                return false;
            }
            slot = static_cast<int>(parsed);
            return true;
        };
        auto takeNonNegative = [&](int &slot, const char *name) -> bool {
            if (!takeInt(slot, name)) {
                return false;
            }
            if (slot < 0) {
                a.error = std::string(name) + " must be >= 0";
                return false;
            }
            return true;
        };
        auto takeVisibility = [&](bool &slot) -> bool {
            if (a.visibleOnly || a.hiddenOnly || a.includeHidden) {
                a.error = "--visible, --hidden-only and --include-hidden are mutually exclusive";
                return false;
            }
            slot = true;
            return true;
        };
        auto queryOnly = [&](const char *name) -> bool {
            if (!isQueryVerb(a.verb)) {
                a.error = std::string(name) + " is only valid for query commands";
                return false;
            }
            return true;
        };
        auto rangeOnly = [&](const char *name) -> bool {
            if (!isRangeVerb(a.verb)) {
                a.error = std::string(name) + " is not valid for this command";
                return false;
            }
            return true;
        };
        auto visibilityOnly = [&](const char *name) -> bool {
            if (!isVisibilityVerb(a.verb)) {
                a.error = std::string(name) + " is not valid for this command";
                return false;
            }
            return true;
        };
        auto limitOnly = [&](const char *name) -> bool {
            if (!isLimitVerb(a.verb)) {
                a.error = std::string(name) + " is not valid for this command";
                return false;
            }
            return true;
        };
        auto typeOnly = [&](const char *name) -> bool {
            if (!isTypeVerb(a.verb)) {
                a.error = std::string(name) + " is not valid for this command";
                return false;
            }
            return true;
        };
        auto mutationOnly = [&](const char *name) -> bool {
            if (isQueryVerb(a.verb)) {
                a.error = std::string(name) + " is not valid for query commands";
                return false;
            }
            return true;
        };

        if (token == "--json") {
            a.json = true;
        } else if (token == "--no-header") {
            a.noHeader = true;
        } else if (token == "--db") {
            if (!takeValue(a.dbPath, "--db")) {
                return a;
            }
        } else if (token == "--key-file") {
            if (!takeValue(a.keyFile, "--key-file")) {
                return a;
            }
        } else if (token == "--format") {
            if (!takeValue(a.format, "--format")) {
                return a;
            }
        } else if (token == "--title") {
            if (!mutationOnly("--title")) {
                return a;
            }
            if (!takeValue(a.title, "--title")) {
                return a;
            }
            a.titleGiven = true;
        } else if (token == "--start") {
            if (!mutationOnly("--start")) {
                return a;
            }
            if (!takeValue(a.start, "--start")) {
                return a;
            }
            a.startGiven = true;
        } else if (token == "--end") {
            if (!mutationOnly("--end")) {
                return a;
            }
            if (!takeValue(a.end, "--end")) {
                return a;
            }
            a.endGiven = true;
        } else if (token == "--duration") {
            if (!mutationOnly("--duration")) {
                return a;
            }
            if (!takeValue(a.duration, "--duration")) {
                return a;
            }
            a.durationGiven = true;
        } else if (token == "--shift") {
            if (!mutationOnly("--shift")) {
                return a;
            }
            if (!takeValue(a.shift, "--shift")) {
                return a;
            }
            a.shiftGiven = true;
        } else if (token == "--type") {
            if (!typeOnly("--type")) {
                return a;
            }
            if (!takeValue(a.type, "--type")) {
                return a;
            }
            a.typeGiven = true;
        } else if (token == "--search") {
            if (!queryOnly("--search")) {
                return a;
            }
            if (a.searchGiven) {
                a.error = "--search is given more than once";
                return a;
            }
            if (!takeValue(a.search, "--search")) {
                return a;
            }
            a.searchGiven = true;
        } else if (token == "--sort") {
            if (!queryOnly("--sort")) {
                return a;
            }
            if (!takeValue(a.sort, "--sort")) {
                return a;
            }
            a.sortGiven = true;
        } else if (token == "--limit") {
            if (!limitOnly("--limit")) {
                return a;
            }
            if (!takeNonNegative(a.limit, "--limit")) {
                return a;
            }
            a.limitGiven = true;
        } else if (token == "--offset") {
            if (!queryOnly("--offset")) {
                return a;
            }
            if (!takeNonNegative(a.offset, "--offset")) {
                return a;
            }
            a.offsetGiven = true;
        } else if (token == "--days") {
            if (!queryOnly("--days")) {
                return a;
            }
            if (!takeNonNegative(a.days, "--days")) {
                return a;
            }
            a.daysGiven = true;
        } else if (token == "--min-duration") {
            if (a.verb != Verb::Free) {
                a.error = "--min-duration is only valid for free";
                return a;
            }
            if (!takeValue(a.minDuration, "--min-duration")) {
                return a;
            }
            a.minDurationGiven = true;
        } else if (token == "--min-importance") {
            if (!queryOnly("--min-importance")) {
                return a;
            }
            if (!takeInt(a.minImportance, "--min-importance")) {
                return a;
            }
            a.minImportanceGiven = true;
        } else if (token == "--max-importance") {
            if (!queryOnly("--max-importance")) {
                return a;
            }
            if (!takeInt(a.maxImportance, "--max-importance")) {
                return a;
            }
            a.maxImportanceGiven = true;
        } else if (token == "--min-urgency") {
            if (!queryOnly("--min-urgency")) {
                return a;
            }
            if (!takeInt(a.minUrgency, "--min-urgency")) {
                return a;
            }
            a.minUrgencyGiven = true;
        } else if (token == "--max-urgency") {
            if (!queryOnly("--max-urgency")) {
                return a;
            }
            if (!takeInt(a.maxUrgency, "--max-urgency")) {
                return a;
            }
            a.maxUrgencyGiven = true;
        } else if (token == "--visible") {
            if (!visibilityOnly("--visible")) {
                return a;
            }
            if (!takeVisibility(a.visibleOnly)) {
                return a;
            }
        } else if (token == "--hidden-only") {
            if (!visibilityOnly("--hidden-only")) {
                return a;
            }
            if (!takeVisibility(a.hiddenOnly)) {
                return a;
            }
        } else if (token == "--include-hidden") {
            if (!visibilityOnly("--include-hidden")) {
                return a;
            }
            if (!takeVisibility(a.includeHidden)) {
                return a;
            }
        } else if (token == "--from") {
            if (!rangeOnly("--from")) {
                return a;
            }
            if (!takeValue(a.from, "--from")) {
                return a;
            }
            a.fromGiven = true;
        } else if (token == "--to") {
            if (!rangeOnly("--to")) {
                return a;
            }
            if (!takeValue(a.to, "--to")) {
                return a;
            }
            a.toGiven = true;
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
            if (!mutationOnly("--importance")) {
                return a;
            }
            if (!takeInt(a.importance, "--importance")) {
                return a;
            }
            a.importanceGiven = true;
        } else if (token == "--urgency") {
            if (!mutationOnly("--urgency")) {
                return a;
            }
            if (!takeInt(a.urgency, "--urgency")) {
                return a;
            }
            a.urgencyGiven = true;
        } else if (token == "--alarm-offset") {
            if (!mutationOnly("--alarm-offset")) {
                return a;
            }
            if (!takeInt(a.alarmOffset, "--alarm-offset")) {
                return a;
            }
            a.alarmOffsetGiven = true;
        } else if (token == "--notes") {
            if (!mutationOnly("--notes")) {
                return a;
            }
            if (!takeValue(a.notes, "--notes")) {
                return a;
            }
            a.notesGiven = true;
        } else if (token == "--clear-notes") {
            if (!mutationOnly("--clear-notes")) {
                return a;
            }
            a.notes.clear();
            a.notesGiven = true;
            a.clearNotes = true;
        } else if (token == "--hidden") {
            if (isQueryVerb(a.verb)) {
                a.error = "for queries use --hidden-only (or --visible/--include-hidden)";
                return a;
            }
            a.hidden = true;
            a.hiddenGiven = true;
        } else if (token == "--unhide") {
            if (!mutationOnly("--unhide")) {
                return a;
            }
            a.hidden = false;
            a.hiddenGiven = true;
        } else if (token == "-h" || token == "--help") {
            if (a.verb != Verb::Help) {
                a.helpTopic = verbName(a.verb);
            }
            a.verb = Verb::Help;
        } else if (isOption(token)) {
            a.error = "unknown option: " + std::string(token);
            return a;
        } else if (a.verb == Verb::Help) {
            a.helpTopic = std::string(token);
        } else if (takesId(a.verb)) {
            long long parsed = 0;
            if (!parseLong(token, parsed)) {
                a.error = "not an id: " + std::string(token);
                return a;
            }
            if (a.id != 0) {
                a.error = "unexpected argument: " + std::string(token);
                return a;
            }
            a.id = parsed;
        } else if (a.verb == Verb::Search) {
            if (a.searchGiven) {
                a.error = "search takes exactly one query (quote it if it contains spaces)";
                return a;
            }
            a.search = std::string(token);
            a.searchGiven = true;
        } else if (a.verb == Verb::Add) {
            if (a.titleGiven) {
                a.error = "unexpected argument: " + std::string(token);
                return a;
            }
            a.title = std::string(token);
            a.titleGiven = true;
        } else {
            a.error = "unexpected argument: " + std::string(token);
            return a;
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
           "  add        create an event\n"
           "  edit       change fields of an event\n"
           "  duplicate  copy an event\n"
           "  list       list events overlapping a time range\n"
           "  today      what is on today\n"
           "  week       what is on this week\n"
           "  next       the next event still running or starting\n"
           "  agenda     the next N days from now\n"
           "  search     find text in titles and notes\n"
           "  conflicts  find overlapping events\n"
           "  free       find free slots in a range\n"
           "  alarms     list upcoming alarm triggers\n"
           "  show       show one event by id\n"
           "  rm         delete one event by id\n"
           "  holidays   list a country's public holidays for a year\n"
           "  help       show help (help <command>)\n"
           "\n"
           "time expressions are UTC:\n"
           "  2026-10-16T09:00:00Z  2026-10-16  now  today  tomorrow  yesterday\n"
           "  week  next-week  last-week  sunday .. saturday\n"
           "  +90m  -1d  today+2h  week-1d\n"
           "durations: 90m  2h  1h30m  1d  1w   (a bare number means seconds)\n"
           "\n"
           "query options (list/today/week/next/agenda/search):\n"
           "  --from EXPR --to EXPR  --type T  --search TEXT\n"
           "  --min-importance N --max-importance N\n"
           "  --min-urgency N --max-urgency N\n"
           "  --sort S  --limit N  --offset N\n"
           "  --visible | --hidden-only | --include-hidden\n"
           "\n"
           "intelligence options (conflicts/free/alarms):\n"
           "  --from EXPR --to EXPR  --type T  --limit N\n"
           "  --visible | --hidden-only | --include-hidden\n"
           "  free also takes --min-duration D (default 30m)\n"
           "\n"
           "common options:\n"
           "  --db PATH          database file (default: the app's state dir)\n"
           "  --key-file PATH    read the database passphrase from this file\n"
           "                     (else $KLEND_ARIO_KEY; else plaintext)\n"
           "  --format FMT       text|table|json|csv|tsv (default text)\n"
           "  --json             same as --format json\n"
           "  --no-header        omit the header row of csv/tsv/table\n"
           "  -h, --help         this text\n"
           "\n"
           "run `klend_ario_cli help <command>` for a verb's options\n";
}

namespace {

std::string queryOptionsUsage() {
    return "  --from EXPR --to EXPR   override the default range\n"
           "  --type T                 event|sleep|wake|alarm\n"
           "  --search TEXT            literal substring of title or notes\n"
           "  --min-importance N       inclusive, 0..100\n"
           "  --max-importance N\n"
           "  --min-urgency N\n"
           "  --max-urgency N\n"
           "  --sort S                 start|-start|importance|urgency|updated|title\n"
           "  --limit N --offset N     paginate (limit 0 returns no rows)\n"
           "  --visible                only visible rows\n"
           "  --hidden-only            only hidden rows\n"
           "  --include-hidden         both (overrides a view's visible default)\n";
}

} // namespace

std::string usageFor(std::string_view topic) {
    if (topic == "add") {
        return "usage: klend_ario_cli add [TITLE] --start EXPR [--end EXPR | --duration D]\n"
               "                       [--type T] [--importance N] [--urgency N]\n"
               "                       [--alarm-offset N] [--notes TEXT] [--hidden]\n"
               "\n"
               "  TITLE can also be given with --title.\n"
               "  A missing --start means now, but only when --duration is given.\n";
    }
    if (topic == "edit") {
        return "usage: klend_ario_cli edit <id> [--title T] [--start EXPR] [--end EXPR]\n"
               "                             [--duration D] [--shift D] [--type T]\n"
               "                             [--importance N] [--urgency N]\n"
               "                             [--alarm-offset N] [--notes TEXT | --clear-notes]\n"
               "                             [--hidden | --unhide]\n"
               "\n"
               "  Only the flags you pass change. Moving --start preserves the duration\n"
               "  unless --end or --duration is also given. --shift moves the whole\n"
               "  event and cannot be combined with --start/--end/--duration.\n";
    }
    if (topic == "duplicate") {
        return "usage: klend_ario_cli duplicate <id> [--start EXPR] [--shift D]\n"
               "\n"
               "  Copies every field into a new event and prints its id.\n";
    }
    if (topic == "list") {
        return "usage: klend_ario_cli list [--from EXPR] [--to EXPR] [query options]\n"
               "\n"
               "  Default range is today 00:00 UTC plus 7 days. Hidden rows are\n"
               "  included. `agenda` is the from-now, visible-only short form.\n"
               "\n" +
               queryOptionsUsage();
    }
    if (topic == "today") {
        return "usage: klend_ario_cli today [query options]\n"
               "\n"
               "  Events overlapping today [00:00, tomorrow). Visible rows only by\n"
               "  default; --include-hidden shows the rest.\n"
               "\n" +
               queryOptionsUsage();
    }
    if (topic == "week") {
        return "usage: klend_ario_cli week [query options]\n"
               "\n"
               "  Events overlapping the current Sunday-based week. Visible rows only\n"
               "  by default; --include-hidden shows the rest.\n"
               "\n" +
               queryOptionsUsage();
    }
    if (topic == "next") {
        return "usage: klend_ario_cli next [--limit N] [--offset N] [query options]\n"
               "\n"
               "  The first event with end > now: an event already underway counts.\n"
               "  Visible rows only, limit 1 by default; --limit N gives the next N.\n"
               "  Sort is fixed to start-ascending.\n"
               "\n" +
               queryOptionsUsage();
    }
    if (topic == "agenda") {
        return "usage: klend_ario_cli agenda [--days N] [query options]\n"
               "\n"
               "  From now through the next N days (default 7), visible rows only.\n"
               "  --from EXPR moves the start; --days and --to are mutually exclusive.\n"
               "\n" +
               queryOptionsUsage();
    }
    if (topic == "search") {
        return "usage: klend_ario_cli search TEXT [query options]\n"
               "\n"
               "  Literal, ASCII case-insensitive substring of title or notes, over\n"
               "  all time. `%` and `_` are ordinary characters. Newest first.\n"
               "  Quote a phrase. Hidden rows are included.\n"
               "\n" +
               queryOptionsUsage();
    }
    if (topic == "conflicts") {
        return "usage: klend_ario_cli conflicts [--from EXPR] [--to EXPR] [--type T]\n"
               "                               [--limit N] [--visible | --hidden-only |\n"
               "                               --include-hidden]\n"
               "\n"
               "  Overlapping visible events in the range (default: today + 7 days).\n"
               "  Touching endpoints and zero-length events do not conflict; alarm\n"
               "  rows are reminders, not blocks. A triple overlap reports three pairs.\n";
    }
    if (topic == "free") {
        return "usage: klend_ario_cli free [--from EXPR] [--to EXPR] [--min-duration D]\n"
               "                          [--limit N] [--visible | --hidden-only |\n"
               "                          --include-hidden]\n"
               "\n"
               "  Gaps between busy events (default: now + 7 days, minimum 30m).\n"
               "  Overlapping events merge; hidden rows are not busy unless included.\n"
               "  Alarm rows are reminders and never block time.\n";
    }
    if (topic == "alarms") {
        return "usage: klend_ario_cli alarms [--from EXPR] [--to EXPR] [--type T]\n"
               "                             [--limit N] [--visible | --hidden-only |\n"
               "                             --include-hidden]\n"
               "\n"
               "  Upcoming triggers (default: now + 7 days), sorted by fire time.\n"
               "  Hidden rows ARE included: hiding a sleep block does not cancel its\n"
               "  alarm. Each event contributes T-offset (if set) and T=0.\n";
    }
    if (topic == "show") {
        return "usage: klend_ario_cli show <id>\n";
    }
    if (topic == "rm") {
        return "usage: klend_ario_cli rm <id>\n"
               "\n"
               "  Removing an id that does not exist succeeds; the operation is\n"
               "  idempotent so a script can delete without checking first.\n";
    }
    if (topic == "holidays") {
        return "usage: klend_ario_cli holidays [--country CC] [--year N]\n"
               "\n"
               "  A civil-date query; it never opens the database.\n";
    }
    if (topic == "help") {
        return "usage: klend_ario_cli help [command]\n";
    }
    return {};
}

} // namespace klend_ario::cli
