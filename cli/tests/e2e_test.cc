#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "commands.hh"

#include "klend_ario/db.hh"

using namespace klend_ario::cli;

namespace {

int run(std::vector<std::string_view> tokens) { return runCommand(parseArgs(tokens)); }

#ifndef _WIN32
// Capture stdout for ONE command. The query verbs are only wired correctly if
// the filter actually reaches core, and an exit code cannot see that; this
// keeps the check in-process (no shell, no second binary) and restores stdout
// before the assertion so a failure still prints normally.
struct Capture {
    int code = kExitError;
    std::string output;
};

Capture runCaptured(std::vector<std::string_view> tokens) {
    std::fflush(stdout);
    FILE *file = std::tmpfile();
    assert(file != nullptr);
    const int saved = dup(fileno(stdout));
    assert(saved >= 0);
    assert(dup2(fileno(file), fileno(stdout)) >= 0);

    Capture capture;
    capture.code = runCommand(parseArgs(tokens));

    std::fflush(stdout);
    assert(dup2(saved, fileno(stdout)) >= 0);
    close(saved);

    std::rewind(file);
    char buffer[512];
    size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        capture.output.append(buffer, read);
    }
    std::fclose(file);
    return capture;
}

size_t countLines(const std::string &text) {
    size_t lines = 0;
    for (const char c : text) {
        if (c == '\n') {
            ++lines;
        }
    }
    return lines;
}
#endif

} // namespace

// Drives the REAL command layer against a temporary database file — the
// end-to-end check from essentialAgent/TESTING.md. It would fail if a verb
// stopped reaching core, which parsing-only tests cannot catch.
int main() {
    // The CLI reads $KLEND_ARIO_KEY; clear it so these tests exercise the
    // plaintext path and the explicit --key-file path, not whatever the
    // developer's shell happens to have set.
#ifdef _WIN32
    _putenv_s("KLEND_ARIO_KEY", "");
#else
    unsetenv("KLEND_ARIO_KEY");
#endif

    const char *dbPath = "klend_e2e_test.db";
    std::remove(dbPath);

    // add: a normal event.
    assert(run({"add", "--db", dbPath, "--title", "Standup", "--start",
                "2026-10-16T09:00:00Z", "--end", "2026-10-16T09:30:00Z", "--importance", "80",
                "--urgency", "10", "--notes", "daily"}) == kExitOk);

    // add: a hidden sleep block.
    assert(run({"add", "--db", dbPath, "--title", "Sleep", "--type", "sleep", "--start",
                "2026-10-16T23:00:00Z", "--end", "2026-10-17T07:00:00Z", "--hidden"}) == kExitOk);

    // add: missing required flags is a usage error, and writes nothing.
    assert(run({"add", "--db", dbPath, "--title", "NoTime"}) == kExitUsage);

    // Read back through core.
    auto db = klend_ario::Db::open(dbPath);
    assert(db.ok());
    auto all = db.value().listEvents(0, 4102444800LL); // 1970 .. 2100
    assert(all.ok());
    assert(all.value().size() == 2);

    const klend_ario::Event &first = all.value()[0];  // ordered by start
    const klend_ario::Event &second = all.value()[1];
    assert(first.title == "Standup");
    assert(first.notes == "daily");
    assert(first.importance == 80);
    assert(second.type == klend_ario::EventType::Sleep);
    assert(second.hidden);

    // show: present id succeeds, absent id is a runtime error.
    const std::string firstId = std::to_string(first.id);
    assert(run({"show", "--db", dbPath, firstId}) == kExitOk);
    assert(run({"show", "--db", dbPath, "999999"}) == kExitError);

    // rm is idempotent: removing a missing id still exits 0.
    assert(run({"rm", "--db", dbPath, firstId}) == kExitOk);
    assert(run({"rm", "--db", dbPath, firstId}) == kExitOk);

    // A usage error for a verb without its id.
    assert(run({"rm", "--db", dbPath}) == kExitUsage);

    // list --json over a range that contains the surviving event.
    assert(run({"list", "--db", dbPath, "--json", "--from", "2026-10-01T00:00:00Z", "--to",
                "2026-11-01T00:00:00Z"}) == kExitOk);
    assert(run({"list", "--db", dbPath, "--from", "2026-10-01T00:00:00Z", "--to",
                "2026-11-01T00:00:00Z"}) == kExitOk);

    // holidays: a civil-date query over core. It never opens a database, so
    // there is no --db here on purpose.
    assert(run({"holidays", "--year", "2026"}) == kExitOk);
    assert(run({"holidays", "--year", "2026", "--json"}) == kExitOk);
    // An unknown country is an error; an impossible year is a usage error.
    assert(run({"holidays", "--country", "XX", "--year", "2026"}) == kExitError);
    assert(run({"holidays", "--year", "0"}) == kExitUsage);
    assert(run({"holidays", "--year", "10000"}) == kExitUsage);

    // --help exits 0.
    assert(run({"--help"}) == kExitOk);

    std::remove(dbPath);

    // ---------------------------------------------------------------------
    // Stage 1-3: edit, duplicate, durations, relative time, output formats.
    // ---------------------------------------------------------------------
    const char *verbsDb = "klend_e2e_verbs.db";
    std::remove(verbsDb);

    // A positional title plus --duration; a missing --start means "now".
    assert(run({"add", "--db", verbsDb, "Focus", "--duration", "90m"}) == kExitOk);
    // Both bounds and both ways to specify the end are usage errors.
    assert(run({"add", "--db", verbsDb, "NoStart"}) == kExitUsage);
    assert(run({"add", "--db", verbsDb, "BothEnds", "--start", "today", "--end", "tomorrow",
                "--duration", "1h"}) == kExitUsage);

    // Two fixed events to edit.
    assert(run({"add", "--db", verbsDb, "--title", "Standup", "--start", "2026-10-16T09:00:00Z",
                "--end", "2026-10-16T09:30:00Z", "--importance", "80", "--urgency", "10",
                "--notes", "daily", "--alarm-offset", "-600"}) == kExitOk);
    assert(run({"add", "--db", verbsDb, "--title", "Sleep", "--type", "sleep", "--start",
                "2026-10-16T23:00:00Z", "--end", "2026-10-17T07:00:00Z", "--hidden"}) == kExitOk);

    long long standupId = 0;
    long long sleepId = 0;
    {
        auto vdb = klend_ario::Db::open(verbsDb);
        assert(vdb.ok());
        auto events = vdb.value().listEvents(0, 4102444800LL);
        assert(events.ok() && events.value().size() == 3);
        for (const klend_ario::Event &event : events.value()) {
            if (event.title == "Focus") {
                // The implicit start is now; only the duration is guaranteed.
                assert(event.endUtc - event.startUtc == 5400);
            } else if (event.title == "Standup") {
                standupId = event.id;
            } else if (event.title == "Sleep") {
                sleepId = event.id;
            }
        }
        assert(standupId > 0 && sleepId > 0);
    }

    const std::string standup = std::to_string(standupId);
    const std::string sleep = std::to_string(sleepId);

    // Partial edit: only the title changes, everything else survives.
    assert(run({"edit", "--db", verbsDb, standup, "--title", "Renamed"}) == kExitOk);
    {
        auto vdb = klend_ario::Db::open(verbsDb);
        assert(vdb.ok());
        auto event = vdb.value().getEvent(standupId);
        assert(event.ok());
        assert(event.value().title == "Renamed");
        assert(event.value().startUtc == *klend_ario::parseIso8601("2026-10-16T09:00:00Z"));
        assert(event.value().endUtc == *klend_ario::parseIso8601("2026-10-16T09:30:00Z"));
        assert(event.value().importance == 80);
        assert(event.value().urgency == 10);
        assert(event.value().alarmOffset == -600);
        assert(event.value().notes == "daily");
        assert(event.value().type == klend_ario::EventType::Event);
    }

    // Moving the start alone preserves the 30-minute duration.
    assert(run({"edit", "--db", verbsDb, standup, "--start", "2026-10-16T10:00:00Z"}) == kExitOk);
    {
        auto vdb = klend_ario::Db::open(verbsDb);
        assert(vdb.ok());
        auto event = vdb.value().getEvent(standupId);
        assert(event.ok());
        assert(event.value().startUtc == *klend_ario::parseIso8601("2026-10-16T10:00:00Z"));
        assert(event.value().endUtc == *klend_ario::parseIso8601("2026-10-16T10:30:00Z"));
    }

    // An explicit duration changes the length; a shift moves the whole block.
    assert(run({"edit", "--db", verbsDb, standup, "--duration", "45m"}) == kExitOk);
    assert(run({"edit", "--db", verbsDb, standup, "--shift", "1h"}) == kExitOk);
    {
        auto vdb = klend_ario::Db::open(verbsDb);
        assert(vdb.ok());
        auto event = vdb.value().getEvent(standupId);
        assert(event.ok());
        assert(event.value().startUtc == *klend_ario::parseIso8601("2026-10-16T11:00:00Z"));
        assert(event.value().endUtc == *klend_ario::parseIso8601("2026-10-16T11:45:00Z"));
    }

    // An end before the start and a stale id are both caught.
    assert(run({"edit", "--db", verbsDb, standup, "--end", "2026-10-16T10:00:00Z"}) == kExitUsage);
    assert(run({"edit", "--db", verbsDb, "999999", "--title", "Nope"}) == kExitError);
    assert(run({"edit", "--db", verbsDb, standup, "--shift", "1h", "--start", "today"}) ==
           kExitUsage);

    // Hidden is a toggle, and notes can be cleared without retyping them.
    assert(run({"edit", "--db", verbsDb, sleep, "--unhide"}) == kExitOk);
    assert(run({"edit", "--db", verbsDb, standup, "--clear-notes"}) == kExitOk);
    {
        auto vdb = klend_ario::Db::open(verbsDb);
        assert(vdb.ok());
        assert(!vdb.value().getEvent(sleepId).value().hidden);
        assert(vdb.value().getEvent(standupId).value().notes.empty());
    }

    // duplicate: same fields, a new id, and the duration preserved.
    assert(run({"duplicate", "--db", verbsDb, standup, "--shift", "-1h"}) == kExitOk);
    assert(run({"duplicate", "--db", verbsDb, standup, "--start", "2026-10-20T09:00:00Z"}) ==
           kExitOk);
    assert(run({"duplicate", "--db", verbsDb, standup, "--end", "2026-10-20T10:00:00Z"}) ==
           kExitUsage);
    {
        auto vdb = klend_ario::Db::open(verbsDb);
        assert(vdb.ok());
        auto events = vdb.value().listEvents(0, 4102444800LL);
        assert(events.ok());
        assert(events.value().size() == 5); // Focus, Standup, Sleep, and two copies
        int copies = 0;
        for (const klend_ario::Event &event : events.value()) {
            if (event.title != "Renamed" || event.id == standupId) {
                continue;
            }
            ++copies;
            assert(event.endUtc - event.startUtc == 45 * 60);
            assert(event.importance == 80);
            assert(event.urgency == 10);
            assert(event.alarmOffset == -600);
        }
        assert(copies == 2);
    }

    // Relative instants work through the real parse -> command path.
    assert(run({"list", "--db", verbsDb, "--from", "2026-10-01", "--to", "2026-11-01",
                "--format", "csv"}) == kExitOk);
    assert(run({"list", "--db", verbsDb, "--from", "today", "--to", "week+7d", "--format",
                "table"}) == kExitOk);

    // Output-format validation and per-verb help.
    assert(run({"list", "--db", verbsDb, "--format", "xml"}) == kExitUsage);
    assert(run({"help", "add"}) == kExitOk);
    assert(run({"help", "nope"}) == kExitUsage);
    assert(run({"edit", "--help"}) == kExitOk);

    std::remove(verbsDb);

    // ---------------------------------------------------------------------
    // Stage 4: query verbs, filters, sorting, pagination and hidden policy.
    // ---------------------------------------------------------------------
    const char *queryDb = "klend_e2e_query.db";
    std::remove(queryDb);

    // One ongoing event, one hidden event two hours out, and two past events.
    assert(run({"add", "--db", queryDb, "Now", "--duration", "1h", "--importance", "90",
                "--urgency", "80", "--notes", "alpha"}) == kExitOk);
    assert(run({"add", "--db", queryDb, "Hidden", "--start", "now+2h", "--duration", "1h",
                "--hidden", "--notes", "alpha"}) == kExitOk);
    assert(run({"add", "--db", queryDb, "Old", "--start", "2026-10-16T09:00:00Z", "--end",
                "2026-10-16T09:30:00Z", "--importance", "10", "--notes", "beta"}) == kExitOk);
    assert(run({"add", "--db", queryDb, "Dawn", "--type", "sleep", "--start",
                "2026-10-16T05:00:00Z", "--end", "2026-10-16T06:00:00Z", "--hidden"}) == kExitOk);

#ifndef _WIN32
    {
        // next = the ongoing visible event, not the hidden block later.
        const Capture next = runCaptured({"next", "--db", queryDb, "--format", "csv"});
        assert(next.code == kExitOk);
        assert(next.output.find("Now") != std::string::npos);
        assert(next.output.find("Hidden") == std::string::npos);
    }
    {
        // --hidden-only flips the visibility filter for the same query.
        const Capture hidden = runCaptured({"next", "--db", queryDb, "--hidden-only",
                                            "--format", "csv"});
        assert(hidden.code == kExitOk);
        assert(hidden.output.find("Hidden") != std::string::npos);
        assert(hidden.output.find("Now") == std::string::npos);
    }
    {
        // --limit widens next; with hidden included there are two rows.
        const Capture widened = runCaptured({"next", "--db", queryDb, "--limit", "2",
                                             "--include-hidden", "--format", "csv"});
        assert(widened.code == kExitOk);
        assert(countLines(widened.output) == 3); // header + Now + Hidden
    }
    {
        // search is unbounded and newest-first by default.
        const Capture search = runCaptured({"search", "alpha", "--db", queryDb, "--format",
                                            "csv"});
        assert(search.code == kExitOk);
        assert(search.output.find("Now") != std::string::npos);
        assert(search.output.find("Hidden") != std::string::npos);
        assert(search.output.find("Old") == std::string::npos);
        assert(search.output.find("Hidden") < search.output.find("Now"));
    }
    {
        const Capture visible = runCaptured({"search", "alpha", "--db", queryDb, "--visible",
                                            "--format", "csv"});
        assert(visible.code == kExitOk);
        assert(visible.output.find("Now") != std::string::npos);
        assert(visible.output.find("Hidden") == std::string::npos);
    }
    {
        const Capture hidden = runCaptured({"search", "alpha", "--db", queryDb, "--hidden-only",
                                            "--format", "csv"});
        assert(hidden.code == kExitOk);
        assert(hidden.output.find("Hidden") != std::string::npos);
        assert(hidden.output.find("Now") == std::string::npos);
    }
    {
        const Capture beta = runCaptured({"search", "beta", "--db", queryDb, "--format", "csv"});
        assert(beta.code == kExitOk);
        assert(beta.output.find("Old") != std::string::npos);
        assert(beta.output.find("Now") == std::string::npos);
    }
    {
        // Filters compose, and --search works on list too.
        const Capture typed = runCaptured({"search", "alpha", "--db", queryDb, "--type", "sleep",
                                           "--format", "csv"});
        assert(typed.code == kExitOk);
        assert(typed.output.find("Now") == std::string::npos);
        assert(typed.output.find("Hidden") == std::string::npos);

        const Capture important = runCaptured({"search", "alpha", "--db", queryDb,
                                               "--min-importance", "80", "--format", "csv"});
        assert(important.code == kExitOk);
        assert(important.output.find("Now") != std::string::npos);
        assert(important.output.find("Hidden") == std::string::npos);

        const Capture ranged = runCaptured({"list", "--db", queryDb, "--from", "2026-10-16",
                                            "--to", "2026-10-17", "--search", "beta", "--format",
                                            "csv"});
        assert(ranged.code == kExitOk);
        assert(ranged.output.find("Old") != std::string::npos);
        assert(ranged.output.find("Dawn") == std::string::npos);
    }
    {
        // Pagination after ordering: newest-first, so row 0 is Hidden.
        const Capture first = runCaptured({"search", "alpha", "--db", queryDb, "--limit", "1",
                                           "--format", "csv"});
        assert(first.code == kExitOk);
        assert(countLines(first.output) == 2);
        assert(first.output.find("Hidden") != std::string::npos);
        assert(first.output.find("Now") == std::string::npos);

        const Capture second = runCaptured({"search", "alpha", "--db", queryDb, "--offset", "1",
                                            "--limit", "1", "--format", "csv"});
        assert(second.code == kExitOk);
        assert(countLines(second.output) == 2);
        assert(second.output.find("Now") != std::string::npos);
        assert(second.output.find("Hidden") == std::string::npos);

        const Capture oldestFirst = runCaptured({"search", "alpha", "--db", queryDb, "--sort",
                                                 "start", "--format", "csv"});
        assert(oldestFirst.code == kExitOk);
        assert(oldestFirst.output.find("Now") < oldestFirst.output.find("Hidden"));
    }
#endif

    // The view verbs reach core. Their exact rows depend on the real clock, so
    // query_test pins the logic and these pin the wiring.
    assert(run({"today", "--db", queryDb}) == kExitOk);
    assert(run({"today", "--db", queryDb, "--include-hidden", "--format", "table"}) == kExitOk);
    assert(run({"week", "--db", queryDb, "--format", "json"}) == kExitOk);
    assert(run({"agenda", "--db", queryDb, "--days", "2"}) == kExitOk);
    assert(run({"list", "--db", queryDb, "--sort", "importance", "--limit", "2"}) == kExitOk);

    // Usage errors: bad names, ranges, and query-only flags on mutation verbs.
    assert(run({"agenda", "--db", queryDb, "--days", "0"}) == kExitUsage);
    assert(run({"agenda", "--db", queryDb, "--days", "2", "--to", "tomorrow"}) == kExitUsage);
    assert(run({"list", "--db", queryDb, "--days", "2"}) == kExitUsage);
    assert(run({"next", "--db", queryDb, "--sort", "updated"}) == kExitUsage);
    assert(run({"list", "--db", queryDb, "--sort", "banana"}) == kExitUsage);
    assert(run({"list", "--db", queryDb, "--type", "banana"}) == kExitUsage);
    assert(run({"list", "--db", queryDb, "--min-importance", "101"}) == kExitUsage);
    assert(run({"list", "--db", queryDb, "--min-importance", "90", "--max-importance", "10"}) ==
           kExitUsage);
    assert(run({"list", "--db", queryDb, "--search", ""}) == kExitUsage);
    assert(run({"search", "--db", queryDb}) == kExitUsage);
    assert(run({"list", "--db", queryDb, "--visible", "--hidden-only"}) == kExitUsage);
    assert(run({"list", "--db", queryDb, "--limit", "-1"}) == kExitUsage);
    assert(run({"add", "--db", queryDb, "--search", "x"}) == kExitUsage);

    std::remove(queryDb);

    // ---------------------------------------------------------------------
    // Stage 5a: conflicts, free slots, alarm triggers.
    // ---------------------------------------------------------------------
    const char *intelDb = "klend_e2e_intel.db";
    std::remove(intelDb);

    assert(run({"add", "--db", intelDb, "A", "--start", "2026-10-16T09:00:00Z", "--end",
                "2026-10-16T10:00:00Z"}) == kExitOk);
    assert(run({"add", "--db", intelDb, "B", "--start", "2026-10-16T09:30:00Z", "--end",
                "2026-10-16T10:30:00Z"}) == kExitOk);
    assert(run({"add", "--db", intelDb, "C", "--start", "2026-10-16T10:00:00Z", "--end",
                "2026-10-16T11:00:00Z"}) == kExitOk);
    // Hidden sleep overlaps A and B, but it is not part of the default view.
    assert(run({"add", "--db", intelDb, "Sleep", "--type", "sleep", "--start",
                "2026-10-16T09:15:00Z", "--end", "2026-10-16T09:45:00Z", "--hidden"}) ==
           kExitOk);
    // A reminder row is not a busy block.
    assert(run({"add", "--db", intelDb, "Reminder", "--type", "alarm", "--start",
                "2026-10-16T09:00:00Z", "--end", "2026-10-16T10:00:00Z"}) == kExitOk);
    // Alarm triggers: a visible event with T-10m and a hidden sleep block with T-5m.
    assert(run({"add", "--db", intelDb, "Timed", "--start", "2026-10-16T14:00:00Z", "--end",
                "2026-10-16T15:00:00Z", "--alarm-offset", "600"}) == kExitOk);
    assert(run({"add", "--db", intelDb, "Sleepy", "--type", "sleep", "--start",
                "2026-10-16T23:00:00Z", "--end", "2026-10-17T07:00:00Z", "--alarm-offset",
                "300", "--hidden"}) == kExitOk);

#ifndef _WIN32
    {
        // Visible conflicts: A/B and B/C. Sleep is hidden; Reminder is ignored.
        const Capture conflicts = runCaptured(
            {"conflicts", "--db", intelDb, "--from", "2026-10-16T00:00:00Z", "--to",
             "2026-10-17T00:00:00Z", "--format", "csv"});
        assert(conflicts.code == kExitOk);
        assert(countLines(conflicts.output) == 3); // header + A/B + B/C
        assert(conflicts.output.find("1,A,2,B,") != std::string::npos);
        assert(conflicts.output.find("2,B,3,C,") != std::string::npos);
        assert(conflicts.output.find("Sleep") == std::string::npos);
        assert(conflicts.output.find("Reminder") == std::string::npos);
    }
    {
        const Capture hidden = runCaptured(
            {"conflicts", "--db", intelDb, "--from", "2026-10-16T00:00:00Z", "--to",
             "2026-10-17T00:00:00Z", "--include-hidden", "--format", "csv"});
        assert(hidden.code == kExitOk);
        assert(hidden.output.find("Sleep") != std::string::npos);
    }
    {
        const Capture limited = runCaptured(
            {"conflicts", "--db", intelDb, "--from", "2026-10-16T00:00:00Z", "--to",
             "2026-10-17T00:00:00Z", "--limit", "1", "--format", "csv"});
        assert(limited.code == kExitOk);
        assert(countLines(limited.output) == 2); // header + one pair
    }
    {
        // Busy [09:00, 11:00); free before and after is one hour each.
        const Capture free = runCaptured(
            {"free", "--db", intelDb, "--from", "2026-10-16T08:00:00Z", "--to",
             "2026-10-16T12:00:00Z", "--min-duration", "30m", "--format", "csv"});
        assert(free.code == kExitOk);
        assert(countLines(free.output) == 3); // header + two slots
        assert(free.output.find("2026-10-16T08:00:00Z,2026-10-16T09:00:00Z,3600") !=
               std::string::npos);
        assert(free.output.find("2026-10-16T11:00:00Z,2026-10-16T12:00:00Z,3600") !=
               std::string::npos);
    }
    {
        const Capture none = runCaptured(
            {"free", "--db", intelDb, "--from", "2026-10-16T08:00:00Z", "--to",
             "2026-10-16T12:00:00Z", "--min-duration", "90m", "--format", "csv"});
        assert(none.code == kExitOk);
        assert(countLines(none.output) == 1); // header only
    }
    {
        // Hidden sleep blocks still alarm. Two triggers: T-5m and T=0.
        const Capture alarms = runCaptured(
            {"alarms", "--db", intelDb, "--from", "2026-10-16T22:00:00Z", "--to",
             "2026-10-17T00:00:00Z", "--type", "sleep", "--format", "csv"});
        assert(alarms.code == kExitOk);
        assert(countLines(alarms.output) == 3); // header + T-5m + T=0
        assert(alarms.output.find("Sleepy") != std::string::npos);
        assert(alarms.output.find("T-5m") != std::string::npos);
        assert(alarms.output.find("T=0") != std::string::npos);
    }
    {
        const Capture alarms = runCaptured(
            {"alarms", "--db", intelDb, "--from", "2026-10-16T13:00:00Z", "--to",
             "2026-10-16T15:00:00Z", "--type", "event", "--limit", "1", "--format", "csv"});
        assert(alarms.code == kExitOk);
        assert(countLines(alarms.output) == 2); // header + T-10m
        assert(alarms.output.find("Timed") != std::string::npos);
        assert(alarms.output.find("T-10m") != std::string::npos);
    }
#endif

    // Defaults and bad input.
    assert(run({"conflicts", "--db", intelDb}) == kExitOk);
    assert(run({"free", "--db", intelDb}) == kExitOk);
    assert(run({"alarms", "--db", intelDb}) == kExitOk);
    assert(run({"free", "--db", intelDb, "--min-duration", "0"}) == kExitUsage);
    assert(run({"free", "--db", intelDb, "--min-duration", "banana"}) == kExitUsage);
    assert(run({"conflicts", "--db", intelDb, "--sort", "start"}) == kExitUsage);
    assert(run({"conflicts", "--db", intelDb, "--offset", "1"}) == kExitUsage);
    assert(run({"alarms", "--db", intelDb, "--search", "x"}) == kExitUsage);
    assert(run({"conflicts", "--db", intelDb, "--type", "banana"}) == kExitUsage);
    assert(run({"list", "--db", intelDb, "--min-duration", "30m"}) == kExitUsage);

    // ---------------------------------------------------------------------
    // Stage 5b: stats and doctor.
    // ---------------------------------------------------------------------
#ifndef _WIN32
    // A previous aborted run may have left the fake conflict copy behind.
    std::remove("klend_e2e_intel.sync-conflict-20261016-101500-ABCDEF.db");
    {
        // All rows: 7 events, 2 hidden, 4 event/2 sleep/1 alarm, 4 conflict
        // pairs (A/B, B/C, A/Sleep, Sleep/B; the reminder row is ignored).
        const Capture stats = runCaptured({"stats", "--db", intelDb, "--format", "csv"});
        assert(stats.code == kExitOk);
        assert(stats.output.find("total,7\n") != std::string::npos);
        assert(stats.output.find("visible,5\n") != std::string::npos);
        assert(stats.output.find("hidden,2\n") != std::string::npos);
        assert(stats.output.find("event,4\n") != std::string::npos);
        assert(stats.output.find("sleep,2\n") != std::string::npos);
        assert(stats.output.find("wake,0\n") != std::string::npos);
        assert(stats.output.find("alarm,1\n") != std::string::npos);
        assert(stats.output.find("scheduled_seconds,48600\n") != std::string::npos);
        assert(stats.output.find("conflict_pairs,4\n") != std::string::npos);
        assert(stats.output.find("earliest,2026-10-16T09:00:00Z\n") != std::string::npos);
        assert(stats.output.find("latest,2026-10-17T07:00:00Z\n") != std::string::npos);
    }
    {
        const Capture visible = runCaptured(
            {"stats", "--db", intelDb, "--visible", "--format", "csv"});
        assert(visible.code == kExitOk);
        assert(visible.output.find("total,5\n") != std::string::npos);
        assert(visible.output.find("hidden,0\n") != std::string::npos);
        assert(visible.output.find("conflict_pairs,2\n") != std::string::npos);
    }
    {
        const Capture sleep = runCaptured(
            {"stats", "--db", intelDb, "--type", "sleep", "--format", "csv"});
        assert(sleep.code == kExitOk);
        assert(sleep.output.find("total,2\n") != std::string::npos);
        assert(sleep.output.find("event,0\n") != std::string::npos);
        assert(sleep.output.find("scheduled_seconds,30600\n") != std::string::npos);
    }
    {
        const Capture ranged = runCaptured(
            {"stats", "--db", intelDb, "--from", "2026-10-16", "--to", "2026-10-17",
             "--format", "json"});
        assert(ranged.code == kExitOk);
        assert(ranged.output.find("\"total\":7") != std::string::npos);
        assert(ranged.output.find("\"conflict_pairs\":4") != std::string::npos);
    }
    {
        const Capture doctor = runCaptured({"doctor", "--db", intelDb, "--format", "text"});
        assert(doctor.code == kExitOk);
        assert(doctor.output.find("[ok] integrity: ok") != std::string::npos);
        assert(doctor.output.find("healthy: yes") != std::string::npos);
    }
    {
        // A conflict copy is a real warning: doctor must surface it and exit 1.
        const char *conflict =
            "klend_e2e_intel.sync-conflict-20261016-101500-ABCDEF.db";
        {
            std::ofstream out(conflict);
            out << "not a database";
        }
        const Capture doctor = runCaptured({"doctor", "--db", intelDb, "--format", "json"});
        assert(doctor.code == kExitError);
        assert(doctor.output.find("\"healthy\":false") != std::string::npos);
        assert(doctor.output.find("\"name\":\"sync\"") != std::string::npos);
        assert(doctor.output.find("\"status\":\"warn\"") != std::string::npos);
        std::remove(conflict);
    }
    {
        // Once the copy is gone, the same database is healthy again.
        const Capture doctor = runCaptured({"doctor", "--db", intelDb, "--format", "text"});
        assert(doctor.code == kExitOk);
    }
#endif

    assert(run({"stats", "--db", intelDb, "--limit", "1"}) == kExitUsage);
    assert(run({"stats", "--db", intelDb, "--sort", "start"}) == kExitUsage);
    assert(run({"doctor", "--db", intelDb, "--from", "today"}) == kExitUsage);

    // ---------------------------------------------------------------------
    // Stage 6a: export and shell completion.
    // ---------------------------------------------------------------------
#ifndef _WIN32
    {
        const Capture csv = runCaptured({"export", "--db", intelDb, "--format", "csv"});
        assert(csv.code == kExitOk);
        assert(countLines(csv.output) == 8); // header + 7 events
        assert(csv.output.rfind("id,title,start,end,type,importance,urgency,alarm_offset,"
                                "hidden,notes,created_utc,updated_utc\n",
                                0) == 0);
        assert(csv.output.find("1,A,2026-10-16T09:00:00Z,2026-10-16T10:00:00Z,"
                               "event,50,50,0,false,") != std::string::npos);
    }
    {
        // Export is data access: hidden rows are included unless filtered.
        const Capture visible =
            runCaptured({"export", "--db", intelDb, "--visible", "--format", "csv"});
        assert(visible.code == kExitOk);
        assert(countLines(visible.output) == 6); // header + 5 visible rows
        assert(visible.output.find("Sleep") == std::string::npos);
    }
    {
        const Capture jsonl = runCaptured({"export", "--db", intelDb, "--json"});
        assert(jsonl.code == kExitOk);
        assert(countLines(jsonl.output) == 7);
        assert(jsonl.output.find("\"title\":\"A\"") != std::string::npos);
        assert(jsonl.output.find("\"hidden\":true") != std::string::npos);
    }
    {
        const Capture ics = runCaptured({"export", "--db", intelDb, "--format", "ics"});
        assert(ics.code == kExitOk);
        assert(ics.output.find("BEGIN:VCALENDAR\r\n") != std::string::npos);
        assert(ics.output.find("UID:klend-ario-7@klend-ario.invalid\r\n") !=
               std::string::npos);
        assert(ics.output.find("SUMMARY:Sleepy\r\n") != std::string::npos);
        assert(ics.output.find("X-KLEND-ARIO-HIDDEN:TRUE\r\n") != std::string::npos);
        assert(ics.output.find("TRIGGER:-PT5M\r\n") != std::string::npos);
        assert(ics.output.find("TRIGGER:-PT10M\r\n") != std::string::npos);
    }
    {
        const Capture ranged = runCaptured(
            {"export", "--db", intelDb, "--from", "2026-10-16T13:00:00Z", "--to",
             "2026-10-16T15:00:00Z", "--type", "event", "--format", "csv"});
        assert(ranged.code == kExitOk);
        assert(countLines(ranged.output) == 2); // header + Timed
        assert(ranged.output.find("6,Timed,") != std::string::npos);
        assert(ranged.output.find("1,A,") == std::string::npos);
    }
    {
        const Capture bash = runCaptured({"completion", "bash"});
        assert(bash.code == kExitOk);
        assert(bash.output.find("complete -F _klend_ario_cli klend_ario_cli\n") !=
               std::string::npos);
        assert(bash.output.find("export") != std::string::npos);
    }
#endif

    assert(run({"export", "--db", intelDb}) == kExitOk);
    assert(run({"export", "--db", intelDb, "--format", "xml"}) == kExitUsage);
    assert(run({"export", "--db", intelDb, "--limit", "1"}) == kExitUsage);
    assert(run({"completion"}) == kExitUsage);
    assert(run({"completion", "tcsh"}) == kExitUsage);
    assert(run({"completion", "bash", "zsh"}) == kExitUsage);

    // ---------------------------------------------------------------------
    // Stage 6b: import from csv/jsonl.
    // ---------------------------------------------------------------------
#ifndef _WIN32
    const char *importDb = "klend_e2e_import.db";
    const char *importJsonDb = "klend_e2e_import_json.db";
    const char *importIdsDb = "klend_e2e_import_ids.db";
    const char *replaceDb = "klend_e2e_replace.db";
    const char *dryDb = "klend_e2e_dry.db";
    const char *badDb = "klend_e2e_bad_import.db";
    const char *csvFile = "klend_e2e_import.csv";
    const char *jsonlFile = "klend_e2e_import.jsonl";
    const char *badFile = "klend_e2e_bad.csv";
    const char *emptyFile = "klend_e2e_empty.csv";
    std::remove(importDb);
    std::remove(importJsonDb);
    std::remove(importIdsDb);
    std::remove(replaceDb);
    std::remove(dryDb);
    std::remove(badDb);

    {
        const Capture csv = runCaptured({"export", "--db", intelDb, "--format", "csv"});
        assert(csv.code == kExitOk);
        std::ofstream out(csvFile, std::ios::binary);
        out << csv.output;

        const Capture jsonl = runCaptured({"export", "--db", intelDb, "--json"});
        assert(jsonl.code == kExitOk);
        std::ofstream outJson(jsonlFile, std::ios::binary);
        outJson << jsonl.output;
    }
    {
        const Capture imported = runCaptured({"import", "--db", importDb, csvFile});
        assert(imported.code == kExitOk);
        assert(imported.output == "imported 7 events\n");

        auto db = klend_ario::Db::open(importDb);
        assert(db.ok());
        auto all = db.value().queryEvents(klend_ario::EventQuery{});
        assert(all.ok());
        assert(all.value().size() == 7);
        assert(all.value()[0].title == "A");
        assert(all.value()[0].createdUtc != 0);
        bool sawHidden = false;
        for (const klend_ario::Event &event : all.value()) {
            if (event.title == "Sleepy") {
                sawHidden = event.hidden;
            }
        }
        assert(sawHidden);
    }
    {
        const Capture imported =
            runCaptured({"import", "--db", importJsonDb, "--format", "jsonl", jsonlFile});
        assert(imported.code == kExitOk);
        assert(imported.output == "imported 7 events\n");
        auto db = klend_ario::Db::open(importJsonDb);
        assert(db.ok());
        auto all = db.value().queryEvents(klend_ario::EventQuery{});
        assert(all.value().size() == 7);
    }
    {
        // Replace is a restore, not an append: seed a row, then import all 7.
        assert(run({"add", "--db", replaceDb, "Seed", "--start", "2026-10-16T00:00:00Z",
                    "--end", "2026-10-16T01:00:00Z"}) == kExitOk);
        const Capture replaced =
            runCaptured({"import", "--db", replaceDb, csvFile, "--replace"});
        assert(replaced.code == kExitOk);
        assert(replaced.output == "imported 7 events\n");
        auto db = klend_ario::Db::open(replaceDb);
        assert(db.ok());
        auto all = db.value().queryEvents(klend_ario::EventQuery{});
        assert(all.value().size() == 7);
        for (const klend_ario::Event &event : all.value()) {
            assert(event.title != "Seed");
        }
    }
    {
        // Preserve ids is explicit; the export already carries 1..7.
        const Capture imported =
            runCaptured({"import", "--db", importIdsDb, csvFile, "--preserve-ids"});
        assert(imported.code == kExitOk);
        auto db = klend_ario::Db::open(importIdsDb);
        assert(db.ok());
        auto all = db.value().queryEvents(klend_ario::EventQuery{});
        assert(all.value().size() == 7);
        std::vector<bool> seen(8, false);
        for (const klend_ario::Event &event : all.value()) {
            assert(event.id >= 1 && event.id <= 7);
            seen[static_cast<size_t>(event.id)] = true;
        }
        for (size_t id = 1; id <= 7; ++id) {
            assert(seen[id]);
        }
    }
    {
        // Dry run parses and reports, but does not create the database file.
        const Capture dry = runCaptured({"import", "--db", dryDb, csvFile, "--dry-run"});
        assert(dry.code == kExitOk);
        assert(dry.output == "parsed 7 events (dry run)\n");
        std::ifstream check(dryDb);
        assert(!check.good());
    }
    {
        {
            std::ofstream out(badFile);
            out << "title,start,end\nBad,not-a-date,2026-10-16T10:00:00Z\n";
        }
        const Capture bad = runCaptured({"import", "--db", badDb, badFile});
        assert(bad.code == kExitError);
        assert(bad.output.empty());
        std::ifstream check(badDb);
        assert(!check.good());

        std::ofstream empty(emptyFile);
        assert(run({"import", "--db", badDb, emptyFile}) == kExitError);
    }
    std::remove(importDb);
    std::remove(importJsonDb);
    std::remove(importIdsDb);
    std::remove(replaceDb);
    std::remove(dryDb);
    std::remove(badDb);
    std::remove(csvFile);
    std::remove(jsonlFile);
    std::remove(badFile);
    std::remove(emptyFile);
#endif

    assert(run({"import"}) == kExitUsage);
    assert(run({"import", "no_such_file.csv"}) == kExitError);
    assert(run({"import", "no_such_file.csv", "--format", "ics"}) == kExitUsage);
    assert(run({"list", "--replace"}) == kExitUsage);
    assert(run({"help", "update"}) == kExitOk);
    assert(run({"help", "copy"}) == kExitOk);
    assert(run({"help", "remove"}) == kExitOk);

#ifndef _WIN32
    {
        const Capture version = runCaptured({"--version"});
        assert(version.code == kExitOk);
        assert(version.output.rfind("klend_ario_cli ", 0) == 0);
        assert(version.output.back() == '\n');
    }
#endif

    // Options are scoped to their verbs, not silently ignored.
    assert(run({"export", "--db", intelDb, "--title", "Z"}) == kExitUsage);
    assert(run({"completion", "bash", "--title", "X"}) == kExitUsage);
    assert(run({"completion", "bash", "--format", "xml"}) == kExitUsage);
    assert(run({"holidays", "--hidden"}) == kExitUsage);
    assert(run({"list", "--db", intelDb, "--days", "3"}) == kExitUsage);
    assert(run({"export", "--db", intelDb, "--no-header"}) == kExitUsage);

#ifndef _WIN32
    // `doctor` and `export` are read-only: a missing file is an error, not a
    // freshly initialised calendar.
    const char *missingDoctor = "klend_e2e_missing_doctor.db";
    const char *missingExport = "klend_e2e_missing_export.db";
    const char *missingView = "klend_e2e_missing_view.db";
    const char *emptyDoctor = "klend_e2e_empty_doctor.db";
    std::remove(missingDoctor);
    std::remove(missingExport);
    std::remove(missingView);
    std::remove(emptyDoctor);
    {
        const Capture doctor = runCaptured({"doctor", "--db", missingDoctor});
        assert(doctor.code == kExitError);
        std::ifstream check(missingDoctor);
        assert(!check.good());

        const Capture exported =
            runCaptured({"export", "--db", missingExport, "--format", "csv"});
        assert(exported.code == kExitError);
        std::ifstream checkExport(missingExport);
        assert(!checkExport.good());

        // Explicit --db on a view follows the same rule.
        const Capture listed = runCaptured({"list", "--db", missingView});
        assert(listed.code == kExitError);
        std::ifstream checkView(missingView);
        assert(!checkView.good());
    }
    {
        {
            std::ofstream empty(emptyDoctor);
            assert(empty.good());
        }
        const Capture doctor = runCaptured({"doctor", "--db", emptyDoctor});
        assert(doctor.code == kExitError);
        assert(doctor.output.find("[fail] schema:") != std::string::npos);
        std::ifstream check(emptyDoctor, std::ios::binary | std::ios::ate);
        assert(check.good());
        assert(check.tellg() == 0);
    }
    {
        // A read-only file must be readable by both commands, and doctor must
        // not need write access to report.
        assert(chmod(intelDb, 0444) == 0);
        const Capture exported = runCaptured({"export", "--db", intelDb, "--format", "csv"});
        assert(exported.code == kExitOk);
        assert(exported.output.find("1,A,") != std::string::npos);

        const Capture doctor = runCaptured({"doctor", "--db", intelDb});
        assert(doctor.code == kExitOk);
        assert(doctor.output.find("healthy: yes") != std::string::npos);
        assert(chmod(intelDb, 0644) == 0);
    }
    std::remove(missingDoctor);
    std::remove(missingExport);
    std::remove(missingView);
    std::remove(emptyDoctor);
#endif

    std::remove(intelDb);

    // ---------------------------------------------------------------------
    // An encrypted database, through the CLI, via --key-file.
    // ---------------------------------------------------------------------
    const char *cryptoDb = "klend_e2e_crypto.db";
    const char *keyFile = "klend_e2e_key.txt";
    std::remove(cryptoDb);
    {
        std::ofstream out(keyFile);
        out << "s3cret\n";
    }
    assert(run({"add", "--db", cryptoDb, "--key-file", keyFile, "--title", "Locked", "--start",
                "2026-10-16T09:00:00Z", "--end", "2026-10-16T10:00:00Z"}) == kExitOk);
    // Reading it back with the key works...
    assert(run({"list", "--db", cryptoDb, "--key-file", keyFile, "--from",
                "2026-10-16T00:00:00Z", "--to", "2026-10-17T00:00:00Z"}) == kExitOk);
    // ...and without a key it fails, because the file is genuinely encrypted.
    assert(run({"list", "--db", cryptoDb, "--from", "2026-10-16T00:00:00Z", "--to",
                "2026-10-17T00:00:00Z"}) == kExitError);
    // A key file that does not exist is a usage error.
    assert(run({"list", "--db", cryptoDb, "--key-file", "no_such_key.txt"}) == kExitUsage);
    std::remove(cryptoDb);
    std::remove(keyFile);

    std::printf("e2e_test: all assertions passed\n");
    return 0;
}
