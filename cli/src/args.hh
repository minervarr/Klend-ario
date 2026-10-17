#pragma once

#include <string>
#include <string_view>
#include <vector>

// Hand-rolled, std-only argument parsing (essentialAgent/MEMORY.md: no getopt,
// no dependency). Pure — it looks at strings and returns a struct, so args_test
// links this file and nothing else.
//
// Validated values are the CALLER's job: this struct carries strings verbatim
// (dates, for instance). That keeps parsing free of the model, and the model's
// ISO parser is tested once, in event_test.

namespace klend_ario::cli {

enum class Verb { None, Help, Add, List, Show, Remove, Holidays };

struct Args {
    Verb verb = Verb::None;

    // Common
    std::string dbPath; // empty => the caller uses the state-layout default
    std::string keyFile; // empty => no --key-file; $KLEND_ARIO_KEY may be used
    bool json = false;

    // add
    std::string title;
    std::string start; // ISO-8601 text, verbatim
    std::string end;   // ISO-8601 text, verbatim
    std::string type = "event";
    int importance = 50;
    int urgency = 50;
    int alarmOffset = 0;
    bool hidden = false;
    std::string notes;

    // list
    std::string from; // ISO-8601 text, verbatim (empty => now)
    std::string to;   // ISO-8601 text, verbatim (empty => from + 7 days)

    // holidays
    std::string country = "PE";
    int year = 0;           // meaningful only when yearGiven
    bool yearGiven = false; // keeps an explicit 0 from meaning "now"

    // show / rm
    long long id = 0;

    // Non-empty means a usage error; the CLI prints it (and usage) and exits 2.
    std::string error;
};

// Parses the arguments AFTER the program name.
Args parseArgs(const std::vector<std::string_view> &argv);

std::string usage();

} // namespace klend_ario::cli
