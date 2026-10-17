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
    assert(parse({"edit"}).verb == Verb::Edit);
    assert(parse({"update"}).verb == Verb::Edit);
    assert(parse({"duplicate"}).verb == Verb::Duplicate);
    assert(parse({"copy"}).verb == Verb::Duplicate);
    assert(parse({"list"}).verb == Verb::List);
    assert(parse({"today"}).verb == Verb::Today);
    assert(parse({"week"}).verb == Verb::Week);
    assert(parse({"next"}).verb == Verb::Next);
    assert(parse({"agenda"}).verb == Verb::Agenda);
    assert(parse({"search"}).verb == Verb::Search);
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

    const Args edit = parse({"edit", "9"});
    assert(edit.error.empty());
    assert(edit.verb == Verb::Edit && edit.id == 9);

    const Args duplicate = parse({"duplicate", "11"});
    assert(duplicate.error.empty());
    assert(duplicate.verb == Verb::Duplicate && duplicate.id == 11);
}

void testPositionalTitle() {
    const Args a = parse({"add", "Standup", "--start", "today", "--duration", "30m"});
    assert(a.error.empty());
    assert(a.verb == Verb::Add);
    assert(a.titleGiven && a.title == "Standup");
    assert(a.startGiven && a.start == "today");
    assert(a.durationGiven && a.duration == "30m");

    // A second positional would be ambiguous: --title or the bare form, never
    // both.
    assert(!parse({"add", "One", "Two"}).error.empty());
    assert(!parse({"add", "--title", "One", "Two"}).error.empty());
}

void testGivenFlags() {
    // The difference that makes `edit` possible: absent means "leave alone".
    const Args untouched = parse({"edit", "5"});
    assert(untouched.error.empty());
    assert(!untouched.titleGiven);
    assert(!untouched.startGiven);
    assert(!untouched.endGiven);
    assert(!untouched.durationGiven);
    assert(!untouched.typeGiven);
    assert(!untouched.importanceGiven);
    assert(!untouched.urgencyGiven);
    assert(!untouched.alarmOffsetGiven);
    assert(!untouched.notesGiven);
    assert(!untouched.hiddenGiven);
    assert(!untouched.shiftGiven);

    const Args set = parse({"edit", "5", "--title", "New", "--importance", "50", "--urgency", "0",
                            "--alarm-offset", "60", "--hidden", "--notes", "n"});
    assert(set.titleGiven && set.title == "New");
    assert(set.importanceGiven && set.importance == 50);
    assert(set.urgencyGiven && set.urgency == 0);
    assert(set.alarmOffsetGiven && set.alarmOffset == 60);
    assert(set.hiddenGiven && set.hidden);
    assert(set.notesGiven && set.notes == "n");

    const Args unhide = parse({"edit", "5", "--unhide"});
    assert(unhide.hiddenGiven && !unhide.hidden);

    const Args clear = parse({"edit", "5", "--clear-notes"});
    assert(clear.notesGiven && clear.clearNotes && clear.notes.empty());
}

void testShiftAndFormat() {
    const Args shift = parse({"edit", "9", "--shift", "-30m"});
    assert(shift.error.empty());
    assert(shift.shiftGiven && shift.shift == "-30m");

    const Args format = parse({"list", "--format", "csv", "--no-header"});
    assert(format.error.empty());
    assert(format.format == "csv");
    assert(format.noHeader);
    // The name is validated by the command; parsing only carries it.
    assert(parse({"list", "--format", "xml"}).error.empty());
}

void testHelpTopics() {
    const Args named = parse({"help", "edit"});
    assert(named.verb == Verb::Help && named.helpTopic == "edit");

    const Args flag = parse({"edit", "--help"});
    assert(flag.verb == Verb::Help && flag.helpTopic == "edit");

    const Args shortFlag = parse({"add", "-h"});
    assert(shortFlag.verb == Verb::Help && shortFlag.helpTopic == "add");
}

void testIntegerOverflowRejected() {
    assert(!parse({"add", "--importance", "99999999999999999999"}).error.empty());
    assert(!parse({"add", "--urgency", "-99999999999999999999"}).error.empty());
}

void testQueryFlags() {
    const Args a = parse({"list", "--from", "today", "--to", "week", "--search", "sync",
                          "--type", "sleep", "--min-importance", "60", "--max-importance", "90",
                          "--min-urgency", "10", "--max-urgency", "80", "--sort", "importance",
                          "--limit", "5", "--offset", "2", "--visible"});
    assert(a.error.empty());
    assert(a.fromGiven && a.from == "today");
    assert(a.toGiven && a.to == "week");
    assert(a.searchGiven && a.search == "sync");
    assert(a.typeGiven && a.type == "sleep");
    assert(a.minImportanceGiven && a.minImportance == 60);
    assert(a.maxImportanceGiven && a.maxImportance == 90);
    assert(a.minUrgencyGiven && a.minUrgency == 10);
    assert(a.maxUrgencyGiven && a.maxUrgency == 80);
    assert(a.sortGiven && a.sort == "importance");
    assert(a.limitGiven && a.limit == 5);
    assert(a.offsetGiven && a.offset == 2);
    assert(a.visibleOnly && !a.hiddenOnly && !a.includeHidden);

    const Args defaults = parse({"list"});
    assert(defaults.limit == -1);
    assert(!defaults.limitGiven);
    assert(defaults.offset == 0);
    assert(defaults.sort == "start");
    assert(!defaults.sortGiven);
    assert(!defaults.searchGiven);

    // Zero is a valid limit; a negative one is rejected at parse time.
    assert(parse({"list", "--limit", "0"}).limit == 0);
    assert(!parse({"list", "--limit", "-1"}).error.empty());
    assert(!parse({"list", "--offset", "-1"}).error.empty());

    const Args agenda = parse({"agenda", "--days", "3"});
    assert(agenda.daysGiven && agenda.days == 3);
    assert(!parse({"agenda", "--days", "-1"}).error.empty());

    const Args hiddenOnly = parse({"list", "--hidden-only"});
    assert(hiddenOnly.hiddenOnly && !hiddenOnly.visibleOnly);
    const Args includeHidden = parse({"list", "--include-hidden"});
    assert(includeHidden.includeHidden && !includeHidden.visibleOnly);

    // The three visibility flags describe one choice.
    assert(!parse({"list", "--visible", "--hidden-only"}).error.empty());
    assert(!parse({"list", "--visible", "--include-hidden"}).error.empty());
    assert(!parse({"list", "--hidden-only", "--include-hidden"}).error.empty());
}

void testSearchPositional() {
    const Args positional = parse({"search", "daily sync"});
    assert(positional.error.empty());
    assert(positional.verb == Verb::Search);
    assert(positional.searchGiven && positional.search == "daily sync");

    const Args flag = parse({"search", "--search", "ana"});
    assert(flag.error.empty());
    assert(flag.searchGiven && flag.search == "ana");

    // No query at all parses; the command reports the usage error.
    const Args empty = parse({"search"});
    assert(empty.error.empty());
    assert(!empty.searchGiven);

    // One query, not several positionals, and not both forms.
    assert(!parse({"search", "one", "two"}).error.empty());
    assert(!parse({"search", "one", "--search", "two"}).error.empty());
    assert(!parse({"search", "--search", "one", "two"}).error.empty());
    assert(!parse({"search", "--search", "one", "--search", "two"}).error.empty());
}

void testQueryOnlyFlagsRejectedOnMutation() {
    assert(!parse({"add", "--limit", "2"}).error.empty());
    assert(!parse({"add", "--search", "x"}).error.empty());
    assert(!parse({"edit", "5", "--visible"}).error.empty());
    assert(!parse({"show", "5", "--from", "today"}).error.empty());
    assert(!parse({"rm", "5", "--sort", "start"}).error.empty());
    assert(!parse({"holidays", "--days", "3"}).error.empty());

    // The reverse trap: a mutation flag on a query must not be silently
    // ignored (especially --importance, which is not the filter).
    assert(!parse({"list", "--importance", "80"}).error.empty());
    assert(!parse({"list", "--title", "x"}).error.empty());
    assert(!parse({"search", "x", "--start", "today"}).error.empty());
    assert(!parse({"list", "--hidden"}).error.empty());
    assert(!parse({"list", "--unhide"}).error.empty());
}

void testHolidaysVerb() {
    const Args defaults = parse({"holidays"});
    assert(defaults.error.empty());
    assert(defaults.verb == Verb::Holidays);
    assert(defaults.country == "PE");
    assert(defaults.year == 0);
    assert(!defaults.yearGiven); // the command resolves "now"

    const Args a = parse({"holidays", "--country", "pe", "--year", "2026", "--json"});
    assert(a.error.empty());
    assert(a.verb == Verb::Holidays);
    assert(a.country == "pe");
    assert(a.year == 2026);
    assert(a.yearGiven);
    assert(a.json);

    // The year is validated by the command, but it must still parse as an int,
    // and holidays takes no positional argument.
    assert(!parse({"holidays", "--year", "x"}).error.empty());
    assert(!parse({"holidays", "--country"}).error.empty());
    assert(!parse({"holidays", "2026"}).error.empty());
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
    testPositionalTitle();
    testGivenFlags();
    testShiftAndFormat();
    testHelpTopics();
    testIntegerOverflowRejected();
    testQueryFlags();
    testSearchPositional();
    testQueryOnlyFlagsRejectedOnMutation();
    testHolidaysVerb();
    testUsageErrors();
    std::printf("args_test: all assertions passed\n");
    return 0;
}
