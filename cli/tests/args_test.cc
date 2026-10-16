#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "args.hh"

using namespace klend_ario::cli;

namespace {

Args parse(std::vector<std::string_view> tokens) { return parseArgs(tokens); }

void testNoArgsIsHelp() {
    assert(parse({}).verb == Verb::Help);
    assert(parse({"-h"}).verb == Verb::Help);
    assert(parse({"--help"}).verb == Verb::Help);
    assert(parse({"help"}).verb == Verb::Help);
}

void testVerbs() {
    assert(parse({"add"}).verb == Verb::Add);
    assert(parse({"list"}).verb == Verb::List);
    assert(parse({"show"}).verb == Verb::Show);
    assert(parse({"rm"}).verb == Verb::Remove);
    assert(parse({"remove"}).verb == Verb::Remove);
}

void testUnknownCommand() {
    const Args a = parse({"frobnicate"});
    assert(a.verb == Verb::None);
    assert(!a.error.empty());
}

void testAddOptions() {
    const Args a = parse({"add", "--title", "Standup", "--start", "2026-10-16T09:00:00Z",
                          "--end", "2026-10-16T09:30:00Z", "--importance", "80", "--urgency",
                          "10", "--type", "sleep", "--alarm-offset", "-600", "--notes", "daily",
                          "--hidden", "--json"});
    assert(a.error.empty());
    assert(a.verb == Verb::Add);
    assert(a.title == "Standup");
    assert(a.start == "2026-10-16T09:00:00Z");
    assert(a.end == "2026-10-16T09:30:00Z");
    assert(a.importance == 80);
    assert(a.urgency == 10);
    assert(a.type == "sleep");
    assert(a.alarmOffset == -600);
    assert(a.notes == "daily");
    assert(a.hidden);
    assert(a.json);
}

void testDefaults() {
    const Args a = parse({"add", "--title", "X", "--start", "s", "--end", "e"});
    assert(a.importance == 50);
    assert(a.urgency == 50);
    assert(a.type == "event");
    assert(a.alarmOffset == 0);
    assert(a.notes.empty());
    assert(!a.hidden);
    assert(!a.json);
    assert(a.dbPath.empty());
}

void testDbAndRangeFlags() {
    const Args a = parse({"list", "--db", "/tmp/x.db", "--from", "2026-01-01T00:00:00Z",
                          "--to", "2026-01-08T00:00:00Z"});
    assert(a.error.empty());
    assert(a.verb == Verb::List);
    assert(a.dbPath == "/tmp/x.db");
    assert(a.from == "2026-01-01T00:00:00Z");
    assert(a.to == "2026-01-08T00:00:00Z");
}

void testKeyFileFlag() {
    const Args a = parse({"list", "--key-file", "/tmp/secret.key"});
    assert(a.error.empty());
    assert(a.keyFile == "/tmp/secret.key");
    // The flag needs a value.
    assert(!parse({"list", "--key-file"}).error.empty());
}

void testPositionalId() {
    const Args show = parse({"show", "42"});
    assert(show.error.empty());
    assert(show.id == 42);

    const Args rm = parse({"rm", "7"});
    assert(rm.error.empty());
    assert(rm.id == 7);
}

void testUsageErrors() {
    // Missing value.
    assert(!parse({"add", "--title"}).error.empty());
    // Non-numeric integer.
    assert(!parse({"add", "--importance", "x"}).error.empty());
    // Unknown option.
    assert(!parse({"list", "--nope"}).error.empty());
    // A positional where the verb does not take one.
    assert(!parse({"list", "stray"}).error.empty());
    // A non-numeric id.
    assert(!parse({"show", "abc"}).error.empty());
}

} // namespace

int main() {
    testNoArgsIsHelp();
    testVerbs();
    testUnknownCommand();
    testAddOptions();
    testDefaults();
    testDbAndRangeFlags();
    testKeyFileFlag();
    testPositionalId();
    testUsageErrors();
    std::printf("args_test: all assertions passed\n");
    return 0;
}
