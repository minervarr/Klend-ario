#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "klend_ario/event.hh"

// Interchange export, as a pure value.
//
// This is deliberately separate from the CLI's display formatters: `list` has a
// compact human shape, while export is a stable, lossless contract that another
// machine (or a future import) can read. CSV and JSONL carry every stored field;
// ICS is the one-way calendar interchange format and carries the same event as
// a VEVENT with a VALARM for the T-offset.
//
// The caller chooses the events (range, type, visibility); this module never
// reaches for a database or a clock. `DTSTAMP` comes from `updatedUtc` (or the
// start when an event has no update stamp), so the same input always produces
// the same bytes.

namespace klend_ario {

enum class ExportFormat { Csv, Jsonl, Ics };

const char *exportFormatName(ExportFormat format);
bool exportFormatFromName(std::string_view name, ExportFormat &out);

std::string exportEvents(const std::vector<Event> &events, ExportFormat format);

} // namespace klend_ario
