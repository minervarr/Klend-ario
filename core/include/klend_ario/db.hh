#pragma once

#include <string>
#include <vector>

#include "arc/result.hh"
#include "klend_ario/event.hh"
#include "klend_ario/query.hh"

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

    // Opens an encrypted database with `passphrase` (Phase 3). ChaCha20-
    // Poly1305 via SQLite3MultipleCiphers; the key is derived by the library.
    // A wrong passphrase fails here rather than on a later read. Passphrases
    // never touch the schema — the file carries its own salt.
    static arc::Result<Db> openEncrypted(const std::string &path, const std::string &passphrase);

    static arc::Result<Db> openMemory();

    // Inserts `e`, ignoring its id, and returns the rowid it was given. Fills
    // created_utc/updated_utc with utcNowSeconds() when they are 0.
    arc::Result<long long> insertEvent(Event e);

    // Updates the event named by `e.id`, bumping updated_utc to now and leaving
    // created_utc as stored. Fails when no row has that id, so a stale edit is
    // reported rather than silently lost.
    arc::Result<void> updateEvent(const Event &e);

    // The event with `id`, or an error when there is none.
    arc::Result<Event> getEvent(long long id);

    // Events overlapping [fromUtc, toUtc), ordered by start. A half-open range,
    // so adjacent queries do not double-count a boundary event. A convenience
    // wrapper over queryEvents, so both share one SQL implementation.
    arc::Result<std::vector<Event>> listEvents(long long fromUtc, long long toUtc);

    // Filtered, ordered, paginated query. Text is matched literally and
    // case-insensitively (ASCII) against title and notes; `%` and `_` are not
    // wildcards. The range is half-open, as above.
    arc::Result<std::vector<Event>> queryEvents(const EventQuery &query);

    // Deletes `id`. Succeeds even when nothing matched.
    arc::Result<void> removeEvent(long long id);

    // ---------------------------------------------------------------------
    // `meta` — small key/value settings (the schema version, and later the
    // user's prefs: sleep window, week start, colour anchors). The schema
    // version is the one key core itself owns.
    // ---------------------------------------------------------------------
    arc::Result<std::string> getMeta(const std::string &key); // "" when absent
    arc::Result<void> setMeta(const std::string &key, const std::string &value);

private:
    explicit Db(sqlite3 *handle) : db_(handle) {}
    static arc::Result<Db> openInternal(const std::string &path, const std::string *passphrase);
    arc::Result<void> migrate();
    arc::Result<void> touchModified();

    sqlite3 *db_ = nullptr;
};

} // namespace klend_ario
