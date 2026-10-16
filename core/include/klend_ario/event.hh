#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

// ---------------------------------------------------------------------------
// Priority and urgency, the two independent visual axes (merged.md §4):
//
//   importance -> hue        violet (0) .. red (100)
//   urgency    -> border     thin (0) .. thick (100)
//
// Same hue either way; urgency never introduces a second colour channel, which
// is the whole point of the Eisenhower encoding. These are PURE so the weekly
// view and any later surface (print, MCP preview) colour an event identically.
//
// The endpoints are compile-time constants for now; they move into `meta` when
// they become user-tunable, which is why callers take a number and not a struct.
// ---------------------------------------------------------------------------
inline constexpr double kImportanceLowHue = 270.0;  // violet
inline constexpr double kImportanceHighHue = 0.0;   // red

double hueForImportance(int importance);          // degrees, 0..360
double borderFactorForUrgency(int urgency);        // 0..1

struct EventStyle {
    double hue = kImportanceLowHue; // degrees
    double borderFactor = 0.5;      // 0..1
};
EventStyle styleFor(const Event &event);

// ---------------------------------------------------------------------------
// Calendar arithmetic. The week STARTS SUNDAY (merged.md §1) and every instant
// is UTC. Pure integer math — no <ctime>, no timezone database — so a boundary
// can be asserted exactly.
// ---------------------------------------------------------------------------

// 0 = Sunday .. 6 = Saturday, for the UTC day containing `utcSeconds`.
int weekdayUtc(long long utcSeconds);

// The timestamp of 00:00 UTC on the Sunday that begins the week containing
// `utcSeconds`.
long long startOfWeekUtc(long long utcSeconds);

// The instants at which an event should fire an alarm: the T-offset trigger
// first (when the event carries one), then T=0 (merged.md §6). Ascending.
std::vector<long long> alarmTriggersUtc(const Event &event);

} // namespace klend_ario
