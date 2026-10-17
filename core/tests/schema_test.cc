#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include "klend_ario/db.hh"
#include "klend_ario/event.hh"
#include "klend_ario/schema.hh"

using namespace klend_ario;

namespace {

Event makeEvent(const char *title, long long start, long long end) {
    Event e;
    e.title = title;
    e.startUtc = start;
    e.endUtc = end;
    return e;
}

void testOpenAppliesSchema() {
    auto db = Db::openMemory();
    assert(db.ok());

    Event e;
    e.title = "Standup";
    e.startUtc = 1000;
    e.endUtc = 1600;
    e.importance = 80;
    e.urgency = 10;
    e.notes = "daily";
    auto id = db.value().insertEvent(e);
    assert(id.ok());
    assert(id.value() > 0);

    auto read = db.value().getEvent(id.value());
    assert(read.ok());
    assert(read.value().title == "Standup");
    assert(read.value().startUtc == 1000);
    assert(read.value().endUtc == 1600);
    assert(read.value().importance == 80);
    assert(read.value().urgency == 10);
    assert(read.value().notes == "daily");
    assert(read.value().type == EventType::Event);
    // Insert stamps created/updated when the caller leaves them at 0.
    assert(read.value().createdUtc != 0);
    assert(read.value().updatedUtc == read.value().createdUtc);
}

void testConstraints() {
    auto db = Db::openMemory();
    assert(db.ok());

    // end before start violates the CHECK.
    assert(!db.value().insertEvent(makeEvent("bad", 200, 100)).ok());

    // importance outside 0..100 violates the CHECK.
    Event bad = makeEvent("bad2", 100, 200);
    bad.importance = 101;
    assert(!db.value().insertEvent(bad).ok());
}

void testListRangeOverlap() {
    auto db = Db::openMemory();
    assert(db.ok());
    db.value().insertEvent(makeEvent("a", 900, 1100));  // spans into the range
    db.value().insertEvent(makeEvent("b", 1500, 1700)); // fully inside
    db.value().insertEvent(makeEvent("c", 2500, 2600)); // after

    auto list = db.value().listEvents(1000, 2000);
    assert(list.ok());
    assert(list.value().size() == 2);
    // Ordered by start. The half-open range includes "a" (its end is 1100>1000)
    // even though it started before the window — overlap, not containment.
    assert(list.value()[0].title == "a");
    assert(list.value()[1].title == "b");
}

void testRemove() {
    auto db = Db::openMemory();
    assert(db.ok());
    auto id = db.value().insertEvent(makeEvent("gone", 1, 2));
    assert(id.ok());
    assert(db.value().removeEvent(id.value()).ok());
    assert(!db.value().getEvent(id.value()).ok());
    // Removing an id that never existed still succeeds.
    assert(db.value().removeEvent(999999).ok());
}

void testMissingEventIsError() {
    auto db = Db::openMemory();
    assert(db.ok());
    assert(!db.value().getEvent(42).ok());
}

void testUpdateEvent() {
    auto db = Db::openMemory();
    assert(db.ok());
    auto id = db.value().insertEvent(makeEvent("draft", 100, 200));
    assert(id.ok());
    auto before = db.value().getEvent(id.value());
    assert(before.ok());

    Event edit = before.value();
    edit.title = "final";
    edit.endUtc = 300;
    edit.hidden = true;
    edit.type = EventType::Sleep;
    edit.notes = "moved";
    assert(db.value().updateEvent(edit).ok());

    auto after = db.value().getEvent(id.value());
    assert(after.ok());
    assert(after.value().title == "final");
    assert(after.value().endUtc == 300);
    assert(after.value().hidden);
    assert(after.value().type == EventType::Sleep);
    assert(after.value().notes == "moved");
    // An edit does not create: created_utc is untouched.
    assert(after.value().createdUtc == before.value().createdUtc);

    // Updating an id that is not there is reported, not a silent no-op.
    Event ghost = edit;
    ghost.id = 999999;
    assert(!db.value().updateEvent(ghost).ok());
}

void testRangeBoundaryIsHalfOpen() {
    auto db = Db::openMemory();
    assert(db.ok());
    db.value().insertEvent(makeEvent("touches", 900, 1000)); // ends exactly at `from`
    db.value().insertEvent(makeEvent("inside", 1000, 1100));
    auto list = db.value().listEvents(1000, 2000);
    assert(list.ok());
    assert(list.value().size() == 1);
    assert(list.value()[0].title == "inside");
}

void testMeta() {
    auto db = Db::openMemory();
    assert(db.ok());
    // An absent key is empty, not an error: preferences simply are not set.
    assert(db.value().getMeta("week_start").value().empty());
    assert(db.value().setMeta("week_start", "sunday").ok());
    assert(db.value().getMeta("week_start").value() == "sunday");
    // Writing again overwrites.
    assert(db.value().setMeta("week_start", "monday").ok());
    assert(db.value().getMeta("week_start").value() == "monday");
}

void testSchemaVersionIsRecorded() {
    auto db = Db::openMemory();
    assert(db.ok());
    assert(db.value().getMeta("schema_version").value() == std::to_string(kSchemaVersion));
}

void testNewerSchemaIsRefused() {
    // This needs a real file: the guard only shows when the SAME database is
    // reopened. It lives in the test's working directory and is removed after.
    const char *path = "klend_schema_guard_test.db";
    std::remove(path);
    {
        auto db = Db::open(path);
        assert(db.ok());
        // Pretend a newer build wrote this file.
        assert(db.value().setMeta("schema_version", std::to_string(kSchemaVersion + 1)).ok());
    }
    auto reopened = Db::open(path);
    assert(!reopened.ok());
    std::remove(path);
}

void testModifiedMarker() {
    auto db = Db::openMemory();
    assert(db.ok());
    assert(db.value().getMeta("modified_utc").value().empty()); // nothing written yet
    assert(db.value().insertEvent(makeEvent("a", 1, 2)).ok());
    const std::string after = db.value().getMeta("modified_utc").value();
    assert(!after.empty());
    assert(std::strtoll(after.c_str(), nullptr, 10) > 0);
}

void testEncryptionRoundTrip() {
    const char *path = "klend_crypto_test.db";
    std::remove(path);
    const std::string key = "correct horse battery staple";
    {
        auto db = Db::openEncrypted(path, key);
        assert(db.ok());
        assert(db.value().insertEvent(makeEvent("secret", 100, 200)).ok());
    }
    {
        auto db = Db::openEncrypted(path, key);
        assert(db.ok());
        auto all = db.value().listEvents(0, 1000);
        assert(all.ok());
        assert(all.value().size() == 1);
        assert(all.value()[0].title == "secret");
    }
    // A wrong key is refused at open, rather than looking like an empty calendar.
    assert(!Db::openEncrypted(path, "wrong key").ok());
    // A plain open of an encrypted file also fails.
    assert(!Db::open(path).ok());
    // An empty passphrase is a caller bug, not "no encryption".
    assert(!Db::openEncrypted(path, "").ok());
    std::remove(path);
}

void testReadOnlyOpenDoesNotCreateOrMigrate() {
    const char *missing = "klend_readonly_missing_test.db";
    std::remove(missing);
    assert(!Db::openReadOnly(missing).ok());
    std::ifstream missingCheck(missing);
    assert(!missingCheck.good());

    const char *path = "klend_readonly_test.db";
    std::remove(path);
    {
        auto db = Db::open(path);
        assert(db.ok());
        assert(db.value().insertEvent(makeEvent("readable", 100, 200)).ok());
    }
    {
        auto db = Db::openReadOnly(path);
        assert(db.ok());
        auto all = db.value().listEvents(0, 1000);
        assert(all.ok());
        assert(all.value().size() == 1);
        assert(all.value()[0].title == "readable");
        // The handle is genuinely read-only, not just conventionally so.
        assert(!db.value().insertEvent(makeEvent("nope", 300, 400)).ok());
    }

    // A zero-byte file must open for inspection, but must not be initialised.
    // That is what lets `doctor` report the missing schema instead of creating
    // a healthy empty calendar over a truncated file.
    const char *empty = "klend_readonly_empty_test.db";
    std::remove(empty);
    {
        std::ofstream out(empty);
        assert(out.good());
    }
    {
        auto db = Db::openReadOnly(empty);
        assert(db.ok());
        assert(!db.value().getMeta("schema_version").ok());
    }
    {
        std::ifstream check(empty, std::ios::binary | std::ios::ate);
        assert(check.good());
        assert(check.tellg() == 0);
    }

    std::remove(path);
    std::remove(empty);
}

void testEncryptedReadOnlyOpen() {
    const char *path = "klend_readonly_crypto_test.db";
    std::remove(path);
    const std::string key = "correct horse battery staple";
    {
        auto db = Db::openEncrypted(path, key);
        assert(db.ok());
        assert(db.value().insertEvent(makeEvent("secret", 100, 200)).ok());
    }
    {
        auto db = Db::openEncryptedReadOnly(path, key);
        assert(db.ok());
        auto all = db.value().listEvents(0, 1000);
        assert(all.ok());
        assert(all.value().size() == 1);
        assert(all.value()[0].title == "secret");
    }
    assert(!Db::openEncryptedReadOnly(path, "wrong key").ok());
    assert(!Db::openReadOnly(path).ok());
    std::remove(path);
}

void testEncryptedFileIsNotPlaintext() {
    const char *path = "klend_crypto_header_test.db";
    std::remove(path);
    {
        auto db = Db::openEncrypted(path, "pw");
        assert(db.ok());
        assert(db.value().insertEvent(makeEvent("x", 1, 2)).ok());
    }
    // An unencrypted SQLite file starts with "SQLite format 3\0"; an encrypted
    // one must not, or the encryption is not actually happening.
    std::ifstream in(path, std::ios::binary);
    assert(in.good());
    char header[16] = {0};
    in.read(header, sizeof(header));
    assert(in.gcount() == 16);
    static const char kSqliteMagic[16] = "SQLite format 3"; // + implicit NUL
    assert(std::memcmp(header, kSqliteMagic, 16) != 0);
    in.close();
    std::remove(path);
}

void testDataVersionSeesOtherConnections() {
    // data_version exists so a long-lived view notices an edit made by another
    // process (the CLI, the MCP server) without polling the file's mtime.
    const char *path = "klend_data_version_test.db";
    std::remove(path);
    auto writer = Db::open(path);
    assert(writer.ok());
    auto reader = Db::openReadOnly(path);
    assert(reader.ok());

    const long long before = reader.value().dataVersion().value();
    // The reader has not changed, and a schema-only write by the writer is still
    // a commit: the counter must move.
    assert(writer.value().insertEvent(makeEvent("Later", 10, 20)).ok());
    assert(reader.value().dataVersion().value() != before);
    std::remove(path);
}

} // namespace

int main() {
    testOpenAppliesSchema();
    testConstraints();
    testListRangeOverlap();
    testRemove();
    testMissingEventIsError();
    testUpdateEvent();
    testRangeBoundaryIsHalfOpen();
    testMeta();
    testSchemaVersionIsRecorded();
    testNewerSchemaIsRefused();
    testModifiedMarker();
    testDataVersionSeesOtherConnections();
    testEncryptionRoundTrip();
    testReadOnlyOpenDoesNotCreateOrMigrate();
    testEncryptedReadOnlyOpen();
    testEncryptedFileIsNotPlaintext();
    std::printf("schema_test: all assertions passed\n");
    return 0;
}
