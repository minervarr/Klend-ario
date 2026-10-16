#include "klend_ario/db.hh"

#include "klend_ario/schema.hh"

#include <sqlite3.h>

#include <cstdio>
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
    sqlite3 *handle = nullptr;
    const int rc = sqlite3_open_v2(path.c_str(), &handle,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        arc::Error error = dbError(handle, "open");
        if (handle != nullptr) {
            sqlite3_close(handle);
        }
        return error;
    }
    Db db(handle);
    if (auto migrated = db.migrate(); !migrated) {
        return migrated.error();
    }
    return db;
}

arc::Result<Db> Db::openMemory() { return open(":memory:"); }

arc::Result<void> Db::migrate() {
    if (auto applied = exec(db_, schemaSql()); !applied) {
        return applied.error();
    }
    // Record/refresh the version now that `meta` exists. A future migration
    // reads the old value first and applies additive steps up to kSchemaVersion.
    char sql[160];
    std::snprintf(sql, sizeof(sql),
                  "INSERT INTO meta(key, value) VALUES('schema_version', '%d') "
                  "ON CONFLICT(key) DO UPDATE SET value = excluded.value",
                  kSchemaVersion);
    return exec(db_, sql);
}

arc::Result<long long> Db::insertEvent(Event e) {
    const long long now = utcNowSeconds();
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
    Statement stmt;
    if (auto prepared = stmt.prepare(db_, kInsert); !prepared) {
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
    stmt.bindInt(10, e.createdUtc);
    stmt.bindInt(11, e.updatedUtc);
    if (auto done = stmt.step(); !done) {
        return done.error();
    }
    return sqlite3_last_insert_rowid(db_);
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
    const std::string sql = std::string("SELECT ") + kColumns +
                            " FROM events WHERE start_utc < ? AND end_utc > ? "
                            "ORDER BY start_utc, id";
    Statement stmt;
    if (auto prepared = stmt.prepare(db_, sql.c_str()); !prepared) {
        return prepared.error();
    }
    stmt.bindInt(1, toUtc);
    stmt.bindInt(2, fromUtc);

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

arc::Result<void> Db::removeEvent(long long id) {
    Statement stmt;
    if (auto prepared = stmt.prepare(db_, "DELETE FROM events WHERE id = ?"); !prepared) {
        return prepared.error();
    }
    stmt.bindInt(1, id);
    if (auto done = stmt.step(); !done) {
        return done.error();
    }
    return {};
}

} // namespace klend_ario
