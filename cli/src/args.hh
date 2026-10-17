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

enum class Verb {
    None,
    Help,
    Version,
    Add,
    Edit,
    Duplicate,
    List,
    Today,
    Week,
    Next,
    Agenda,
    Search,
    Conflicts,
    Free,
    Alarms,
    Stats,
    Doctor,
    Export,
    Import,
    Completion,
    Show,
    Remove,
    Holidays,
};

struct Args {
    Verb verb = Verb::None;
    std::string helpTopic;       // for `help edit` / `edit --help`
    std::string completionShell; // bash|zsh|fish for `completion`

    // Common
    std::string dbPath;  // empty => the caller uses the state-layout default
    std::string keyFile; // empty => no --key-file; $KLEND_ARIO_KEY may be used
    bool json = false;   // legacy alias for --format json
    std::string format = "text";
    bool formatGiven = false; // export/import need a different default than text
    bool noHeader = false;    // suppress the header row of csv/tsv/table

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

    // list / today / week / next / agenda / search
    std::string from; // instant expression, verbatim (empty => the verb's default)
    bool fromGiven = false;
    std::string to; // instant expression, verbatim (empty => the verb's default)
    bool toGiven = false;
    std::string search; // literal substring of title or notes
    bool searchGiven = false;
    std::string sort = "start";
    bool sortGiven = false;
    int limit = -1; // -1 = unlimited; 0 is a valid "no rows"
    bool limitGiven = false;
    int offset = 0;
    bool offsetGiven = false;
    int minImportance = 0;
    bool minImportanceGiven = false;
    int maxImportance = 100;
    bool maxImportanceGiven = false;
    int minUrgency = 0;
    bool minUrgencyGiven = false;
    int maxUrgency = 100;
    bool maxUrgencyGiven = false;
    // Visibility filters. They are mutually exclusive; the verb picks the
    // default (views visible-only, list/search everything).
    bool visibleOnly = false;
    bool hiddenOnly = false;
    bool includeHidden = false;
    int days = 7; // agenda's window
    bool daysGiven = false;

    // free
    std::string minDuration = "30m"; // duration expression, verbatim
    bool minDurationGiven = false;

    // import
    std::string importPath; // "-" means stdin
    bool replace = false;
    bool preserveIds = false;
    bool dryRun = false;

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
