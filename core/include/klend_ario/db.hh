#pragma once

#include <string>
#include <vector>

#include "arc/result.hh"
#include "klend_ario/event.hh"

// sqlite3 is a C API; forward-declare it so this header stays free of the
// sqlite3 include and callers never see the C surface. The definition is
// private to db.cc.
struct sqlite3;

namespace klend_ario {

// A single connection to the calendar database.
//
// RAII and move-only: opening returns the handle (or an error), closing is the
// destructor's job. Every fallible operation returns arc::Result<T> — no
// exception ever crosses this boundary (essentialAgent/AGENTS.md §2.5).
//
// The schema is brought up to date during open(), so a successfully-opened Db
// is always usable.
class Db {
public:
    Db() = default;
    ~Db();
    Db(Db &&other) noexcept;
    Db &operator=(Db &&other) noexcept;
    Db(const Db &) = delete;
    Db &operator=(const Db &) = delete;

    // Opens a file (creating it and its schema if new) or ":memory:". The
    // caller owns WHERE the file lives: core never invents a path — the CLI
    // supplies arc::fs::layout("klend_ario").state (essentialAgent/AGENTS.md §2.3).
    static arc::Result<Db> open(const std::string &path);
    static arc::Result<Db> openMemory();

    // Inserts `e`, ignoring its id, and returns the rowid it was given. Fills
    // created_utc/updated_utc with utcNowSeconds() when they are 0.
    arc::Result<long long> insertEvent(Event e);

    // The event with `id`, or an error when there is none.
    arc::Result<Event> getEvent(long long id);

    // Events overlapping [fromUtc, toUtc), ordered by start. A half-open range,
    // so adjacent queries do not double-count a boundary event.
    arc::Result<std::vector<Event>> listEvents(long long fromUtc, long long toUtc);

    // Deletes `id`. Succeeds even when nothing matched.
    arc::Result<void> removeEvent(long long id);

private:
    explicit Db(sqlite3 *handle) : db_(handle) {}
    arc::Result<void> migrate();

    sqlite3 *db_ = nullptr;
};

} // namespace klend_ario
