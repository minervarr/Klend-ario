#include "klend_ario/sync.hh"

#include <cstddef>

#include "arc/fs/paths.hh"
#include "arc/fs/walk.hh" // listDir / Entry

namespace klend_ario {

std::vector<std::string> conflictCopiesFrom(std::string_view dbPath,
                                            const std::vector<std::string> &names) {
    const std::string base = arc::fs::filename(dbPath);
    const std::size_t dot = base.rfind('.');
    // The stem is everything before the LAST dot; a name with no dot is its own
    // stem ("calendar" -> "calendar.sync-conflict-...").
    const std::string stem = dot == std::string::npos ? base : base.substr(0, dot);
    const std::string marker = stem + ".sync-conflict-";
    const std::string parent = arc::fs::parent(dbPath);

    std::vector<std::string> found;
    for (const std::string &name : names) {
        // Longer than the marker, so an exact-name match (the db itself) is not
        // reported and "klend_ario.db-wal" does not count.
        if (name.size() > marker.size() && name.compare(0, marker.size(), marker) == 0) {
            found.push_back(arc::fs::join(parent, name));
        }
    }
    return found;
}

std::vector<std::string> syncConflictCopies(const std::string &dbPath) {
    std::string parent = arc::fs::parent(dbPath);
    // A bare filename ("klend_ario.db") has no parent component. Listing ""
    // fails, which silently hid conflict copies for every relative --db path.
    if (parent.empty()) {
        parent = ".";
    }
    auto entries = arc::fs::listDir(parent);
    if (!entries) {
        return {};
    }
    std::vector<std::string> names;
    names.reserve(entries.value().size());
    for (const arc::fs::Entry &entry : entries.value()) {
        names.push_back(arc::fs::filename(entry.path));
    }
    return conflictCopiesFrom(dbPath, names);
}

} // namespace klend_ario
