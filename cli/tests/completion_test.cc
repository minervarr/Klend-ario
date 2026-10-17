#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>
#include <string_view>

#include "completion.hh"

using namespace klend_ario::cli;

namespace {

void expectVerbs(const std::string &script) {
    for (const std::string_view verb :
         {"add", "edit", "duplicate", "list", "today", "week", "next", "agenda", "search",
          "conflicts", "free", "alarms", "stats", "doctor", "export", "import",
          "completion", "show", "rm", "holidays", "help"}) {
        assert(script.find(std::string(verb)) != std::string::npos);
    }
}

std::string bashWords(const std::string &script, std::string_view verb) {
    const std::string needle = "        " + std::string(verb) + ") words=\"";
    const size_t start = script.find(needle);
    assert(start != std::string::npos);
    const size_t begin = start + needle.size();
    const size_t end = script.find("\";;", begin);
    assert(end != std::string::npos);
    return script.substr(begin, end - begin);
}

std::string zshCase(const std::string &script, std::string_view verb) {
    const std::string needle = "                " + std::string(verb) + ")\n";
    const size_t start = script.find(needle);
    assert(start != std::string::npos);
    const size_t end = script.find("                    ;;", start);
    assert(end != std::string::npos);
    return script.substr(start, end - start);
}

void testBash() {
    const std::string script = completionScript("bash");
    assert(!script.empty());
    assert(script.rfind("# bash completion for klend_ario_cli\n", 0) == 0);
    assert(script.find("complete -F _klend_ario_cli klend_ario_cli\n") != std::string::npos);
    assert(script.find("--format") != std::string::npos);
    assert(script.find("--version") != std::string::npos);
    assert(script.back() == '\n');
    expectVerbs(script);

    // A suggestion must be valid for the verb. These are the cases the audit
    // found: `--days` is agenda-only, `next` fixes its sort, and stats has
    // neither `--limit` nor `--min-duration`.
    assert(bashWords(script, "list").find("--days") == std::string::npos);
    assert(bashWords(script, "agenda").find("--days") != std::string::npos);
    assert(bashWords(script, "next").find("--sort") == std::string::npos);
    assert(bashWords(script, "stats").find("--limit") == std::string::npos);
    assert(bashWords(script, "stats").find("--min-duration") == std::string::npos);
    assert(bashWords(script, "free").find("--min-duration") != std::string::npos);
    assert(bashWords(script, "export").find("--no-header") == std::string::npos);
    assert(bashWords(script, "import").find("--from") == std::string::npos);
    assert(bashWords(script, "completion").find("--format") == std::string::npos);
    assert(bashWords(script, "holidays").find("--country") != std::string::npos);
    assert(bashWords(script, "holidays").find("--db") == std::string::npos);
    assert(bashWords(script, "add").find("--shift") == std::string::npos);
    assert(bashWords(script, "edit").find("--shift") != std::string::npos);
    assert(bashWords(script, "duplicate").find("--title") == std::string::npos);
}

void testZsh() {
    const std::string script = completionScript("zsh");
    assert(!script.empty());
    assert(script.rfind("#compdef klend_ario_cli\n", 0) == 0);
    assert(script.find("_arguments") != std::string::npos);
    assert(script.find("verbs=(add edit") != std::string::npos);
    assert(script.back() == '\n');
    expectVerbs(script);

    assert(zshCase(script, "list").find("--days") == std::string::npos);
    assert(zshCase(script, "agenda").find("--days") != std::string::npos);
    assert(zshCase(script, "next").find("--sort") == std::string::npos);
    assert(zshCase(script, "stats").find("--limit") == std::string::npos);
    assert(zshCase(script, "stats").find("--min-duration") == std::string::npos);
    assert(zshCase(script, "free").find("--min-duration") != std::string::npos);
    assert(zshCase(script, "export").find("--no-header") == std::string::npos);
    assert(zshCase(script, "import").find("--from") == std::string::npos);
    assert(zshCase(script, "completion").find("--format") == std::string::npos);
    assert(zshCase(script, "holidays").find("--country") != std::string::npos);
    assert(zshCase(script, "holidays").find("--db") == std::string::npos);
    assert(zshCase(script, "add").find("--shift") == std::string::npos);
    assert(zshCase(script, "edit").find("--shift") != std::string::npos);
    assert(zshCase(script, "duplicate").find("--title") == std::string::npos);
}

void testFish() {
    const std::string script = completionScript("fish");
    assert(!script.empty());
    assert(script.rfind("# fish completion for klend_ario_cli\n", 0) == 0);
    assert(script.find("complete -c klend_ario_cli -f\n") != std::string::npos);
    assert(script.find("-n '__fish_use_subcommand'") != std::string::npos);
    assert(script.back() == '\n');
    expectVerbs(script);

    assert(script.find("-n '__fish_seen_subcommand_from stats' -l limit") == std::string::npos);
    assert(script.find("-n '__fish_seen_subcommand_from stats' -l min-duration") ==
           std::string::npos);
    assert(script.find("-n '__fish_seen_subcommand_from next' -l sort") == std::string::npos);
    assert(script.find("-n '__fish_seen_subcommand_from list' -l days") == std::string::npos);
    assert(script.find("-n '__fish_seen_subcommand_from agenda' -l days") != std::string::npos);
    assert(script.find("-n '__fish_seen_subcommand_from free' -l min-duration") !=
           std::string::npos);
    assert(script.find("-n '__fish_seen_subcommand_from holidays' -l country") !=
           std::string::npos);
    assert(script.find("-n '__fish_seen_subcommand_from holidays' -l db") == std::string::npos);
    assert(script.find("-n '__fish_seen_subcommand_from completion' -l format") ==
           std::string::npos);
    assert(script.find("-n '__fish_seen_subcommand_from import' -l from") == std::string::npos);
}

void testUnknownShell() {
    assert(completionScript("tcsh").empty());
    assert(completionScript("").empty());
}

} // namespace

int main() {
    testBash();
    testZsh();
    testFish();
    testUnknownShell();
    std::printf("completion_test: all assertions passed\n");
    return 0;
}
