#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef _WIN32
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
