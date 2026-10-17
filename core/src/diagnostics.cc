#include "klend_ario/diagnostics.hh"

#include <string>
#include <string_view>
#include <utility>

#include "klend_ario/query.hh"
#include "klend_ario/schema.hh"
#include "klend_ario/sync.hh"

namespace klend_ario {
namespace {

bool parseNonNegative(std::string_view text, long long &out) {
    if (text.empty()) {
        return false;
    }
    long long value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + (c - '0');
    }
    out = value;
    return true;
}

DiagnosticCheck check(const char *name, CheckStatus status, std::string detail) {
    return DiagnosticCheck{name, status, std::move(detail)};
}

} // namespace

const char *checkStatusName(CheckStatus status) {
    switch (status) {
    case CheckStatus::Ok:
        return "ok";
    case CheckStatus::Warn:
        return "warn";
    case CheckStatus::Fail:
        return "fail";
    }
    return "fail";
}

bool Diagnostics::healthy() const {
    for (const DiagnosticCheck &entry : checks) {
        if (entry.status != CheckStatus::Ok) {
            return false;
        }
    }
    return true;
}

Diagnostics diagnose(Db &db, const std::string &dbPath) {
    Diagnostics report;

    // Integrity first: every later check is meaningless on a corrupt file.
    if (auto integrity = db.integrityCheck(); !integrity) {
        report.checks.push_back(check("integrity", CheckStatus::Fail, integrity.error().message));
    } else if (integrity.value() != "ok") {
        report.checks.push_back(check("integrity", CheckStatus::Fail, integrity.value()));
    } else {
        report.checks.push_back(check("integrity", CheckStatus::Ok, "ok"));
    }

    auto storedVersion = db.getMeta("schema_version");
    if (!storedVersion) {
        report.checks.push_back(check("schema", CheckStatus::Fail, storedVersion.error().message));
    } else if (storedVersion.value().empty()) {
        report.checks.push_back(check("schema", CheckStatus::Fail, "meta.schema_version is missing"));
    } else {
        long long version = 0;
        if (!parseNonNegative(storedVersion.value(), version)) {
            report.checks.push_back(check("schema", CheckStatus::Fail,
                                          "meta.schema_version is not a number: " +
                                              storedVersion.value()));
        } else if (version != kSchemaVersion) {
            report.checks.push_back(
                check("schema", CheckStatus::Fail,
                      "database v" + std::to_string(version) + ", build v" +
                          std::to_string(kSchemaVersion)));
        } else {
            report.checks.push_back(
                check("schema", CheckStatus::Ok, "v" + std::to_string(version)));
        }
    }

    auto events = db.queryEvents(EventQuery{});
    const bool eventsReadable = events.ok();

    auto modified = db.getMeta("modified_utc");
    if (!modified) {
        report.checks.push_back(check("modified", CheckStatus::Fail, modified.error().message));
    } else if (modified.value().empty()) {
        if (eventsReadable && events.value().empty()) {
            report.checks.push_back(
                check("modified", CheckStatus::Ok, "no writes yet"));
        } else {
            report.checks.push_back(
                check("modified", CheckStatus::Warn, "meta.modified_utc is missing"));
        }
    } else {
        long long modifiedUtc = 0;
        if (!parseNonNegative(modified.value(), modifiedUtc) || modifiedUtc == 0) {
            report.checks.push_back(check("modified", CheckStatus::Warn,
                                          "meta.modified_utc is malformed: " + modified.value()));
        } else {
            report.checks.push_back(
                check("modified", CheckStatus::Ok, "modified_utc=" + modified.value()));
        }
    }

    if (dbPath.empty() || dbPath == ":memory:") {
        report.checks.push_back(check("sync", CheckStatus::Ok, "in-memory database"));
    } else {
        const std::vector<std::string> conflicts = syncConflictCopies(dbPath);
        if (conflicts.empty()) {
            report.checks.push_back(check("sync", CheckStatus::Ok, "no conflict copies"));
        } else {
            std::string detail = std::to_string(conflicts.size()) +
                                 " sync conflict copy/copies; resolve manually: " + conflicts[0];
            report.checks.push_back(check("sync", CheckStatus::Warn, std::move(detail)));
        }
    }

    if (!eventsReadable) {
        report.checks.push_back(check("events", CheckStatus::Fail, events.error().message));
    } else {
        long long invalid = 0;
        long long zeroLength = 0;
        for (const Event &event : events.value()) {
            if (event.endUtc < event.startUtc) {
                ++invalid;
            }
            if (event.endUtc == event.startUtc) {
                ++zeroLength;
            }
        }
        if (invalid > 0) {
            report.checks.push_back(check("events", CheckStatus::Fail,
                                          std::to_string(invalid) + " invalid interval(s)"));
        } else {
            report.checks.push_back(
                check("events", CheckStatus::Ok,
                      std::to_string(events.value().size()) + " events (" +
                          std::to_string(zeroLength) + " zero-length)"));
        }
    }

    return report;
}

} // namespace klend_ario
