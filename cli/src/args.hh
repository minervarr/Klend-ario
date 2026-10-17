#pragma once

#include <string>
#include <string_view>
#include <vector>

// Hand-rolled, std-only argument parsing (essentialAgent/MEMORY.md: no getopt,
// no dependency). Pure — it looks at strings and returns a struct, so args_test
// links this file and nothing else.
//
// Validated values are the CALLER's job: this struct carries strings verbatim
// (dates, durations, formats). That keeps parsing free of the model, and the
// model's parser is tested once, in event_test/timeexpr_test.
//
// The `*Given` flags are load-bearing for `edit`: a missing --importance means
// "leave it alone", while --importance 50 means "set it to 50". Defaults alone
// cannot tell those apart.

namespace klend_ario::cli {

enum class Verb { None, Help, Add, Edit, Duplicate, List, Show, Remove, Holidays };

struct Args {
    Verb verb = Verb::None;
    std::string helpTopic; // for `help edit` / `edit --help`

    // Common
    std::string dbPath;  // empty => the caller uses the state-layout default
    std::string keyFile; // empty => no --key-file; $KLEND_ARIO_KEY may be used
    bool json = false;   // legacy alias for --format json
    std::string format = "text";
    bool noHeader = false; // suppress the header row of csv/tsv/table

    // add / edit / duplicate
    std::string title;
    bool titleGiven = false;
    std::string start; // instant expression, verbatim
    bool startGiven = false;
    std::string end; // instant expression, verbatim
    bool endGiven = false;
    std::string duration; // duration expression, verbatim
    bool durationGiven = false;
    std::string type = "event";
    bool typeGiven = false;
    int importance = 50;
    bool importanceGiven = false;
    int urgency = 50;
    bool urgencyGiven = false;
    int alarmOffset = 0;
    bool alarmOffsetGiven = false;
    bool hidden = false;
    bool hiddenGiven = false; // set by --hidden or --unhide
    std::string notes;
    bool notesGiven = false;
    bool clearNotes = false;
    std::string shift; // duration expression for edit/duplicate
    bool shiftGiven = false;

    // list
    std::string from; // instant expression, verbatim (empty => today)
    std::string to;   // instant expression, verbatim (empty => from + 7 days)

    // holidays
    std::string country = "PE";
    int year = 0;           // meaningful only when yearGiven
    bool yearGiven = false; // keeps an explicit 0 from meaning "now"

    // show / rm / edit / duplicate
    long long id = 0;

    // Non-empty means a usage error; the CLI prints it (and usage) and exits 2.
    std::string error;
};

// Parses the arguments AFTER the program name.
Args parseArgs(const std::vector<std::string_view> &argv);

// The full help text, and one verb's section ("" when the topic is unknown).
std::string usage();
std::string usageFor(std::string_view topic);

} // namespace klend_ario::cli
