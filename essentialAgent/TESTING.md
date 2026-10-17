# TESTING — how this repo proves it works

*The project is written to be audited. A claim without a test is an opinion.*

---

## Doctrine

- **Plain `assert()`, no framework.** Every test source starts with
  `#undef NDEBUG` so the asserts survive an optimized build.
- **One executable per suite**, with a `main()` that calls test functions and
  prints a final line, e.g. `printf("db_test: all assertions passed\n")`.
- **Registered with CTest**, so `ctest` lists every suite and prints a single
  pass/fail summary. That is how we "keep track".
- **Debug-only, desktop-only.** Tests are never built into a Release app.
- **Deterministic and fixture-free where possible.** Pure functions are asserted
  exactly; the database is tested against an **in-memory** handle (`:memory:`),
  not a file on disk.

This mirrors the convention already used by `archive_engine` (`paths_test`,
`walk_test`) and Matrix_Player (`variants_test`, `stats_test`).

## How to add a test

1. Write `core/tests/<name>_test.cc`:

   ```cpp
   #undef NDEBUG
   #include <cassert>
   #include <cstdio>

   #include "klend_ario/event.hh"

   static void testRoundTrip() {
       // one behaviour per function; assert the fact, comment the *why* when it
       // is not obvious.
       assert(klend_ario::parseIso8601("2026-10-16T09:00Z").has_value());
   }

   int main() {
       testRoundTrip();
       std::printf("event_test: all assertions passed\n");
       return 0;
   }
   ```

2. Register it in `core/CMakeLists.txt` (the helper is defined in
   `cmake/TestHelpers.cmake`):

   ```cmake
   klend_ario_add_test(event_test
       SOURCES tests/event_test.cc src/event.cc
       LIBS    arc_util)
   ```

   `klend_ario_add_test` creates the executable, links Debug-only, adds
   `-Wall`, and calls `add_test()` so CTest sees it.

3. Run just that one:

   ```bash
   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
   ninja -C build event_test
   ctest --test-dir build -R event_test --output-on-failure
   ```

## What to test

| Layer | Test lives in | Links | Fixture |
|---|---|---|---|
| Pure model / parsing (`event`, ISO 8601, priority→colour) | `core/tests/` | the one source file, nothing else | none |
| UTC / day arithmetic (`parseIso8601`, `formatIso8601`, half-open day) | `core/tests/event_test.cc` | `src/event.cc` | none |
| User-facing instants/durations (`today`, `week+7d`, `90m`) | `core/tests/timeexpr_test.cc` | `src/timeexpr.cc src/event.cc` | none |
| Schema & storage (`db`, `schema`) | `core/tests/` | `src/db.cc src/schema.cc` + `sqlite3` | in-memory DB |
| CLI argument parsing | `cli/tests/` | `args.cc` only | none |
| CLI rendering (text/table/json/csv/tsv) | `cli/tests/format_test.cc` | `format.cc src/event.cc` | none |
| CLI end-to-end (`add`/`edit`/`duplicate`/`list`/`show`/`rm`/`holidays`) | `cli/tests/` | `klend_ario_core` | temp DB (not needed by `holidays`) |
| Holiday adapter (`std::optional` → `arc::Result`) | `core/tests/holidays_test.cc` | `src/holidays.cc` + `pe_holiday_calc` | none |

### Time and day model

`event_test` pins the vectors from the 2026-10-16 notes:

- 80,000 s after midnight UTC = **22:13:20**.
- 81,000 s after midnight UTC = **22:30:00**.
- The gap is **1,000 s = 16 min 40 s**.
- 23:59:59 is **86,399 s** after midnight; the next midnight is **86,400 s**
  later and belongs to the next day. The store is half-open `[0, 86400)` and has
  whole seconds only — do not assert microseconds until a column exists.
- No live NTP/network in tests, ever. NTP is a runtime frontend concern.

### CLI time expressions

`timeexpr_test` pins the grammar the CLI exposes; `now` is a parameter, so no
test reads the clock:

- `today` is 00:00 UTC of the day containing `now`; `tomorrow`/`yesterday` are
  ±86,400 s. Pre-epoch input floors to the day, never truncates toward zero.
- `week` is the Sunday that starts the current week; `next-week`/`last-week`
  move by seven days (the product week starts Sunday, `merged.md` §1).
- `friday` on a Friday is **today**, not a week later; otherwise it is the next
  occurrence at 00:00 UTC.
- `today+2h`, `week-1d` and bare `+90m`/`-1d` resolve against `now`.
- Durations are `90m`, `2h`, `1h30m`, `1d`, `1w`, or a bare number of seconds.
  Overflow and unknown units are rejected (`Ok`/`nullopt`, never wrapped).

### CLI rendering

`format_test` asserts the actual bytes for every format, so the user-visible
contract is tested without capturing stdout:

- text is one line per event (`id  start  end  title  [type]  [hidden]`);
- json is the object used by the MCP/tooling, and `list` emits an array;
- csv quotes a field containing `,`/`"`/newline and doubles embedded quotes;
- tsv escapes `\`, tab and newline so a record stays on one line;
- table right-aligns numeric columns and keeps widths across a headerless run.

An end-to-end test still drives parsing → command → core → SQLite with a temp
DB. Exit codes are the contract there: `0` ok, `1` runtime error, `2` usage.

`holidays_test` tests the **boundary**, not the calendar: the child's own suites
already pin the Peru dates. A parent `ctest` also runs the child's seven suites
(`date`, `easter`, `holiday`, `registry`, `custom_provider`, `pe`,
`pe_official_calendar`), because the root CMake adds the submodule with tests ON.
That is why the parent currently reports 20 suites, not 11. The CLI's
`holidays` verb is covered for parsing and dispatch in `cli/tests/`; the dates
themselves stay pinned by the child suites and `holidays_test`.

**Narrow is the point.** A pure-logic test should link its one source file and
nothing else — if it needs more, the source picked up a dependency it should not
have. Matrix_Player's `variants_test` states this explicitly; keep the same
property here.

## Habits

- A bug fix arrives with the assertion that would have caught it.
- Assert the *edge* — the case that looks wrong and is right — with a comment.
  Those are the tests that are worth reading later.
- When a test must touch the filesystem, prefer `arc_fs`'s helpers and clean up;
  but first ask whether the behaviour can be tested purely.

## Anti-patterns

- No sleep/retry/flaky timing tests.
- No network. Ever, in this repo's tests.
- No test that only proves the compiler works (`assert(1 == 1)`).
