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
          "conflicts", "free", "alarms", "stats", "doctor", "export", "completion", "show",
          "rm", "holidays", "help"}) {
        assert(script.find(std::string(verb)) != std::string::npos);
    }
}

void testBash() {
    const std::string script = completionScript("bash");
    assert(!script.empty());
    assert(script.rfind("# bash completion for klend_ario_cli\n", 0) == 0);
    assert(script.find("complete -F _klend_ario_cli klend_ario_cli\n") != std::string::npos);
    assert(script.find("--format") != std::string::npos);
    assert(script.back() == '\n');
    expectVerbs(script);
}

void testZsh() {
    const std::string script = completionScript("zsh");
    assert(!script.empty());
    assert(script.rfind("#compdef klend_ario_cli\n", 0) == 0);
    assert(script.find("_arguments") != std::string::npos);
    assert(script.find("verbs=(add edit") != std::string::npos);
    assert(script.back() == '\n');
    expectVerbs(script);
}

void testFish() {
    const std::string script = completionScript("fish");
    assert(!script.empty());
    assert(script.rfind("# fish completion for klend_ario_cli\n", 0) == 0);
    assert(script.find("complete -c klend_ario_cli -f\n") != std::string::npos);
    assert(script.find("-n '__fish_use_subcommand'") != std::string::npos);
    assert(script.back() == '\n');
    expectVerbs(script);
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
