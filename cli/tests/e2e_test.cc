#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "commands.hh"

#include "klend_ario/db.hh"

using namespace klend_ario::cli;

namespace {

int run(std::vector<std::string_view> tokens) { return runCommand(parseArgs(tokens)); }

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
