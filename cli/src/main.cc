// klend_ario_cli — the MVP frontend.
//
// main() does one thing: turn argv into parsed args and hand them to the
// command layer (cli/src/commands.cc). Everything else is there, and the
// business logic it calls is in core/ (essentialAgent/AGENTS.md §4).

#include <string_view>
#include <vector>

#include "args.hh"
#include "commands.hh"

int main(int argc, char **argv) {
    const std::vector<std::string_view> tokens(argv + 1, argv + argc);
    return klend_ario::cli::runCommand(klend_ario::cli::parseArgs(tokens));
}
