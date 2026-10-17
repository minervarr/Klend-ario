#include "klend_ario/db.hh"

#include "klend_ario/schema.hh"

#include "sqlite3mc_amalgamation.h" // SQLite + SQLite3MultipleCiphers, one header

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>

namespace klend_ario {
namespace {

// ---------------------------------------------------------------------------
// Small RAII helpers over the C API. A prepared statement finalises itself;
// a bind failure is remembered and surfaced by step() instead of being checked
// at every call site (sqlite3_bind_* only fails on misuse or OOM, both of which
// the caller can then see in one place).
// ---------------------------------------------------------------------------

arc::Error dbError(sqlite3 *db, const char *what) {
    return arc::Error{arc::ErrorKind::Other, db != nullptr ? sqlite3_errcode(db) : 0,
                      std::string(what) + ": " +
                          (db != nullptr ? sqlite3_errmsg(db) : "no database")};
}

arc::Result<void> exec(sqlite3 *db, const char *sql) {
    char *message = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &message);
    if (rc != SQLITE_OK) {
        std::string text = message != nullptr ? message : "sqlite error";
        sqlite3_free(message);
        return arc::Error{arc::ErrorKind::Other, rc, std::move(text)};
    }
    return {};
}

class Statement {
public:
    Statement() = default;
    ~Statement() {
        if (stmt_ != nullptr) {
            sqlite3_finalize(stmt_);
        }
    }
    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;

    arc::Result<void> prepare(sqlite3 *db, const char *sql) {
        db_ = db;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
            return dbError(db, "prepare");
        }
        return {};
    }

    void bindInt(int index, long long value) {
        note(sqlite3_bind_int64(stmt_, index, value), index);
    }
    void bindBool(int index, bool value) {
        note(sqlite3_bind_int(stmt_, index, value ? 1 : 0), index);
    }
    void bindText(int index, const std::string &value) {
        note(sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT), index);
    }

    // true = a row is available; false = done.
    arc::Result<bool> step() {
        if (bindFailed_) {
            return arc::Error{arc::ErrorKind::Other, bindCode_, bindMessage_};
        }
        const int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW) {
            return true;
        }
        if (rc == SQLITE_DONE) {
            return false;
        }
        return dbError(db_, "step");
    }

    sqlite3_stmt *get() const { return stmt_; }

private:
    void note(int rc, int index) {
        if (rc != SQLITE_OK && !bindFailed_) {
            bindFailed_ = true;
            bindCode_ = rc;
            bindMessage_ = "bind parameter " + std::to_string(index) + ": " +
                           (db_ != nullptr ? sqlite3_errmsg(db_) : "sqlite error");
        }
    }

    sqlite3 *db_ = nullptr;
    sqlite3_stmt *stmt_ = nullptr;
    bool bindFailed_ = false;
    int bindCode_ = 0;
    std::string bindMessage_;
};

const char *kColumns =
    "id, title, start_utc, end_utc, type, importance, urgency, alarm_offset, "
    "hidden, notes, created_utc, updated_utc";

Event readEvent(sqlite3_stmt *stmt) {
    Event e;
    e.id = sqlite3_column_int64(stmt, 0);
    const auto *title = sqlite3_column_text(stmt, 1);
    e.title = title != nullptr ? reinterpret_cast<const char *>(title) : "";
    e.startUtc = sqlite3_column_int64(stmt, 2);
    e.endUtc = sqlite3_column_int64(stmt, 3);
    const auto *type = sqlite3_column_text(stmt, 4);
    eventTypeFromName(type != nullptr ? reinterpret_cast<const char *>(type) : "", e.type);
    e.importance = sqlite3_column_int(stmt, 5);
    e.urgency = sqlite3_column_int(stmt, 6);
    e.alarmOffset = sqlite3_column_int(stmt, 7);
    e.hidden = sqlite3_column_int(stmt, 8) != 0;
    const auto *notes = sqlite3_column_text(stmt, 9);
    e.notes = notes != nullptr ? reinterpret_cast<const char *>(notes) : "";
    e.createdUtc = sqlite3_column_int64(stmt, 10);
    e.updatedUtc = sqlite3_column_int64(stmt, 11);
    return e;
}

// One row insert, shared by `insertEvent` and `importEvents`. `preserveId` is
// explicit: a normal insert must let SQLite assign the id, while a restore keeps
// the id that notes/links may refer to. Timestamps are filled here so both
// callers agree on the "0 means now" convention.
arc::Result<long long> insertRow(sqlite3 *db, Event e, long long now, bool preserveId) {
    if (preserveId && e.id <= 0) {
        return arc::Error{arc::ErrorKind::Other, 0,
                          "preserve ids requires a positive id on every event"};
    }
    if (e.createdUtc == 0) {
        e.createdUtc = now;
    }
    if (e.updatedUtc == 0) {
        e.updatedUtc = now;
    }

    static const char *kInsert =
        "INSERT INTO events(title, start_utc, end_utc, type, importance, urgency, "
        "alarm_offset, hidden, notes, created_utc, updated_utc) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    static const char *kInsertWithId =
        "INSERT INTO events(id, title, start_utc, end_utc, type, importance, urgency, "
        "alarm_offset, hidden, notes, created_utc, updated_utc) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

    Statement stmt;
    if (auto prepared = stmt.prepare(db, preserveId ? kInsertWithId : kInsert); !prepared) {
        return prepared.error();
    }
    int index = 1;
    if (preserveId) {
        stmt.bindInt(index++, e.id);
    }
    stmt.bindText(index++, e.title);
    stmt.bindInt(index++, e.startUtc);
    stmt.bindInt(index++, e.endUtc);
    stmt.bindText(index++, eventTypeName(e.type));
    stmt.bindInt(index++, e.importance);
    stmt.bindInt(index++, e.urgency);
    stmt.bindInt(index++, e.alarmOffset);
    stmt.bindBool(index++, e.hidden);
    stmt.bindText(index++, e.notes);
    stmt.bindInt(index++, e.createdUtc);
    stmt.bindInt(index, e.updatedUtc);
    if (auto done = stmt.step(); !done) {
        return done.error();
    }
    return preserveId ? e.id : sqlite3_last_insert_rowid(db);
}

} // namespace

Db::~Db() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
    }
}

Db::Db(Db &&other) noexcept : db_(other.db_) { other.db_ = nullptr; }

Db &Db::operator=(Db &&other) noexcept {
    if (this != &other) {
        if (db_ != nullptr) {
            sqlite3_close(db_);
        }
        db_ = other.db_;
        other.db_ = nullptr;
    }
    return *this;
}

arc::Result<Db> Db::open(const std::string &path) {
    return openInternal(path, nullptr, false);
}

arc::Result<Db> Db::openEncrypted(const std::string &path, const std::string &passphrase) {
    if (passphrase.empty()) {
        // Refuse rather than fall back to plaintext: an empty passphrase is a
        // caller bug, not a request for "no encryption".
        return arc::Error{arc::ErrorKind::Other, 0,
                          "openEncrypted needs a non-empty passphrase"};
    }
    return openInternal(path, &passphrase, false);
}

arc::Result<Db> Db::openReadOnly(const std::string &path) {
    return openInternal(path, nullptr, true);
}

arc::Result<Db> Db::openEncryptedReadOnly(const std::string &path,
                                          const std::string &passphrase) {
    if (passphrase.empty()) {
        return arc::Error{arc::ErrorKind::Other, 0,
                          "openEncryptedReadOnly needs a non-empty passphrase"};
    }
    return openInternal(path, &passphrase, true);
}

arc::Result<Db> Db::openInternal(const std::string &path, const std::string *passphrase,
                                 bool readOnly) {
    sqlite3 *handle = nullptr;
    const int flags =
        readOnly ? SQLITE_OPEN_READONLY : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    const int rc = sqlite3_open_v2(path.c_str(), &handle, flags, nullptr);
    if (rc != SQLITE_OK) {
        arc::Error error = dbError(handle, "open");
        if (handle != nullptr) {
            sqlite3_close(handle);
        }
        return error;
    }

    if (passphrase != nullptr) {
        // Pin the on-disk format BEFORE the key. ChaCha20 is also the library's
        // own default, but stating it makes the format an explicit choice we
        // can keep across SQLite3MultipleCiphers upgrades.
        if (auto cipher = exec(handle, "PRAGMA cipher = 'chacha20';"); !cipher) {
            sqlite3_close(handle);
            return cipher.error();
        }
        if (sqlite3_key(handle, passphrase->data(), static_cast<int>(passphrase->size())) !=
            SQLITE_OK) {
            arc::Error error = dbError(handle, "key");
            sqlite3_close(handle);
            return error;
        }
    }

    if (readOnly) {
        // Belt and braces: SQLITE_OPEN_READONLY already refuses writes, and
        // query_only makes an accidental write from a later refactor fail
        // instead of quietly succeeding on a writable file.
        if (auto guarded = exec(handle, "PRAGMA query_only = ON;"); !guarded) {
            sqlite3_close(handle);
            return guarded.error();
        }
        // Touch the schema header so an encrypted file without a key (or a
        // corrupt one) fails HERE, not as a confusing query error later. An
        // empty file still passes: diagnostics reports the missing schema.
        if (auto readable = exec(handle, "PRAGMA schema_version;"); !readable) {
            if (passphrase != nullptr && readable.error().code == SQLITE_NOTADB) {
                sqlite3_close(handle);
                return arc::Error{arc::ErrorKind::Other, SQLITE_NOTADB,
                                  "cannot read the database with that passphrase "
                                  "(wrong key, or not a Klend-ario database)"};
            }
            sqlite3_close(handle);
            return readable.error();
        }
        return Db(handle);
    }

    Db db(handle);
    if (auto migrated = db.migrate(); !migrated) {
        // A wrong passphrase surfaces here: the first read of an encrypted file
        // fails with SQLITE_NOTADB. Say so plainly rather than echoing
        // "file is not a database".
        if (passphrase != nullptr && migrated.error().code == SQLITE_NOTADB) {
            return arc::Error{arc::ErrorKind::Other, SQLITE_NOTADB,
                              "cannot read the database with that passphrase "
                              "(wrong key, or not a Klend-ario database)"};
        }
        return migrated.error();
    }
    return db;
}

arc::Result<Db> Db::openMemory() { return open(":memory:"); }

arc::Result<void> Db::migrate() {
    if (auto applied = exec(db_, schemaSql()); !applied) {
        return applied.error();
    }

    int current = 0;
    auto stored = getMeta("schema_version");
    if (!stored) {
        return stored.error();
    }
    if (!stored.value().empty()) {
        current = std::atoi(stored.value().c_str());
    }

    // A database written by a NEWER build is refused rather than opened and
    // quietly misread: forward compatibility is not ours to guess.
    if (current > kSchemaVersion) {
        return arc::Error{arc::ErrorKind::Other, 0,
                          "database schema v" + std::to_string(current) +
                              " is newer than this build (v" + std::to_string(kSchemaVersion) +
                              ")"};
    }

    // Additive steps run here, in order, as versions are introduced:
    //   for (int v = current + 1; v <= kSchemaVersion; ++v) applyStep(v);
    // There are none yet — v1 is the initial schema, and schema.sql is
    // idempotent, so a fresh or older database is brought to v1 by the CREATE
    // statements already above.
    return setMeta("schema_version", std::to_string(kSchemaVersion));
}

arc::Result<long long> Db::insertEvent(Event e) {
    auto inserted = insertRow(db_, e, utcNowSeconds(), false);
    if (!inserted) {
        return inserted.error();
    }
    if (auto touched = touchModified(); !touched) {
        return touched.error();
    }
    return inserted.value();
}

arc::Result<long long> Db::importEvents(const std::vector<Event> &events, bool replace,
                                        bool preserveIds) {
    if (auto begun = exec(db_, "BEGIN IMMEDIATE"); !begun) {
        return begun.error();
    }
    const auto rollback = [this]() { (void)exec(db_, "ROLLBACK"); };

    if (replace) {
        if (auto deleted = exec(db_, "DELETE FROM events"); !deleted) {
            rollback();
            return deleted.error();
        }
    }

    const long long now = utcNowSeconds();
    for (const Event &event : events) {
        if (auto inserted = insertRow(db_, event, now, preserveIds); !inserted) {
            rollback();
            return inserted.error();
        }
    }
    if (auto touched = touchModified(); !touched) {
        rollback();
        return touched.error();
    }
    if (auto committed = exec(db_, "COMMIT"); !committed) {
        rollback();
        return committed.error();
    }
    return static_cast<long long>(events.size());
}

arc::Result<void> Db::updateEvent(const Event &e) {
    static const char *kUpdate =
        "UPDATE events SET title=?, start_utc=?, end_utc=?, type=?, importance=?, "
        "urgency=?, alarm_offset=?, hidden=?, notes=?, updated_utc=? WHERE id=?";
    Statement stmt;
    if (auto prepared = stmt.prepare(db_, kUpdate); !prepared) {
        return prepared.error();
    }
    stmt.bindText(1, e.title);
    stmt.bindInt(2, e.startUtc);
    stmt.bindInt(3, e.endUtc);
    stmt.bindText(4, eventTypeName(e.type));
    stmt.bindInt(5, e.importance);
    stmt.bindInt(6, e.urgency);
    stmt.bindInt(7, e.alarmOffset);
    stmt.bindBool(8, e.hidden);
    stmt.bindText(9, e.notes);
    stmt.bindInt(10, utcNowSeconds()); // updated_utc, always refreshed
    stmt.bindInt(11, e.id);
    if (auto done = stmt.step(); !done) {
        return done.error();
    }
    // created_utc is deliberately not touched: an edit did not create anything.
    if (sqlite3_changes(db_) == 0) {
        return arc::Error{arc::ErrorKind::Other, 0, "no event with id " + std::to_string(e.id)};
    }
    return touchModified();
}

arc::Result<Event> Db::getEvent(long long id) {
    const std::string sql = std::string("SELECT ") + kColumns + " FROM events WHERE id = ?";
    Statement stmt;
    if (auto prepared = stmt.prepare(db_, sql.c_str()); !prepared) {
        return prepared.error();
    }
    stmt.bindInt(1, id);
    auto row = stmt.step();
    if (!row) {
        return row.error();
    }
    if (!row.value()) {
        return arc::Error{arc::ErrorKind::Other, 0, "no event with id " + std::to_string(id)};
    }
    return readEvent(stmt.get());
}

arc::Result<std::vector<Event>> Db::listEvents(long long fromUtc, long long toUtc) {
    EventQuery query;
    query.fromUtc = fromUtc;
    query.toUtc = toUtc;
    return queryEvents(query);
}

arc::Result<std::vector<Event>> Db::queryEvents(const EventQuery &query) {
    // One builder, one statement. Every user value is a bind parameter; only
    // the ORDER BY keywords are chosen from a switch, never copied from a
    // string, so a search term can never change the shape of the query.
    struct Binding {
        bool isText = false;
        long long number = 0;
        std::string text;
    };

    std::string sql = std::string("SELECT ") + kColumns + " FROM events WHERE 1=1";
    std::vector<Binding> bindings;

    auto bindNumber = [&](long long value) { bindings.push_back(Binding{false, value, {}}); };
    auto bindText = [&](const std::string &value) { bindings.push_back(Binding{true, 0, value}); };

    // Omit a bound that is still at its sentinel: an unbounded search should
    // not pay for a comparison against the end of time.
    if (query.toUtc != std::numeric_limits<long long>::max()) {
        sql += " AND start_utc < ?";
        bindNumber(query.toUtc);
    }
    if (query.fromUtc != std::numeric_limits<long long>::min()) {
        sql += " AND end_utc > ?";
        bindNumber(query.fromUtc);
    }
    if (!query.text.empty()) {
        // instr() is a literal substring search: unlike LIKE, `%` and `_` are
        // ordinary characters. lower() folds ASCII only; that is the promise.
        sql += " AND instr(lower(title || ' ' || ifnull(notes, '')), lower(?)) > 0";
        bindText(query.text);
    }
    if (query.type) {
        sql += " AND type = ?";
        bindText(eventTypeName(*query.type));
    }
    if (query.minImportance > 0 || query.maxImportance < 100) {
        sql += " AND importance BETWEEN ? AND ?";
        bindNumber(query.minImportance);
        bindNumber(query.maxImportance);
    }
    if (query.minUrgency > 0 || query.maxUrgency < 100) {
        sql += " AND urgency BETWEEN ? AND ?";
        bindNumber(query.minUrgency);
        bindNumber(query.maxUrgency);
    }
    if (query.visibility != Visibility::Any) {
        sql += " AND hidden = ?";
        bindNumber(query.visibility == Visibility::Hidden ? 1 : 0);
    }

    const char *order = "start_utc, id";
    switch (query.sort) {
    case EventSort::Start:
        order = "start_utc, id";
        break;
    case EventSort::StartDesc:
        order = "start_utc DESC, id DESC";
        break;
    case EventSort::Importance:
        // Most important first, then most urgent: importance alone leaves a
        // lot of ties at 50, and a stable tie-break makes tests (and pagination)
        // meaningful.
        order = "importance DESC, urgency DESC, start_utc, id";
        break;
    case EventSort::Urgency:
        order = "urgency DESC, importance DESC, start_utc, id";
        break;
    case EventSort::Updated:
        order = "updated_utc DESC, id DESC";
        break;
    case EventSort::Title:
        order = "title COLLATE NOCASE, start_utc, id";
        break;
    }
    sql += " ORDER BY ";
    sql += order;

    if (query.limit >= 0) {
        sql += " LIMIT ?";
        bindNumber(query.limit);
        if (query.offset > 0) {
            sql += " OFFSET ?";
            bindNumber(query.offset);
        }
    } else if (query.offset > 0) {
        // SQLite needs LIMIT before OFFSET; -1 means "no limit".
        sql += " LIMIT -1 OFFSET ?";
        bindNumber(query.offset);
    }

    Statement stmt;
    if (auto prepared = stmt.prepare(db_, sql.c_str()); !prepared) {
        return prepared.error();
    }
    for (size_t i = 0; i < bindings.size(); ++i) {
        const int index = static_cast<int>(i) + 1;
        if (bindings[i].isText) {
            stmt.bindText(index, bindings[i].text);
        } else {
            stmt.bindInt(index, bindings[i].number);
        }
    }

    std::vector<Event> events;
    for (;;) {
        auto row = stmt.step();
        if (!row) {
            return row.error();
        }
        if (!row.value()) {
            break;
        }
        events.push_back(readEvent(stmt.get()));
    }
    return events;
}

arc::Result<std::string> Db::integrityCheck() {
    Statement stmt;
    if (auto prepared = stmt.prepare(db_, "PRAGMA integrity_check"); !prepared) {
        return prepared.error();
    }

    std::string result;
    for (;;) {
        auto row = stmt.step();
        if (!row) {
            return row.error();
        }
        if (!row.value()) {
            break;
        }
        const auto *text = sqlite3_column_text(stmt.get(), 0);
        if (text != nullptr) {
            if (!result.empty()) {
                result.push_back('\n');
            }
            result += reinterpret_cast<const char *>(text);
        }
    }
    if (result.empty()) {
        result = "ok";
    }
    return result;
}

arc::Result<long long> Db::dataVersion() {
    Statement stmt;
    if (auto prepared = stmt.prepare(db_, "PRAGMA data_version"); !prepared) {
        return prepared.error();
    }
    auto row = stmt.step();
    if (!row) {
        return row.error();
    }
    if (!row.value()) {
        return 0;
    }
    return static_cast<long long>(sqlite3_column_int64(stmt.get(), 0));
}

arc::Result<void> Db::removeEvent(long long id) {    Statement stmt;
    if (auto prepared = stmt.prepare(db_, "DELETE FROM events WHERE id = ?"); !prepared) {
        return prepared.error();
    }
    stmt.bindInt(1, id);
    if (auto done = stmt.step(); !done) {
        return done.error();
    }
    return touchModified();
}

arc::Result<std::string> Db::getMeta(const std::string &key) {
    Statement stmt;
    if (auto prepared = stmt.prepare(db_, "SELECT value FROM meta WHERE key = ?"); !prepared) {
        return prepared.error();
    }
    stmt.bindText(1, key);
    auto row = stmt.step();
    if (!row) {
        return row.error();
    }
    if (!row.value()) {
        return std::string{}; // absent is not an error: prefs simply are not set
    }
    const auto *text = sqlite3_column_text(stmt.get(), 0);
    return std::string(text != nullptr ? reinterpret_cast<const char *>(text) : "");
}

arc::Result<void> Db::setMeta(const std::string &key, const std::string &value) {
    Statement stmt;
    if (auto prepared = stmt.prepare(db_,
                                     "INSERT INTO meta(key, value) VALUES(?, ?) "
                                     "ON CONFLICT(key) DO UPDATE SET value = excluded.value");
        !prepared) {
        return prepared.error();
    }
    stmt.bindText(1, key);
    stmt.bindText(2, value);
    if (auto done = stmt.step(); !done) {
        return done.error();
    }
    return {};
}

arc::Result<void> Db::touchModified() {
    // The Syncthing sync policy's "last writer" marker (see essentialAgent/
    // MEMORY.md §7). Cheap, and the one key that changes on every write.
    return setMeta("modified_utc", std::to_string(utcNowSeconds()));
}

} // namespace klend_ario
