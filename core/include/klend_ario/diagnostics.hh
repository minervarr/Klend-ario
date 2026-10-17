#pragma once

#include <string>
#include <vector>

#include "klend_ario/db.hh"

// A read-only health report for the database, as a value.
//
// `diagnose` is deliberately side-effect free: it never migrates, fixes, or
// deletes. A conflict copy is surfaced for a human to resolve (MEMORY.md §7),
// and a missing `modified_utc` matters because that marker is how the Syncthing
// policy says which side changed last.
//
// The CLI prints the checks and exits 1 when anything is warn/fail. Keeping the
// decision here means the MCP server or a future GUI can make the same call.

namespace klend_ario {

enum class CheckStatus { Ok, Warn, Fail };

const char *checkStatusName(CheckStatus status);

struct DiagnosticCheck {
    std::string name;
    CheckStatus status = CheckStatus::Ok;
    std::string detail;
};

struct Diagnostics {
    std::vector<DiagnosticCheck> checks;

    // Ok only: a warning is still something a human should see.
    bool healthy() const;
};

// `dbPath` is used only to look for Syncthing conflict copies beside the file;
// pass ":memory:" (or an empty path) for an in-memory database.
Diagnostics diagnose(Db &db, const std::string &dbPath);

} // namespace klend_ario
