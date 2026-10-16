#pragma once

#include <optional>
#include <string>
#include <string_view>

// The calendar's model, as a value.
//
// Everything in this header is PURE: no sqlite, no arc_fs, no OS. That is what
// lets event_test link event.cc and nothing else, and it is the property to
// protect (essentialAgent/TESTING.md) — if this file ever needs another
// include to compile, the model has picked up a dependency it should not have.

namespace klend_ario {

// The kinds of thing the weekly view distinguishes. `sleep` and `wake` are the
// blocks the view can HIDE (see Event::hidden); `alarm` is a reminder anchored
// to another event's start (see Event::alarmOffset).
enum class EventType { Event, Sleep, Wake, Alarm };

const char *eventTypeName(EventType type);
bool eventTypeFromName(std::string_view name, EventType &out);

// One row in `events`. Timestamps are Unix seconds, UTC: the model stores an
// INSTANT, not a wall-clock reading. Where that instant is drawn in a local
// week is a rendering question for a later phase.
struct Event {
    long long id = 0; // 0 until inserted
    std::string title;
    long long startUtc = 0;
    long long endUtc = 0;
    EventType type = EventType::Event;
    int importance = 50; // 0..100 -> hue (violet -> red)
    int urgency = 50;    // 0..100 -> border thickness
    int alarmOffset = 0; // seconds before start; 0 = no T-offset alarm
    bool hidden = false; // true = hidden in the weekly view (sleep blocks)
    std::string notes;
    long long createdUtc = 0;
    long long updatedUtc = 0;
};

// ISO-8601 UTC, the interchange format the CLI (and, later, the MCP server)
// speaks:
//     "2026-10-16T09:00:00Z"    seconds shown
//     "2026-10-16T09:00Z"       seconds omitted
// std::nullopt for anything else. The parser is strict on purpose: a silently
// reinterpreted date is worse than a rejected one.
std::optional<long long> parseIso8601(std::string_view text);
std::string formatIso8601(long long utcSeconds);

// Current Unix time in seconds. "Now" lives in the model rather than being
// reached for at each call site, so there is one thing to fake and one place
// to reason about.
long long utcNowSeconds();

} // namespace klend_ario
