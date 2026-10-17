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
| Schema & storage (`db`, `schema`) | `core/tests/` | `src/db.cc src/schema.cc` + `sqlite3` | in-memory DB; `schema_test` uses real temp files for read-only/encrypted open |
| Query filters/sort/pagination (`EventQuery` → SQL) | `core/tests/query_test.cc` | `src/db.cc src/schema.cc src/query.cc src/event.cc` + `sqlite3` | in-memory DB |
| Pairwise conflicts (`findConflicts`) | `core/tests/conflict_test.cc` | `src/conflict.cc src/event.cc` | none |
| Free-slot intervals (`freeSlots`) | `core/tests/free_test.cc` | `src/free.cc src/event.cc` | none |
| Summary counts (`computeStats`) | `core/tests/stats_test.cc` | `src/stats.cc src/conflict.cc src/event.cc` | none |
| Doctor checks (`diagnose`, `integrityCheck`) | `core/tests/diagnostics_test.cc` | `src/diagnostics.cc src/db.cc src/schema.cc src/query.cc src/conflict.cc src/sync.cc` + `sqlite3` | in-memory DB |
| Interchange export (`exportEvents`) | `core/tests/export_test.cc` | `src/export.cc src/event.cc` | none |
| Interchange import (`parseImportedEvents`, `Db::importEvents`) | `core/tests/import_test.cc` | `src/import.cc src/export.cc src/db.cc src/schema.cc src/event.cc src/query.cc` + `sqlite3` | in-memory DB |
| Shell completion scripts | `cli/tests/completion_test.cc` | `completion.cc` only | none |
| CLI argument parsing | `cli/tests/` | `args.cc` only | none |
| CLI rendering (text/table/json/csv/tsv) | `cli/tests/format_test.cc` | `format.cc src/event.cc` | none |
| CLI end-to-end (`add`/`edit`/`duplicate`/`list`/`today`/`week`/`next`/`agenda`/`search`/`conflicts`/`free`/`alarms`/`stats`/`doctor`/`export`/`import`/`completion`/`show`/`rm`/`holidays`) | `cli/tests/` | `klend_ario_core` | temp DB (not needed by `holidays`/`completion`) |
| Holiday adapter (`std::optional` → `arc::Result`) | `core/tests/holidays_test.cc` | `src/holidays.cc` + `pe_holiday_calc` | none |
| Week geometry, DST-length days, overlap clusters | `gui/tests/week_layout_test.cc` | `src/week_layout.cc` | none |
| Local civil days (Lima, Berlin 23/25 h) | `gui/tests/local_time_test.cc` | `src/local_time.cc` | pinned `TZ`, no network |
| Chrome metrics + palette | `gui/tests/theme_test.cc` | `src/theme.cc src/week_layout.cc` | none |
| The weekly view's draw ops (colours, labels, borders, tail, now-line) | `gui/tests/scene_test.cc` | `src/scene.cc src/theme.cc src/week_layout.cc src/local_time.cc` + `core/src/event.cc` | none; fake `TextMetrics` |
| DB → week model (one query, label, holidays) | `gui/tests/week_store_test.cc` | `src/week_store.cc` + core sources | in-memory DB, pinned `TZ` |
| Headless view screenshots | `gui/tools/capture_main.cc` | the built binary | fixture weeks; **not** CTest |
| MCP JSON parser/serializer | `mcp/tests/json_test.cc` | `mcp/src/json.cc` | none |
| MCP JSON-RPC dispatch + five tools | `mcp/tests/server_test.cc` | `mcp/src/json.cc mcp/src/server.cc` + core | in-memory DB |
| MCP end-to-end (real stdio) | `demo/mcp_smoke.py` | the built binary | throwaway temp DB; **not** CTest |

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
  `startOfDayUtc` is the same floor as a public core function; the CLI's default
  range and the MCP server's default `from` both call it.
- `week` is the Sunday that starts the current week; `next-week`/`last-week`
  move by seven days (the product week starts Sunday, `merged.md` §1).
- `friday` on a Friday is **today**, not a week later; otherwise it is the next
  occurrence at 00:00 UTC.
- `today+2h`, `week-1d` and bare `+90m`/`-1d` resolve against `now`.
- Durations are `90m`, `2h`, `1h30m`, `1d`, `1w`, or a bare number of seconds.
  Overflow and unknown units are rejected (`Ok`/`nullopt`, never wrapped).

### CLI query semantics

`query_test` pins the query contract over an in-memory database:

- The range is half-open overlap: `start_utc < to` **and** `end_utc > from`.
  An event that began before `from` and is still running is included; an event
  ending exactly at `from` is not. `today`, `week`, `next`, `agenda` and
  `search` all use the same rule through `Db::queryEvents`.
- Text is a literal `instr` over `title + ' ' + notes`, ASCII case-insensitive.
  `%` and `_` are ordinary characters, and `"daily sync"` is one phrase (two
  spaces do not match).
- Importance/urgency bounds are inclusive; filters compose with text, type,
  visibility, sort and pagination.
- Visibility defaults: `list` and `search` see hidden rows; `today`, `week`,
  `next` and `agenda` show visible rows only. `--visible`,
  `--hidden-only` and `--include-hidden` are mutually exclusive overrides.
- Sorts are deterministic: start ascending/descending, importance (ties broken
  by urgency), urgency, updated, and title (`COLLATE NOCASE`). `--limit`/
  `--offset` apply after ordering; `--limit 0` returns no rows.
- `next` is the first event with `end_utc > now`, so an ongoing event wins;
  limit 1 unless `--limit` widens it, and sort is fixed to start-ascending.

The e2e test captures stdout for the query verbs, so a filter that never
reaches core (or a hidden-policy regression) fails there too, not only in
`query_test`.

### CLI intelligence

`conflict_test` and `free_test` pin the two interval analyses:

- Conflicts are pairwise and half-open: `start < other.end && end > other.start`.
  Touching endpoints and zero-length events never conflict; a triple overlap
  yields three pairs. `EventType::Alarm` rows are excluded because they are
  reminders, not blocks.
- Free slots merge busy intervals first, clamp them to `[from, to)`, and only
  emit gaps at least `minDurationSeconds` long. A gap exactly equal to the
  minimum is included; one second short is not. Alarm rows never block, and a
  zero-length event does not remove time.
- Hidden filtering is the caller's job in both functions. The CLI defaults
  `conflicts`/`free` to visible rows and `alarms` to all rows, because hiding is
  a view choice while an alarm is a schedule fact.
- `alarms` reuses `alarmsIn` (T-offset then T=0, sorted by fire time). The
  trigger carries `eventStartUtc` so the text formatter can print `T-10m`
  without a second lookup.

### CLI summary and health

`stats_test` pins the summary over a fixed clock; `diagnostics_test` pins the
doctor over an in-memory database:

- `upcoming` is `start > now`, `ongoing` is `start <= now < end`, `past` is
  `end <= now`. A zero-length event exactly at `now` is **past**, not ongoing.
- `scheduledSeconds` is a SUM of durations (overlaps double-count);
  `conflictPairs` uses the same rule as `conflicts`. Alarm rows count in totals
  but not in conflict pairs.
- `earliestStartUtc`/`latestEndUtc` are empty for an empty set, not zero.
- Doctor checks are all `Ok` on a fresh in-memory DB: integrity `ok`, schema
  `v1`, `modified` "no writes yet", sync skipped, events `0`. A write makes the
  marker `Ok`; a missing/malformed marker is a Warn; a wrong schema version is
  a Fail.
- The CLI doctor is read-only and exits `1` on any warn/fail. The e2e test
  creates a fake `*.sync-conflict-*` file beside a relative temp database and
  asserts the report names it and the exit code changes; this is what caught
  the empty-parent bug in `syncConflictCopies`.

### CLI export and completion

`export_test` pins the interchange bytes without a database:

- CSV is RFC 4180: a field with `,`, `"` or a newline is quoted, embedded quotes
  are doubled, and the header is the full 12-column export shape.
- JSONL is one compact object per line; all strings are escaped, and timestamps
  are ISO-8601 UTC (the `created_utc`/`updated_utc` fields are integers).
- ICS uses CRLF, escapes `\ ; ,` and newlines, folds at 75 octets (backing off a
  UTF-8 continuation byte), and emits `VALARM` only for a non-zero offset. The
  sign flips correctly: `alarm_offset = 600` is `TRIGGER:-PT10M`, `-300` is
  `TRIGGER:PT5M`.
- An empty export is still a valid document: a CSV header, zero JSONL lines, or
  an empty VCALENDAR.

`completion_test` pins the three scripts and their verb list. All shells are
generated from one per-verb table in `completion.cc`, so the test checks the
negative cases as well: `list` has no `--days`, `next` has no `--sort`, `stats`
has no `--limit` or `--min-duration`, and `holidays` has no `--db`. If a verb or
flag is added, update the table and the test together.

`import_test` pins the parser and the transaction:

- CSV is RFC 4180: quoted commas/quotes/newlines, reordered header by name,
  BOM/CRLF, and strict errors for a bad quote, missing/unknown column, wrong
  field count, bad instant/type/range, or empty input.
- JSONL is one flat object per line; escapes decode, unknown scalar keys are
  ignored, and nested values, duplicate keys and wrong types fail with a line.
- Export → parse round-trips every field for both formats.
- `Db::importEvents` appends with fresh ids by default, replaces in one
  transaction, preserves ids only when asked, and rolls a collision back.
- The CLI parses before opening the database, so `--dry-run` and a bad file
  leave no target file behind; an empty file is an error.

Read-only access and option scoping are covered at both layers:

- `schema_test` opens a real file read-only, asserts an insert fails, asserts a
  missing path is not created, and asserts a zero-byte file is not migrated.
  Encrypted read-only open works with the right key and fails with the wrong one.
- `e2e_test` asserts `doctor`/`export` do not create a missing database, a
  zero-byte database stays zero bytes and reports a schema fail, and both
  commands work on a `chmod 0444` file.
- `args_test` asserts every mutation flag is rejected on unrelated verbs, plus
  `--country`/`--year` (holidays), `--days` (agenda), and output flags on
  `help`/`completion`. Legitimate uses are asserted alongside them.

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

### MCP surface

`server_test` pins the model-facing contract, not only the wire:

- `initialize` returns `2025-06-18`, `capabilities.tools.listChanged = false`,
  and non-empty `instructions` (UTC, half-open ranges, hidden rows, alarms).
- `tools/list` carries titles and annotations (`readOnlyHint` for list/get,
  `destructiveHint` for update/delete, `idempotentHint` for create=false), real
  enums/bounds, and `additionalProperties: false`.
- Inputs are strict: unknown arguments, string ids, string booleans, out-of-range
  importance/urgency, `end < start`, `end` + `duration`, and an empty update are
  tool errors with actionable text; an unknown tool is JSON-RPC `-32602`.
- `list_events` is an `EventQuery`: search is a trimmed literal substring, type
  and visibility filter, sort is deterministic, and `limit 0` returns nothing.
- Every successful data call has `structuredContent`; single-event and id
  results equal their text JSON, while `list_events` wraps the text array as
  `{"events": [...]}` because the 2025-era field is an object.
- A read-only `Server` advertises only list/get and denies create.
- `delete_event` remains idempotent for a missing id (the core/CLI contract).

`demo/mcp_smoke.py` drives the real binary over newline-delimited JSON-RPC,
asserts the same surface, and cross-checks the MCP write through the CLI on the
same database. It is the host-shaped check, not a CTest suite. It also covers
what the in-process test cannot: malformed frame resilience, string ids and
batching, `--key-file` and `KLEND_ARIO_KEY` encryption, read-only on a
`chmod 0444` database, the `read,write` middle tier, the missing-database
contract, option errors, and a strict "no output after stdin close" check.

`holidays_test` tests the **boundary**, not the calendar: the child's own suites
already pin the Peru dates. A parent `ctest` also runs the child's seven suites
(`date`, `easter`, `holiday`, `registry`, `custom_provider`, `pe`,
`pe_official_calendar`), because the root CMake adds the submodule with tests ON.
That is why the parent currently reports 32 suites, not 25. The CLI's
`holidays` verb is covered for parsing and dispatch in `cli/tests/`; the dates
themselves stay pinned by the child suites and `holidays_test`.

**Narrow is the point.** A pure-logic test should link its one source file and
nothing else — if it needs more, the source picked up a dependency it should not
have. Matrix_Player's `variants_test` states this explicitly; keep the same
property here.

## Demo showcase

`demo/klend_ario_demo.py` is an **acceptance showcase**, not a CTest suite: it
drives the built binaries against a throwaway database and narrates the public
surface. It must exit `0` before a release, but `ctest` remains the correctness
gate. It is not registered with CTest and must never write to the real `state`
database; the contract is [`DEMO.md`](DEMO.md).

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
