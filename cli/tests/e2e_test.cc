#undef NDEBUG
#include <cassert>
#include <cstdio>
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

    // --help exits 0.
    assert(run({"--help"}) == kExitOk);

    std::remove(dbPath);
    std::printf("e2e_test: all assertions passed\n");
    return 0;
}
