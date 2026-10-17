#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>

#include "klend_ario/db.hh"
#include "klend_ario/diagnostics.hh"
#include "klend_ario/event.hh"

using namespace klend_ario;

namespace {

const DiagnosticCheck *find(const Diagnostics &report, const char *name) {
    for (const DiagnosticCheck &check : report.checks) {
        if (check.name == name) {
            return &check;
        }
    }
    return nullptr;
}

void testFreshDatabaseIsHealthy() {
    auto db = Db::openMemory();
    assert(db.ok());

    const Diagnostics report = diagnose(db.value(), ":memory:");
    assert(report.healthy());
    assert(report.checks.size() == 5);

    const DiagnosticCheck *integrity = find(report, "integrity");
    assert(integrity != nullptr && integrity->status == CheckStatus::Ok);
    assert(integrity->detail == "ok");

    const DiagnosticCheck *schema = find(report, "schema");
    assert(schema != nullptr && schema->status == CheckStatus::Ok);
    assert(schema->detail == "v1");

    const DiagnosticCheck *modified = find(report, "modified");
    assert(modified != nullptr && modified->status == CheckStatus::Ok);
    assert(modified->detail == "no writes yet");

    const DiagnosticCheck *sync = find(report, "sync");
    assert(sync != nullptr && sync->status == CheckStatus::Ok);

    const DiagnosticCheck *events = find(report, "events");
    assert(events != nullptr && events->status == CheckStatus::Ok);
    assert(events->detail == "0 events (0 zero-length)");

    assert(std::string(checkStatusName(CheckStatus::Ok)) == "ok");
    assert(std::string(checkStatusName(CheckStatus::Warn)) == "warn");
    assert(std::string(checkStatusName(CheckStatus::Fail)) == "fail");
}

void testWriteMakesTheMarkerPresent() {
    auto db = Db::openMemory();
    assert(db.ok());

    Event event;
    event.title = "Standup";
    event.startUtc = 1000;
    event.endUtc = 1600;
    auto id = db.value().insertEvent(event);
    assert(id.ok());

    const Diagnostics report = diagnose(db.value(), ":memory:");
    assert(report.healthy());
    const DiagnosticCheck *modified = find(report, "modified");
    assert(modified != nullptr && modified->status == CheckStatus::Ok);
    assert(modified->detail.rfind("modified_utc=", 0) == 0);

    const DiagnosticCheck *events = find(report, "events");
    assert(events != nullptr && events->status == CheckStatus::Ok);
    assert(events->detail == "1 events (0 zero-length)");
}

void testMissingModifiedIsAWarning() {
    auto db = Db::openMemory();
    assert(db.ok());

    // An event is enough to make the missing marker suspicious.
    Event event;
    event.title = "X";
    event.startUtc = 1000;
    event.endUtc = 1600;
    assert(db.value().insertEvent(event).ok());
    assert(db.value().setMeta("modified_utc", "").ok());

    const Diagnostics report = diagnose(db.value(), ":memory:");
    assert(!report.healthy());
    const DiagnosticCheck *modified = find(report, "modified");
    assert(modified != nullptr && modified->status == CheckStatus::Warn);
    assert(modified->detail == "meta.modified_utc is missing");
}

void testMalformedModifiedAndSchemaFailures() {
    auto db = Db::openMemory();
    assert(db.ok());

    assert(db.value().setMeta("modified_utc", "yesterday").ok());
    Diagnostics report = diagnose(db.value(), ":memory:");
    assert(!report.healthy());
    const DiagnosticCheck *modified = find(report, "modified");
    assert(modified != nullptr && modified->status == CheckStatus::Warn);

    assert(db.value().setMeta("schema_version", "999").ok());
    report = diagnose(db.value(), ":memory:");
    assert(!report.healthy());
    const DiagnosticCheck *schema = find(report, "schema");
    assert(schema != nullptr && schema->status == CheckStatus::Fail);
    assert(schema->detail == "database v999, build v1");
}

void testIntegrityCheckIsOk() {
    auto db = Db::openMemory();
    assert(db.ok());
    auto integrity = db.value().integrityCheck();
    assert(integrity.ok());
    assert(integrity.value() == "ok");
}

} // namespace

int main() {
    testFreshDatabaseIsHealthy();
    testWriteMakesTheMarkerPresent();
    testMissingModifiedIsAWarning();
    testMalformedModifiedAndSchemaFailures();
    testIntegrityCheckIsOk();
    std::printf("diagnostics_test: all assertions passed\n");
    return 0;
}
