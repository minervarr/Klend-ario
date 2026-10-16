#pragma once

#include "args.hh"

// The CLI's command layer, separate from main() so it can be driven directly by
// cli/tests/e2e_test.cc against a temporary database — the end-to-end check in
// essentialAgent/TESTING.md. main() is then only argv -> parseArgs -> this.
namespace klend_ario::cli {

// Exit codes, shared by every command and asserted by the e2e test.
inline constexpr int kExitOk = 0;      // it worked
inline constexpr int kExitError = 1;   // a core operation failed
inline constexpr int kExitUsage = 2;   // the command line was wrong

// Runs one parsed command and returns its exit code.
int runCommand(const Args &args);

} // namespace klend_ario::cli
