#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "arc/result.hh"
#include "klend_ario/event.hh"

// Import parsing, as a pure value.
//
// The parser reads the whole document before returning anything: a bad row
// rejects the file, so a truncated or hand-edited export cannot be applied
// halfway. `Db::importEvents` then writes the vector in one transaction.
//
// CSV is the same shape as `export --format csv` (RFC 4180, header by name).
// JSONL is one flat object per line; unknown scalar keys are ignored so a newer
// export remains readable, but nested values are rejected. ICS is deliberately
// not an import format: recurrence, timezones and attendees are a different
// problem from the one-way exporter.

namespace klend_ario {

enum class ImportFormat { Csv, Jsonl };

const char *importFormatName(ImportFormat format);
bool importFormatFromName(std::string_view name, ImportFormat &out);

// Empty/blank input is an error, not "zero events": a truncated file must not
// be able to wipe a database through `--replace`.
arc::Result<std::vector<Event>> parseImportedEvents(std::string_view text, ImportFormat format);

} // namespace klend_ario
