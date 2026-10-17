#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <utility>
#include <vector>

#include "klend_ario/event.hh"
#include "klend_ario/free.hh"

using namespace klend_ario;

namespace {

long long at(const char *text) {
    const auto value = parseIso8601(text);
    assert(value.has_value());
    return *value;
}

Event makeEvent(const char *start, const char *end, EventType type = EventType::Event) {
    Event event;
    event.startUtc = at(start);
    event.endUtc = at(end);
    event.type = type;
    return event;
}

void expectSlots(const std::vector<FreeSlot> &slots,
                 std::initializer_list<std::pair<const char *, const char *>> expected) {
    assert(slots.size() == expected.size());
    size_t index = 0;
    for (const auto &[start, end] : expected) {
        assert(slots[index].startUtc == at(start));
        assert(slots[index].endUtc == at(end));
        ++index;
    }
}

constexpr long long kMin30 = 30 * 60;

void testEmptyCalendarIsOneSlot() {
    expectSlots(freeSlots({}, at("2026-10-16T09:00:00Z"), at("2026-10-16T12:00:00Z"), kMin30),
                {{"2026-10-16T09:00:00Z", "2026-10-16T12:00:00Z"}});
}

void testGapBeforeBetweenAndAfter() {
    const std::vector<Event> events = {
        makeEvent("2026-10-16T09:30:00Z", "2026-10-16T10:00:00Z"),
        makeEvent("2026-10-16T10:45:00Z", "2026-10-16T11:15:00Z"),
    };
    expectSlots(freeSlots(events, at("2026-10-16T09:00:00Z"), at("2026-10-16T12:00:00Z"),
                          kMin30),
                {{"2026-10-16T09:00:00Z", "2026-10-16T09:30:00Z"},
                 {"2026-10-16T10:00:00Z", "2026-10-16T10:45:00Z"},
                 {"2026-10-16T11:15:00Z", "2026-10-16T12:00:00Z"}});
}

void testTouchingEventsLeaveNoGap() {
    const std::vector<Event> events = {
        makeEvent("2026-10-16T09:30:00Z", "2026-10-16T10:00:00Z"),
        makeEvent("2026-10-16T10:00:00Z", "2026-10-16T10:30:00Z"),
    };
    expectSlots(freeSlots(events, at("2026-10-16T09:00:00Z"), at("2026-10-16T12:00:00Z"),
                          kMin30),
                {{"2026-10-16T09:00:00Z", "2026-10-16T09:30:00Z"},
                 {"2026-10-16T10:30:00Z", "2026-10-16T12:00:00Z"}});
}

void testOverlapsMerge() {
    const std::vector<Event> events = {
        makeEvent("2026-10-16T09:30:00Z", "2026-10-16T10:30:00Z"),
        makeEvent("2026-10-16T10:00:00Z", "2026-10-16T11:00:00Z"),
        makeEvent("2026-10-16T10:15:00Z", "2026-10-16T10:45:00Z"),
    };
    expectSlots(freeSlots(events, at("2026-10-16T09:00:00Z"), at("2026-10-16T12:00:00Z"),
                          kMin30),
                {{"2026-10-16T09:00:00Z", "2026-10-16T09:30:00Z"},
                 {"2026-10-16T11:00:00Z", "2026-10-16T12:00:00Z"}});
}

void testMinDurationFiltersShortGaps() {
    const std::vector<Event> events = {
        makeEvent("2026-10-16T09:30:00Z", "2026-10-16T10:00:00Z"),
    };
    // The 30-minute gap is exactly the minimum: included.
    expectSlots(freeSlots(events, at("2026-10-16T09:00:00Z"), at("2026-10-16T12:00:00Z"), kMin30),
                {{"2026-10-16T09:00:00Z", "2026-10-16T09:30:00Z"},
                 {"2026-10-16T10:00:00Z", "2026-10-16T12:00:00Z"}});
    // One second longer drops it.
    expectSlots(
        freeSlots(events, at("2026-10-16T09:00:00Z"), at("2026-10-16T12:00:00Z"), kMin30 + 1),
        {{"2026-10-16T10:00:00Z", "2026-10-16T12:00:00Z"}});
    // The whole 3-hour range cannot fit inside either gap.
    assert(freeSlots(events, at("2026-10-16T09:00:00Z"), at("2026-10-16T12:00:00Z"),
                     3 * 60 * 60)
               .empty());
}

void testRangeIsClamped() {
    // An event covering the whole range leaves nothing.
    assert(freeSlots({makeEvent("2026-10-16T08:00:00Z", "2026-10-16T13:00:00Z")},
                     at("2026-10-16T09:00:00Z"), at("2026-10-16T12:00:00Z"), kMin30)
               .empty());

    // Events entirely outside the range do not affect it.
    expectSlots(freeSlots({makeEvent("2026-10-16T08:00:00Z", "2026-10-16T09:00:00Z")},
                          at("2026-10-16T09:00:00Z"), at("2026-10-16T12:00:00Z"), kMin30),
                {{"2026-10-16T09:00:00Z", "2026-10-16T12:00:00Z"}});
    expectSlots(freeSlots({makeEvent("2026-10-16T12:00:00Z", "2026-10-16T13:00:00Z")},
                          at("2026-10-16T09:00:00Z"), at("2026-10-16T12:00:00Z"), kMin30),
                {{"2026-10-16T09:00:00Z", "2026-10-16T12:00:00Z"}});

    // An event entering from the left is clamped at the range start.
    expectSlots(freeSlots({makeEvent("2026-10-16T07:00:00Z", "2026-10-16T09:30:00Z")},
                          at("2026-10-16T09:00:00Z"), at("2026-10-16T12:00:00Z"), kMin30),
                {{"2026-10-16T09:30:00Z", "2026-10-16T12:00:00Z"}});
}

void testZeroLengthAndAlarmRowsDoNotBlock() {
    const std::vector<Event> events = {
        makeEvent("2026-10-16T10:00:00Z", "2026-10-16T10:00:00Z"),
        makeEvent("2026-10-16T09:30:00Z", "2026-10-16T10:30:00Z", EventType::Alarm),
    };
    expectSlots(freeSlots(events, at("2026-10-16T09:00:00Z"), at("2026-10-16T12:00:00Z"),
                          kMin30),
                {{"2026-10-16T09:00:00Z", "2026-10-16T12:00:00Z"}});
}

void testSleepBlocksWhenPassed() {
    const std::vector<Event> events = {
        makeEvent("2026-10-16T09:30:00Z", "2026-10-16T10:00:00Z", EventType::Sleep),
    };
    expectSlots(freeSlots(events, at("2026-10-16T09:00:00Z"), at("2026-10-16T12:00:00Z"),
                          kMin30),
                {{"2026-10-16T09:00:00Z", "2026-10-16T09:30:00Z"},
                 {"2026-10-16T10:00:00Z", "2026-10-16T12:00:00Z"}});
}

void testEmptyRangeOrNonPositiveMinimum() {
    const long long from = at("2026-10-16T09:00:00Z");
    const long long to = at("2026-10-16T12:00:00Z");
    assert(freeSlots({}, from, from, kMin30).empty());
    assert(freeSlots({}, to, from, kMin30).empty());
    assert(freeSlots({}, from, to, 0).empty());
    assert(freeSlots({}, from, to, -1).empty());
}

} // namespace

int main() {
    testEmptyCalendarIsOneSlot();
    testGapBeforeBetweenAndAfter();
    testTouchingEventsLeaveNoGap();
    testOverlapsMerge();
    testMinDurationFiltersShortGaps();
    testRangeIsClamped();
    testZeroLengthAndAlarmRowsDoNotBlock();
    testSleepBlocksWhenPassed();
    testEmptyRangeOrNonPositiveMinimum();
    std::printf("free_test: all assertions passed\n");
    return 0;
}
