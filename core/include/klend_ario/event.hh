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
//
// A civil day is HALF-OPEN: [00:00:00, next 00:00:00), exactly 86,400 whole
// seconds. 23:59:59 is the last named second (86,399 s after midnight), not the
// day's length; the next midnight belongs to the next day. The model has no
// sub-second field, so microseconds stay an input/display concern until a
// column exists. From the 2026-10-16 notes: 80,000 s = 22:13:20,
// 81,000 s = 22:30:00, and the gap is 1,000 s = 16 min 40 s.
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
// Style -> pixels. The conversion lives in core, not in a frontend, for the
// same reason the hue does: the weekly view and the printed week (and any
// future preview) must colour an event identically, and a second HSL->RGB
// implementation is how those surfaces start to disagree.
//
// Saturation and lightness are fixed rather than derived from the event, so
// the hue axis is the only thing that varies: at this lightness every point of
// the violet->red sweep keeps enough contrast for a label, and urgency stays a
// border-only signal. `textColorOn` picks the label colour from the fill's
// linear luminance, which is what keeps the yellow-green middle of the sweep
// readable without inventing a second palette.
// ---------------------------------------------------------------------------
inline constexpr double kStyleSaturation = 0.72;
inline constexpr double kStyleLightness = 0.46;

struct Rgb {
    unsigned char r = 0;
    unsigned char g = 0;
    unsigned char b = 0;
};

Rgb rgbForStyle(const EventStyle &style);
Rgb textColorOn(Rgb fill); // near-black on light fills, near-white on dark ones

// ---------------------------------------------------------------------------
// Calendar arithmetic. The week STARTS SUNDAY (merged.md §1) and every instant
// is UTC. Pure integer math — no <ctime>, no timezone database — so a boundary
// can be asserted exactly. UTC is the model; a local zone is a rendering
// concern, and NTP is a runtime frontend concern, never a dependency here.
// ---------------------------------------------------------------------------

// 0 = Sunday .. 6 = Saturday, for the UTC day containing `utcSeconds`.
int weekdayUtc(long long utcSeconds);

// The timestamp of 00:00 UTC on the Sunday that begins the week containing
// `utcSeconds`.
long long startOfWeekUtc(long long utcSeconds);

// The instants at which an event should fire an alarm: the T-offset trigger
// first (when the event carries one), then T=0 (merged.md §6). Ascending.
std::vector<long long> alarmTriggersUtc(const Event &event);

// The week counter shown at the top of the weekly view (merged.md §5.2): the
// calendar year and a 1-based week number. Sundays start a week; week 1 is the
// week containing 1 January and may be short, which is the common US reading
// (ISO 8601 would start weeks on Monday and is deliberately NOT used here,
// because the product's week starts Sunday).
struct WeekOfYear {
    int year = 1970;
    int week = 1;
};
WeekOfYear weekOfYearUtc(long long utcSeconds);

// The same counter for a CIVIL date. A local week is not a UTC week: at a
// local Sunday 00:00 an east-of-UTC zone is still on Saturday in UTC, so
// `weekOfYearUtc(instant)` would name the previous week. The GUI resolves
// local midnights itself and passes the date it is actually drawing.
WeekOfYear weekOfYearFromCivilDate(int year, int month, int day);

} // namespace klend_ario
