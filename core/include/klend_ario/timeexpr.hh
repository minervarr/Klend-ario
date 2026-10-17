#pragma once

#include <optional>
#include <string_view>

// User-facing INSTANT and DURATION expressions, as pure functions of a clock.
//
// The CLI lets a person type `--from today --to week+7d` instead of a full
// ISO-8601 stamp, and `--duration 1h30m` instead of arithmetic. That parsing is
// here, not in cli/, for the same reason every other rule is: it is calendar
// logic with edge cases (leap days, month rollover, a negative epoch) and it can
// be asserted exactly. It is UTC-only and uses no <ctime>, no timezone database,
// and no network (essentialAgent/AGENTS.md §2.10).
//
// Every relative form is resolved against `nowUtc`, which the caller supplies.
// That is what makes the result testable: "now" is a parameter, not a call to
// the system clock buried inside the parser.
//
// Accepted instants:
//   ISO-8601 UTC           2026-10-16T09:00:00Z (seconds optional)
//   ISO-8601 date          2026-10-16            (00:00:00 UTC)
//   now | today | yesterday | tomorrow
//   week | next-week | last-week                (Sunday-based, merged.md §1)
//   sunday .. saturday                          (next occurrence, today counts;
//                                                at 00:00 UTC)
//   +DURATION | -DURATION                       (relative to now)
//   <keyword>+DURATION | <keyword>-DURATION     (e.g. today+2h, week-1d)
//
// Accepted durations: one or more <int><unit> groups, unit in s, m, h, d, w,
// or a bare integer meaning seconds. Examples: 90m, 2h, 1h30m, 1d, 604800.
//
// Anything else is std::nullopt. Being strict is deliberate: guessing at an
// ambiguous date is how a calendar silently stores the wrong instant.
namespace klend_ario {

std::optional<long long> parseInstantExpr(std::string_view text, long long nowUtc);

std::optional<long long> parseDurationExpr(std::string_view text);

} // namespace klend_ario
