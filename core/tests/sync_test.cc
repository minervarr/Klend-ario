#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "klend_ario/sync.hh"

using namespace klend_ario;

static void testMatchesOnlyConflictCopies() {
    const std::string db = "/home/nava/.local/state/klend_ario/klend_ario.db";
    // Syncthing inserts ".sync-conflict-<date>-<time>-<device>" BEFORE the file
    // extension, so a conflict copy of klend_ario.db looks like this.
    const std::vector<std::string> listing = {
        "klend_ario.db",
        "klend_ario.db-wal",
        "klend_ario.sync-conflict-20261016-101500-ABCDEF.db",
        "klend_ario.sync-conflict-20261016-101500-GHIJKL.db",
        // A conflict copy of a DIFFERENT database must not match.
        "unrelated.sync-conflict-20261016-101500-ABCDEF.db",
    };
    const std::vector<std::string> found = conflictCopiesFrom(db, listing);
    assert(found.size() == 2);
    assert(found[0] ==
           "/home/nava/.local/state/klend_ario/klend_ario.sync-conflict-20261016-101500-ABCDEF.db");
    assert(found[1] ==
           "/home/nava/.local/state/klend_ario/klend_ario.sync-conflict-20261016-101500-GHIJKL.db");
}

static void testNoneWhenClean() {
    const std::string db = "/data/klend_ario.db";
    assert(conflictCopiesFrom(db, {"klend_ario.db", "other.db", "klend_ario.db-journal"}).empty());
}

static void testTheDbItselfIsNotAConflictCopy() {
    // The exact file name is shorter than the marker, so it never matches —
    // this is the off-by-one that would report the real database.
    const std::string db = "/data/klend_ario.db";
    assert(conflictCopiesFrom(db, {"klend_ario.db"}).empty());
}

static void testExtensionlessName() {
    const std::string db = "/data/calendar";
    const std::vector<std::string> found =
        conflictCopiesFrom(db, {"calendar.sync-conflict-x", "calendar", "calendars"});
    assert(found.size() == 1);
    assert(found[0] == "/data/calendar.sync-conflict-x");
}

int main() {
    testMatchesOnlyConflictCopies();
    testNoneWhenClean();
    testTheDbItselfIsNotAConflictCopy();
    testExtensionlessName();
    std::printf("sync_test: all assertions passed\n");
    return 0;
}
