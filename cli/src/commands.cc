// The command layer: parse (done) -> call one core function -> print. No
// business logic lives here (essentialAgent/AGENTS.md §4). Everything the CLI
// knows how to do is a thin wrapping of a klend_ario:: call.
//
// Rendering is the one exception that is not "logic": it lives in format.cc as
// pure functions returning strings, so the bytes a user sees are testable.

#include "commands.hh"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>

#include "arc/fs/paths.hh"
#include "format.hh"
#include "klend_ario/db.hh"
#include "klend_ario/event.hh"
#include "klend_ario/holidays.hh"
#include "klend_ario/sync.hh"
#include "klend_ario/timeexpr.hh"

namespace klend_ario::cli {
namespace {

using klend_ario::Db;
using klend_ario::Event;
using klend_ario::EventQuery;
using klend_ario::EventSort;
using klend_ario::EventType;
using klend_ario::Visibility;

constexpr long long kDaySeconds = 86400;
constexpr long long kWeekSeconds = 7 * kDaySeconds;

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
    return kExitError;
}

int usageError(const std::string &message) {
    std::fprintf(stderr, "error: %s\n\n", message.c_str());
    std::fputs(usage().c_str(), stderr);
    return kExitUsage;
}

// 00:00 UTC of the day containing `utcSeconds`; correct before 1970.
long long startOfDayUtc(long long utcSeconds) {
    const long long secondsIntoDay = ((utcSeconds % kDaySeconds) + kDaySeconds) % kDaySeconds;
    return utcSeconds - secondsIntoDay;
}

bool addChecked(long long a, long long b, long long &out) {
    if ((b > 0 && a > std::numeric_limits<long long>::max() - b) ||
        (b < 0 && a < std::numeric_limits<long long>::min() - b)) {
        return false;
    }
    out = a + b;
    return true;
}

// A shift may go backwards, so it accepts an explicit sign that the plain
// duration grammar (non-negative by design) does not.
std::optional<long long> parseSignedDuration(std::string_view text) {
    bool negative = false;
    if (!text.empty() && (text[0] == '+' || text[0] == '-')) {
        negative = text[0] == '-';
        text.remove_prefix(1);
    }
    const auto duration = parseDurationExpr(text);
    if (!duration) {
        return std::nullopt;
    }
    return negative ? -*duration : *duration;
}

FormatOptions formatOptionsOf(const Args &args) {
    FormatOptions options;
    options.header = !args.noHeader;
    return options;
}

// The passphrase, from --key-file (first line) or $KLEND_ARIO_KEY. An explicit
// file wins over the environment. Empty means "no encryption" — see openDb.
std::string resolvePassphrase(const Args &args, std::string &error) {
    if (!args.keyFile.empty()) {
        std::ifstream in(args.keyFile, std::ios::binary);
        std::string line;
        if (!std::getline(in, line)) {
            error = "cannot read key file: " + args.keyFile;
            return {};
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back(); // tolerate a file saved on Windows
        }
        return line;
    }
    if (const char *env = std::getenv("KLEND_ARIO_KEY"); env != nullptr) {
        return env;
    }
    return {};
}

int openDb(const Args &args, Db &out) {
    const std::string path = args.dbPath.empty() ? defaultDbPath() : args.dbPath;

    std::string keyError;
    const std::string passphrase = resolvePassphrase(args, keyError);
    if (!keyError.empty()) {
        return usageError(keyError);
    }

    auto opened = passphrase.empty() ? Db::open(path) : Db::openEncrypted(path, passphrase);
    if (!opened) {
        if (passphrase.empty()) {
            std::fprintf(stderr, "error: %s\n", opened.error().message.c_str());
            std::fprintf(stderr, "       (if the database is encrypted, pass --key-file <path> "
                                 "or set $KLEND_ARIO_KEY)\n");
            return kExitError;
        }
        return reportError(opened.error());
    }
    out = opened.take();

    // The sync policy is to DETECT conflict copies, never merge (MEMORY.md §7).
    // Say so on every open, so a conflict cannot sit there unnoticed.
    for (const std::string &conflict : klend_ario::syncConflictCopies(path)) {
        std::fprintf(stderr, "warning: Syncthing conflict copy present: %s\n", conflict.c_str());
    }
    return kExitOk;
}

std::optional<long long> parseInstant(const std::string &text, long long nowUtc) {
    return klend_ario::parseInstantExpr(text, nowUtc);
}

int validateImportance(const Args &args) {
    if (args.importanceGiven && (args.importance < 0 || args.importance > 100)) {
        return usageError("--importance must be 0..100");
    }
    if (args.urgencyGiven && (args.urgency < 0 || args.urgency > 100)) {
        return usageError("--urgency must be 0..100");
    }
    return kExitOk;
}

// Applies only the fields the user actually supplied. Shared by add (where the
// struct starts empty) and edit (where it starts from the stored row), so the
// two cannot drift on type/range rules.
int applyEventFields(const Args &args, Event &event) {
    if (args.titleGiven) {
        if (args.title.empty()) {
            return usageError("--title must not be empty");
        }
        event.title = args.title;
    }
    if (args.typeGiven) {
        EventType type = EventType::Event;
        if (!klend_ario::eventTypeFromName(args.type, type)) {
            return usageError("--type must be event|sleep|wake|alarm");
        }
        event.type = type;
    }
    if (args.importanceGiven) {
        event.importance = args.importance;
    }
    if (args.urgencyGiven) {
        event.urgency = args.urgency;
    }
    if (args.alarmOffsetGiven) {
        event.alarmOffset = args.alarmOffset;
    }
    if (args.notesGiven) {
        event.notes = args.notes;
    }
    if (args.hiddenGiven) {
        event.hidden = args.hidden;
    }
    return validateImportance(args);
}

int commandAdd(const Args &args, OutputFormat format, const FormatOptions &) {
    if (!args.titleGiven || args.title.empty()) {
        return usageError("add needs a title (positional or --title)");
    }
    if (args.endGiven && args.durationGiven) {
        return usageError("use --end or --duration, not both");
    }
    if (!args.startGiven && !args.durationGiven) {
        return usageError("add needs --start (or --duration, which starts now)");
    }

    const long long now = klend_ario::utcNowSeconds();
    long long startUtc = now;
    if (args.startGiven) {
        const auto parsed = parseInstant(args.start, now);
        if (!parsed) {
            return usageError("--start is not a UTC instant (e.g. 2026-10-16T09:00:00Z, today, now+2h)");
        }
        startUtc = *parsed;
    }

    long long endUtc = 0;
    if (args.endGiven) {
        const auto parsed = parseInstant(args.end, now);
        if (!parsed) {
            return usageError("--end is not a UTC instant (e.g. 2026-10-16T10:00:00Z, today+1h)");
        }
        endUtc = *parsed;
    } else {
        const auto duration = parseDurationExpr(args.duration);
        if (!duration) {
            return usageError("--duration is not a duration (e.g. 90m, 2h, 1h30m)");
        }
        if (!addChecked(startUtc, *duration, endUtc)) {
            return usageError("--duration is out of range");
        }
    }
    if (endUtc < startUtc) {
        return usageError("--end is before --start");
    }

    Event event;
    event.startUtc = startUtc;
    event.endUtc = endUtc;
    if (const int rc = applyEventFields(args, event); rc != kExitOk) {
        return rc;
    }

    Db db;
    if (const int rc = openDb(args, db); rc != kExitOk) {
        return rc;
    }

    auto id = db.insertEvent(event);
    if (!id) {
        return reportError(id.error());
    }
    std::fputs(formatId(id.value(), format).c_str(), stdout);
    return kExitOk;
}

int commandEdit(const Args &args, OutputFormat format, const FormatOptions &) {
    if (args.id <= 0) {
        return usageError("edit needs an event id");
    }
    if (args.shiftGiven && (args.startGiven || args.endGiven || args.durationGiven)) {
        return usageError("--shift cannot be combined with --start/--end/--duration");
    }
    if (args.endGiven && args.durationGiven) {
        return usageError("use --end or --duration, not both");
    }

    Db db;
    if (const int rc = openDb(args, db); rc != kExitOk) {
        return rc;
    }
    auto stored = db.getEvent(args.id);
    if (!stored) {
        return reportError(stored.error());
    }
    Event event = stored.value();

    if (const int rc = applyEventFields(args, event); rc != kExitOk) {
        return rc;
    }

    const long long now = klend_ario::utcNowSeconds();
    const long long oldDuration = event.endUtc - event.startUtc;
    long long newStart = event.startUtc;
    long long newEnd = event.endUtc;

    if (args.startGiven) {
        const auto parsed = parseInstant(args.start, now);
        if (!parsed) {
            return usageError("--start is not a UTC instant");
        }
        newStart = *parsed;
    }
    if (args.shiftGiven) {
        const auto shift = parseSignedDuration(args.shift);
        if (!shift) {
            return usageError("--shift is not a duration (e.g. 1h, -30m)");
        }
        if (!addChecked(newStart, *shift, newStart) || !addChecked(newEnd, *shift, newEnd)) {
            return usageError("--shift is out of range");
        }
    }

    if (args.durationGiven) {
        const auto duration = parseDurationExpr(args.duration);
        if (!duration) {
            return usageError("--duration is not a duration (e.g. 90m, 2h)");
        }
        if (!addChecked(newStart, *duration, newEnd)) {
            return usageError("--duration is out of range");
        }
    } else if (args.endGiven) {
        const auto parsed = parseInstant(args.end, now);
        if (!parsed) {
            return usageError("--end is not a UTC instant");
        }
        newEnd = *parsed;
    } else if (args.startGiven) {
        // Moving the start alone keeps the event's length; changing the length
        // is an explicit --end or --duration.
        if (!addChecked(newStart, oldDuration, newEnd)) {
            return usageError("event duration is out of range");
        }
    }

    if (newEnd < newStart) {
        return usageError("--end is before --start");
    }
    event.startUtc = newStart;
    event.endUtc = newEnd;

    auto updated = db.updateEvent(event);
    if (!updated) {
        return reportError(updated.error());
    }
    std::fputs(formatId(event.id, format).c_str(), stdout);
    return kExitOk;
}

int commandDuplicate(const Args &args, OutputFormat format, const FormatOptions &) {
    if (args.id <= 0) {
        return usageError("duplicate needs an event id");
    }
    if (args.startGiven && args.shiftGiven) {
        return usageError("duplicate takes --start or --shift, not both");
    }
    if (args.endGiven || args.durationGiven) {
        return usageError("duplicate keeps the original duration; use --start or --shift");
    }

    Db db;
    if (const int rc = openDb(args, db); rc != kExitOk) {
        return rc;
    }
    auto stored = db.getEvent(args.id);
    if (!stored) {
        return reportError(stored.error());
    }
    Event copy = stored.value();
    copy.id = 0;
    copy.createdUtc = 0;
    copy.updatedUtc = 0;

    const long long now = klend_ario::utcNowSeconds();
    if (args.startGiven) {
        const auto parsed = parseInstant(args.start, now);
        if (!parsed) {
            return usageError("--start is not a UTC instant");
        }
        const long long duration = copy.endUtc - copy.startUtc;
        copy.startUtc = *parsed;
        if (!addChecked(copy.startUtc, duration, copy.endUtc)) {
            return usageError("event duration is out of range");
        }
    }
    if (args.shiftGiven) {
        const auto shift = parseSignedDuration(args.shift);
        if (!shift) {
            return usageError("--shift is not a duration (e.g. 1h, -30m)");
        }
        if (!addChecked(copy.startUtc, *shift, copy.startUtc) ||
            !addChecked(copy.endUtc, *shift, copy.endUtc)) {
            return usageError("--shift is out of range");
        }
    }

    auto id = db.insertEvent(copy);
    if (!id) {
        return reportError(id.error());
    }
    std::fputs(formatId(id.value(), format).c_str(), stdout);
    return kExitOk;
}

std::string trimCopy(std::string_view text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

// A hidden row is a real row: list/search are data access and see everything.
// The view verbs mirror the weekly view, which hides sleep blocks by default.
Visibility defaultVisibilityFor(Verb verb) {
    if (verb == Verb::List || verb == Verb::Search) {
        return Visibility::Any;
    }
    return Visibility::Visible;
}

int resolveVisibility(const Args &args, Verb verb, Visibility &out) {
    const int chosen = (args.visibleOnly ? 1 : 0) + (args.hiddenOnly ? 1 : 0) +
                       (args.includeHidden ? 1 : 0);
    if (chosen > 1) {
        return usageError("--visible, --hidden-only and --include-hidden are mutually exclusive");
    }
    if (args.hiddenOnly) {
        out = Visibility::Hidden;
    } else if (args.includeHidden) {
        out = Visibility::Any;
    } else if (args.visibleOnly) {
        out = Visibility::Visible;
    } else {
        out = defaultVisibilityFor(verb);
    }
    return kExitOk;
}

int resolveSort(const Args &args, Verb verb, EventSort &out) {
    if (verb == Verb::Next && args.sortGiven) {
        return usageError("next is always ordered by start; use --limit to widen it");
    }
    if (!args.sortGiven) {
        // Search is a find, so the most recent match is usually the one meant.
        out = verb == Verb::Search ? EventSort::StartDesc : EventSort::Start;
        return kExitOk;
    }
    if (!klend_ario::eventSortFromName(args.sort, out)) {
        return usageError("--sort must be start|-start|importance|urgency|updated|title");
    }
    return kExitOk;
}

int applyQueryFilters(const Args &args, EventQuery &query) {
    if (args.typeGiven) {
        EventType type = EventType::Event;
        if (!klend_ario::eventTypeFromName(args.type, type)) {
            return usageError("--type must be event|sleep|wake|alarm");
        }
        query.type = type;
    }

    if (args.minImportanceGiven) {
        if (args.minImportance < 0 || args.minImportance > 100) {
            return usageError("--min-importance must be 0..100");
        }
        query.minImportance = args.minImportance;
    }
    if (args.maxImportanceGiven) {
        if (args.maxImportance < 0 || args.maxImportance > 100) {
            return usageError("--max-importance must be 0..100");
        }
        query.maxImportance = args.maxImportance;
    }
    if (query.minImportance > query.maxImportance) {
        return usageError("--min-importance is greater than --max-importance");
    }

    if (args.minUrgencyGiven) {
        if (args.minUrgency < 0 || args.minUrgency > 100) {
            return usageError("--min-urgency must be 0..100");
        }
        query.minUrgency = args.minUrgency;
    }
    if (args.maxUrgencyGiven) {
        if (args.maxUrgency < 0 || args.maxUrgency > 100) {
            return usageError("--max-urgency must be 0..100");
        }
        query.maxUrgency = args.maxUrgency;
    }
    if (query.minUrgency > query.maxUrgency) {
        return usageError("--min-urgency is greater than --max-urgency");
    }

    if (args.searchGiven) {
        const std::string text = trimCopy(args.search);
        if (text.empty()) {
            return usageError("--search must not be empty");
        }
        query.text = text;
    }
    return kExitOk;
}

int resolveQueryRange(const Args &args, Verb verb, long long nowUtc, EventQuery &query) {
    const long long dayStart = startOfDayUtc(nowUtc);
    long long from = 0;

    switch (verb) {
    case Verb::List:
    case Verb::Today:
        from = dayStart;
        break;
    case Verb::Week:
        from = klend_ario::startOfWeekUtc(nowUtc);
        break;
    case Verb::Next:
    case Verb::Agenda:
        from = nowUtc;
        break;
    case Verb::Search:
        from = std::numeric_limits<long long>::min();
        break;
    default:
        return usageError("internal error: not a query verb");
    }

    if (args.fromGiven) {
        const auto parsed = parseInstant(args.from, nowUtc);
        if (!parsed) {
            return usageError("--from is not a UTC instant (e.g. today, 2026-10-16, now)");
        }
        from = *parsed;
    }

    if (verb != Verb::Agenda && args.daysGiven) {
        return usageError("--days is only valid for agenda");
    }
    if (verb == Verb::Agenda && args.daysGiven && args.toGiven) {
        return usageError("agenda takes --days or --to, not both");
    }

    long long to = 0;
    switch (verb) {
    case Verb::List:
    case Verb::Week:
        if (!addChecked(from, kWeekSeconds, to)) {
            return usageError("--from is out of range");
        }
        break;
    case Verb::Today:
        if (!addChecked(from, kDaySeconds, to)) {
            return usageError("--from is out of range");
        }
        break;
    case Verb::Next:
    case Verb::Search:
        to = std::numeric_limits<long long>::max();
        break;
    case Verb::Agenda: {
        if (args.toGiven) {
            const auto parsed = parseInstant(args.to, nowUtc);
            if (!parsed) {
                return usageError("--to is not a UTC instant");
            }
            to = *parsed;
        } else {
            const int days = args.daysGiven ? args.days : 7;
            if (days < 1 || days > 3660) {
                return usageError("--days must be 1..3660");
            }
            const long long span = static_cast<long long>(days) * kDaySeconds;
            if (!addChecked(from, span, to)) {
                return usageError("--days is out of range");
            }
        }
        break;
    }
    default:
        return usageError("internal error: not a query verb");
    }

    if (verb != Verb::Agenda && args.toGiven) {
        const auto parsed = parseInstant(args.to, nowUtc);
        if (!parsed) {
            return usageError("--to is not a UTC instant (e.g. week, tomorrow, 2026-10-23)");
        }
        to = *parsed;
    }
    if (to < from) {
        return usageError("--to is before --from");
    }

    query.fromUtc = from;
    query.toUtc = to;
    return kExitOk;
}

int commandQuery(const Args &args, Verb verb, OutputFormat format, const FormatOptions &options) {
    if (verb == Verb::Search && !args.searchGiven) {
        return usageError("search needs a query (quote it if it contains spaces)");
    }

    const long long now = klend_ario::utcNowSeconds();
    EventQuery query;

    if (const int rc = resolveQueryRange(args, verb, now, query); rc != kExitOk) {
        return rc;
    }
    if (const int rc = applyQueryFilters(args, query); rc != kExitOk) {
        return rc;
    }
    if (verb == Verb::Search && query.text.empty()) {
        return usageError("search needs a non-empty query");
    }
    if (const int rc = resolveVisibility(args, verb, query.visibility); rc != kExitOk) {
        return rc;
    }
    if (const int rc = resolveSort(args, verb, query.sort); rc != kExitOk) {
        return rc;
    }

    // `next` is singular unless the caller widens it; everything else is
    // unbounded unless --limit says otherwise.
    query.limit = args.limitGiven ? args.limit : (verb == Verb::Next ? 1 : -1);
    query.offset = args.offset;

    Db db;
    if (const int rc = openDb(args, db); rc != kExitOk) {
        return rc;
    }
    auto events = db.queryEvents(query);
    if (!events) {
        return reportError(events.error());
    }
    std::fputs(formatEvents(events.value(), format, options).c_str(), stdout);
    return kExitOk;
}

int commandShow(const Args &args, OutputFormat format, const FormatOptions &options) {
    if (args.id <= 0) {
        return usageError("show needs an event id");
    }
    Db db;
    if (const int rc = openDb(args, db); rc != kExitOk) {
        return rc;
    }
    auto event = db.getEvent(args.id);
    if (!event) {
        return reportError(event.error());
    }
    std::fputs(formatEventDetail(event.value(), format, options).c_str(), stdout);
    return kExitOk;
}

int commandRemove(const Args &args, OutputFormat format, const FormatOptions &) {
    if (args.id <= 0) {
        return usageError("rm needs an event id");
    }
    Db db;
    if (const int rc = openDb(args, db); rc != kExitOk) {
        return rc;
    }
    auto removed = db.removeEvent(args.id);
    if (!removed) {
        return reportError(removed.error());
    }
    // rm prints the id it removed so a pipeline can log what happened without
    // re-reading the event, which `show` would no longer find.
    std::fputs(formatId(args.id, format).c_str(), stdout);
    return kExitOk;
}

// Holidays are civil dates, not instants, and never need the database. The
// core adapter turns the library's optional answers into arc::Result, so an
// unknown country is the only error path here.
int commandHolidays(const Args &args, OutputFormat format, const FormatOptions &options) {
    int year = args.year;
    if (!args.yearGiven) {
        year = klend_ario::weekOfYearUtc(klend_ario::utcNowSeconds()).year;
    }
    if (year < 1 || year > 9999) {
        return usageError("--year must be 1..9999");
    }

    auto calendar = klend_ario::holidaysFor(args.country, year);
    if (!calendar) {
        return reportError(calendar.error());
    }
    std::fputs(formatHolidays(calendar.value(), format, options).c_str(), stdout);
    return kExitOk;
}

} // namespace

int runCommand(const Args &args) {
    if (!args.error.empty()) {
        return usageError(args.error);
    }

    if (args.verb == Verb::Help) {
        if (args.helpTopic.empty()) {
            std::fputs(usage().c_str(), stdout);
            return kExitOk;
        }
        const std::string topic = usageFor(args.helpTopic);
        if (topic.empty()) {
            return usageError("unknown help topic: " + args.helpTopic);
        }
        std::fputs(topic.c_str(), stdout);
        return kExitOk;
    }

    OutputFormat format = OutputFormat::Text;
    if (args.json) {
        format = OutputFormat::Json;
    } else if (!parseOutputFormat(args.format, format)) {
        return usageError("unknown --format: " + args.format +
                          " (text|table|json|csv|tsv)");
    }
    const FormatOptions options = formatOptionsOf(args);

    switch (args.verb) {
    case Verb::Add:
        return commandAdd(args, format, options);
    case Verb::Edit:
        return commandEdit(args, format, options);
    case Verb::Duplicate:
        return commandDuplicate(args, format, options);
    case Verb::List:
        return commandQuery(args, Verb::List, format, options);
    case Verb::Today:
        return commandQuery(args, Verb::Today, format, options);
    case Verb::Week:
        return commandQuery(args, Verb::Week, format, options);
    case Verb::Next:
        return commandQuery(args, Verb::Next, format, options);
    case Verb::Agenda:
        return commandQuery(args, Verb::Agenda, format, options);
    case Verb::Search:
        return commandQuery(args, Verb::Search, format, options);
    case Verb::Show:
        return commandShow(args, format, options);
    case Verb::Remove:
        return commandRemove(args, format, options);
    case Verb::Holidays:
        return commandHolidays(args, format, options);
    case Verb::Help:
    case Verb::None:
        break;
    }
    return usageError("no command given");
}

} // namespace klend_ario::cli
