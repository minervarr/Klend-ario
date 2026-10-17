#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "klend_ario/alarm.hh"
#include "klend_ario/conflict.hh"
#include "klend_ario/event.hh"
#include "klend_ario/free.hh"
#include "klend_ario/holidays.hh"

// Output rendering, as pure functions.
//
// The command layer used to printf() each row directly, which made the CLI's
// actual contract — the text a user sees — untestable: an end-to-end test could
// only assert an exit code. Rendering here returns a std::string, so
// format_test asserts the bytes and commands.cc only writes them.
//
// Formats are deliberately a small, fixed set. csv/tsv exist so the CLI can be
// piped into a spreadsheet or another program without a JSON parser; they are
// not a promise of arbitrary dialect support.

namespace klend_ario::cli {

enum class OutputFormat { Text, Table, Json, Csv, Tsv };

bool parseOutputFormat(std::string_view name, OutputFormat &out);
const char *outputFormatName(OutputFormat format);

struct FormatOptions {
    // CSV/TSV/table header row. JSON carries no header concept, and text is
    // one tuple per line, so this only affects the tabular formats.
    bool header = true;
};

// One event as a single text line, or one row/object in a list. For Text this
// omits `notes`, which would break the one-line-per-event shape of `list`.
std::string formatEventLine(const Event &event, OutputFormat format);

// One event as a whole record: text adds the notes line, JSON is the object,
// csv/tsv/table use the same header+row as a one-row list.
std::string formatEventDetail(const Event &event, OutputFormat format, const FormatOptions &options);

std::string formatEvents(const std::vector<Event> &events, OutputFormat format,
                         const FormatOptions &options);

std::string formatHolidays(const std::vector<holidays::Holiday> &holidays, OutputFormat format,
                           const FormatOptions &options);

std::string formatConflicts(const std::vector<Conflict> &conflicts, OutputFormat format,
                            const FormatOptions &options);

std::string formatFreeSlots(const std::vector<FreeSlot> &slots, OutputFormat format,
                            const FormatOptions &options);

std::string formatAlarms(const std::vector<AlarmTrigger> &triggers, OutputFormat format,
                         const FormatOptions &options);

// The value `add`, `edit` and `duplicate` print: a bare id, or {"id":N}.
std::string formatId(long long id, OutputFormat format);

} // namespace klend_ario::cli
