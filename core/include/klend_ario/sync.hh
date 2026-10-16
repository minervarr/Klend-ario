#pragma once

#include <string>
#include <string_view>
#include <vector>

// Syncthing syncs ONE file. When two devices edit it between syncs, Syncthing
// does not merge — it keeps one copy and writes the other beside it as
//   <stem>.sync-conflict-<YYYYMMDD>-<HHMMSS>-<DEVICE><ext>
// i.e. the marker is inserted BEFORE the file extension, so a conflict copy of
// `klend_ario.db` is `klend_ario.sync-conflict-...-ID.db`.
//
// The policy (essentialAgent/MEMORY.md §7) is to DETECT and report those, never
// to merge them: a calendar is low-write enough that a human should look at a
// conflict rather than have one side silently win.

namespace klend_ario {

// The conflict copies for `dbPath` found among `names` (bare filenames from a
// directory listing), returned as full paths. PURE — path arithmetic only — so
// it is asserted exactly, with no fixture.
std::vector<std::string> conflictCopiesFrom(std::string_view dbPath,
                                            const std::vector<std::string> &names);

// The conflict copies actually present beside `dbPath`. Uses
// arc::fs::listDir, which cannot throw; an unreadable directory yields none
// rather than taking the process down.
std::vector<std::string> syncConflictCopies(const std::string &dbPath);

} // namespace klend_ario
