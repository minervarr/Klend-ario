# PLAN — phases, the scaffold, and status

*What gets built, in what order, and what exists right now. Update the checkboxes
as work lands; this file is the tracker.*

---

## Phase table

| Phase | Deliverable | Status |
|---|---|---|
| **0** | **Scaffold** — submodule, CMake+Ninja, pyz tool, docs, first test | **done** |
| 1 | Core — SQLite schema + storage + model, with tests | **done** |
| 2 | **CLI MVP** — `add` / `list` / `show` / `rm` | **done** |
| 3 | Encryption (`SQLite3MultipleCiphers`) + Syncthing sync policy | **done** |
| 4 | GUI — weekly 16:9 view (`vk_canvas` + `app_shell`) | **paused** — geometry done; CLI is the priority (2026-10-17) |
| 5 | MCP server — local, read/write, minimal tool surface, tiers | **done** |
| 6 | Alarms — libnotify + PipeWire / Android native | **done** (notify; PipeWire sound is a named gap) |
| 7 | Android platform layer + storage permissions | **deferred** — desktop-first (2026-10-16); see `REQUIREMENTS.md` |
| 8 | Notes companion — notes cross-referenced to events | **not started** — post-MVP |
| 9 | Sleep scheduler — 10 h automatic window, dynamic visibility | **not started** — post-MVP |
| 10 | G-SHOCK sync — sleep/wake alarms over BLE | **not started** — research, optional |
| — | Storage benchmark — SQLite vs JSON gate | **open** — before any storage change |

**MVP = 0–2.** Detail for phases 3–7 is in [`../merged.md`](../merged.md).

**Desktop-first.** Phase 7 is deferred, not cancelled. No Android/JNI/NDK work
until the desktop product is complete and that phase is explicitly reopened.
Phases 8–10 come from the 2026-10-16 notes and are **not** permitted to move the
current phase.

---

## Captured requirements (2026-10-16)

Source: [`REQUIREMENTS.md`](REQUIREMENTS.md). These are recorded, not built.

- [x] Holiday generation is the C++17 port of the original Rust code —
      `libs/firstparty/pe-holiday-calc`, consumed through `klend_ario/holidays.hh`.
- [x] Desktop-first scope recorded — Android/Phase 7 is deferred.
- [x] UTC is the stored time base; local zones are display-only.
- [x] Half-open day model `[0, 86400)` — already what `event.cc` does;
      `event_test` now pins 80,000 s = 22:13:20, 81,000 s = 22:30:00, and
      Δ 1,000 s = 16 min 40 s.
- [ ] Notes companion — note ↔ event reference by id (Phase 8).
- [ ] Sleep scheduler — 10 h default, auto-generated dynamic block (Phase 9).
- [ ] G-SHOCK sleep/wake sync — research feasibility first (Phase 10).
- [ ] NTP runtime time source — frontend/runtime, no core dependency.
- [ ] Storage benchmark — SQLite vs JSON; no rewrite before a measured win.

---

## CLI hardening (before GUI, started 2026-10-17)

The desktop CLI is the product surface until the GUI is worth resuming. The
work is staged so each layer is useful and tested on its own; logic stays in
`core/`, rendering stays pure and in `cli/src/format.cc`.

- [x] **Stage 1 — output foundation.** `--format text|table|json|csv|tsv`
      (`--json` remains an alias), `--no-header`, `help <verb>` and
      `<verb> --help`. Rendering moved out of `printf` calls into pure
      `format.cc` functions, so `format_test` asserts the bytes.
- [x] **Stage 2 — edit and field ergonomics.** `edit <id>` (partial update via
      `*Given` flags), `duplicate <id>`, `--duration`, `--shift`,
      `--clear-notes`, `--hidden`/`--unhide`, and a positional `add` title.
- [x] **Stage 3 — relative time.** Pure `core/timeexpr` grammar: `now`,
      `today`/`tomorrow`/`yesterday`, `week`/`next-week`/`last-week`,
      `sunday..saturday`, `today+2h`, `+90m`, and durations `90m`/`1h30m`/`1d`.
- [x] **Stage 4 — query power.** `EventQuery` + `Db::queryEvents` (one
      parameterized SQL builder; `listEvents` delegates to it). `today`, `week`,
      `next`, `agenda`, `search`; filters `--type`, `--search`,
      `--min/max-importance`, `--min/max-urgency`; visibility
      `--visible`/`--hidden-only`/`--include-hidden`; `--sort`, `--limit`,
      `--offset`. Literal substring search; no FTS or migration.
- [x] **Stage 5 — intelligence.**
  - [x] **5a — conflicts, free, alarms.** Pure `conflict`/`free` interval
        analyses plus an `alarms` verb over the existing trigger core.
        `EventType::Alarm` rows are reminders and do not occupy time; hidden
        rows still alarm.
  - [x] **5b — stats, doctor.** Summary counts (`computeStats`) and a read-only
        health report (`diagnose`): SQLite integrity, schema version,
        `modified_utc`, Syncthing copies, interval sanity. Doctor exits 1 on
        warn/fail.
- [ ] **Stage 6 — portability.**
  - [x] **6a — export and completion.** Pure `core/export` for CSV, JSONL and
        one-way ICS (UTC VEVENTs, `VALARM`, folding), plus `completion
        bash|zsh|fish`. Export is all-time/all-rows by default and writes to
        stdout.
  - [ ] **6b — import.** CSV/JSONL parsing and a transactional
        `Db::importEvents`: append by default, `--replace`, `--dry-run`, and
        optional id preservation.

Stage 6b is planned, not built. GUI Phase 4 resumes after these unless the
order changes again — see the status log.

---

## Phase 0 — the exact scaffold

Everything below is created **before feature code**, so a fresh session can build
and test immediately.

### Repo plumbing

- [x] `.gitmodules` — register `libs/firstparty/archive_engine` →
      `https://github.com/minervarr/archive_engine` (became a real gitlink
      pinning `c44bf97`).
- [x] `.gitignore` — `build/`, `build_*/`, `__pycache__/`, `*.py[cod]`,
      `scripts/*.pyz`, `compile_commands.json`, editor junk, Python venv.
- [x] `manifest.json` — `name: klend_ario`, version, description,
      `platforms: [linux]`, `deferred_platforms: [android]`, `language: C++17`,
      `time: unix-seconds-utc`, `day: [0,86400)`,
      `build: {linux: scripts/build.sh}`.
- [x] `README.md` — build, run, test; points at `essentialAgent/`.

### Build system

- [x] `CMakeLists.txt` — `project(klend_ario)`, C++17, options
      (`KLEND_BUILD_TESTS` ON, `KLEND_BUILD_GUI` OFF), `include(CTest)`,
      `add_subdirectory(libs/firstparty/archive_engine/core archive_engine_build)`,
      `add_subdirectory(core)`, `add_subdirectory(cli)`.
      archive_engine module flags: `ARC_BUILD_FS=ON`, everything else `OFF`.
- [x] `cmake/TestHelpers.cmake` — `klend_ario_add_test(name SOURCES … [INCLUDES …] [LIBS …])`
      → executable + `add_test()`, Debug-only.

### Core (portable, tested)

- [x] `core/CMakeLists.txt` → `klend_ario_core`; links `arc_fs` (ARGS) + `sqlite3`.
- [x] `libs/thirdparty/sqlite3/{sqlite3.c,sqlite3.h}` — 3.46.1 amalgamation, own target.
- [x] `core/include/klend_ario/{db.hh,schema.hh,event.hh}`.
- [x] `core/src/{db.cc,schema.cc,event.cc}` + `core/sql/schema.sql` (embedded at configure time).
- [x] `core/tests/{schema_test.cc,event_test.cc}` — first passing tests.

### CLI (the MVP frontend)

- [x] `cli/CMakeLists.txt` → `klend_ario_cli`.
- [x] `cli/src/{main.cc,args.hh,args.cc}`; `cli/tests/args_test.cc`.
- [x] Verbs: `add`, `list`, `show`, `rm`; flags `--db <path>` (default
      `state` dir), `--json`.

### Build tool (Linux-only for now)

- [x] `scripts/build_pyz.py`, `scripts/pyproject.toml`, `scripts/build.sh`.
- [x] `scripts/src/klend_ario_build/{__init__,cli,builder,paths,toolchain,menu,ui}.py`
      (plus `errors.py`, `process.py`, `__main__.py` — see the status log).
- [x] `scripts/tests/` — pytest for the builder (`test_paths`, `test_cli`).

### Placeholders & docs

- [x] `android/`, `gui/` — empty-but-in-tree (README stub).
- [x] `essentialAgent/` — this folder.
- [x] root `AGENTS.md`, `opencode.jsonc`.

---

## Phase 0 acceptance

The scaffold is done when, from a clean checkout:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C build
ctest --test-dir build --output-on-failure      # all pass
./build/cli/klend_ario_cli --help               # prints usage
./scripts/build.sh                              # zipapp drives the same build
```

---

## Holidays in core (first-party library)

- [x] `libs/firstparty/pe-holiday-calc` registered in `.gitmodules` (Codeberg,
      pinned to the child's `master`).
- [x] Root `CMakeLists.txt` adds the module with `PE_HOLIDAY_BUILD_TESTS` ON and
      `PE_HOLIDAY_BUILD_EXAMPLES` OFF; `klend_ario_core` links `pe_holiday_calc`
      PUBLIC (the adapter header exposes its `Holiday`/`Date` types).
- [x] `core/include/klend_ario/holidays.hh` + `core/src/holidays.cc` adapt the
      library's `std::optional` answers to `arc::Result`.
- [x] `core/tests/holidays_test.cc` asserts a known 2026 Peru calendar,
      case/trimmed lookup, the *día no laborable* negative, and the
      unknown-country error.
- [x] The child's seven suites run through the parent's CTest (27 suites
      total), so a parent `ctest` also protects the library.
- [ ] Android NDK build of the child — open; no NDK/SDK in this environment.

---

## Status log

Newest first. One line per meaningful change.

- **CLI hardening, Stage 6a — export/completion (2026-10-17).** Added pure
  `core/export` for CSV, JSONL and ICS. CSV/JSONL carry every stored field; ICS
  is one-way interoperability (UTC VEVENTs, `VALARM`, escaped/folded lines).
  New `export` verb defaults to all time/all rows and stdout; `--format` is
  verb-scoped (`csv|jsonl|ics`). Added static `completion bash|zsh|fish`
  scripts generated by the CLI itself. New `export_test`/`completion_test`
  bring the parent to 27 suites. No submodule change.

- **CLI hardening, Stage 5b — stats/doctor (2026-10-17).** Added
  `core/stats.hh` (`computeStats`: counts by type/visibility,
  upcoming/ongoing/past, summed durations, conflict pairs, span) and
  `core/diagnostics.hh` (`diagnose`: integrity, schema version, `modified_utc`,
  Syncthing conflict copies, interval sanity). `Db::integrityCheck()` wraps
  `PRAGMA integrity_check`. New `stats` and `doctor` verbs; doctor is read-only
  and exits 1 on any warn/fail. Fixed `syncConflictCopies` for a relative
  `--db`: a bare filename has an empty parent, which previously made the
  conflict-copy scan silently list nothing. `stats_test`/`diagnostics_test`
  bring the parent to 25 suites. No submodule change.

- **CLI hardening, Stage 5a — conflicts/free/alarms (2026-10-17).** Added pure
  `core/conflict.hh` (pairwise overlaps; `EventType::Alarm` rows are reminders,
  not blocks; touching/zero-length never conflict) and `core/free.hh` (merged
  busy intervals → gaps, range-clamped, minimum duration). `AlarmTrigger` now
  carries `eventStartUtc`, so the CLI renders `T-10m` / `T=0` directly. New
  verbs `conflicts`, `free`, `alarms`; hidden rows are filtered for conflicts
  and free time, but **included by default for alarms** because hiding is a
  display choice, not a schedule change. New `conflict_test`/`free_test` and
  stdout-capturing e2e coverage bring the parent to 23 suites. No submodule
  change.

- **CLI hardening, Stage 4 — query power (2026-10-17).** Added
  `core/query.hh` (`EventQuery`, `EventSort`, `Visibility`) and
  `Db::queryEvents`: one prepared statement, bound user text, a fixed ORDER BY
  whitelist, and `listEvents` delegating so the overlap rule has one
  implementation. New verbs `today`, `week`, `next`, `agenda`, `search`; shared
  filters `--search`/`--type`/`--min/max-importance`/`--min/max-urgency`,
  visibility `--visible`/`--hidden-only`/`--include-hidden`, and
  `--sort`/`--limit`/`--offset`. Views (`today`/`week`/`next`/`agenda`) default
  to visible rows; `list`/`search` see hidden rows too. Search is a literal
  ASCII case-insensitive substring of title+notes (no FTS, no migration), and
  `next` is the first event with `end_utc > now`, including one already
  underway. `query_test` (21 suites total) pins filtering, sorting, pagination
  and half-open boundaries. No submodule change.

- **CLI hardening, Stages 1–3 (2026-10-17).** The CLI is now the priority;
  GUI Phase 4 is paused, not cancelled. Added a pure rendering layer
  (`cli/src/format.{hh,cc}`, `format_test`) covering text/table/json/csv/tsv and
  `--no-header`; `--format` plus the existing `--json` alias; per-verb help
  (`help add`, `edit --help`). Added `edit <id>` with `*Given` partial updates,
  `duplicate <id>`, `add --duration` (and a positional title), `edit --shift`,
  `--clear-notes`, `--hide`/`--unhide`, and duration-preserving `--start`.
  Added the pure `core/timeexpr` grammar (`timeexpr_test`): ISO/date-only,
  `today`/`tomorrow`/`week`, weekday names, `today+2h`, `+90m`, and durations.
  `list` now defaults to 00:00 UTC today + 7 days. 20 CTest suites pass; the
  new tests fail without the features. No submodule change.

- **Requirements captured from the 2026-10-16 notes.** Added
  `essentialAgent/REQUIREMENTS.md` (verbatim source + normalized requirements)
  and wired it into the manual and OpenCode. Locked **desktop-first** (Android
  deferred), **UTC + half-open days** (`[0, 86400)`), a **10-hour dynamic sleep
  window**, a **notes companion keyed to events**, **G-SHOCK research**, and
  **NTP as runtime-only**. Storage stays **SQLite**: JSON is a benchmark
  candidate, not a decision; encryption stays implemented but is not a gate.
  `event_test` now pins the day-second vectors (80,000 s = 22:13:20, 81,000 s =
  22:30:00, 1,000 s = 16:40). No production logic or submodule changed. See
  `REQUIREMENTS.md`.

- **CLI gained a `holidays` verb.** `klend_ario_cli holidays [--country PE]
  [--year N] [--json]` calls `klend_ario::holidaysFor` through core and prints
  the library's canonical `YYYY-MM-DD Name` lines (or a JSON array with
  `date`/`name`/`non_working`/`observed`). It never opens the database. The
  default year is the current UTC year via `weekOfYearUtc`, and `Args` carries
  `yearGiven` so an explicit `--year 0` is a usage error rather than "now".
  Covered by new `args_test` parsing cases and `e2e_test` dispatch/validation
  cases. No submodule or pin change.

- **pe-holiday-calc integrated into core.** Root CMake adds
  `libs/firstparty/pe-holiday-calc` and `klend_ario_core` links
  `pe_holiday_calc`. `core/include/klend_ario/holidays.hh` exposes
  `arc::Result` queries (`holidaysFor`, `isHoliday`, `holidayOn`), adapting the
  library's `std::optional` answers in `core/src/holidays.cc`.
  `core/tests/holidays_test.cc` proves the 2026 Peru calendar, case/trim lookup,
  the *día no laborable* negative, and the unknown-country error. All 18 CTest
  suites pass (the child's 7 suites now run in the parent too). The NDK build
  remains open; see `MEMORY.md` §4.

- **pe-holiday-calc is now a pinned submodule.** `.gitmodules` registers
  `libs/firstparty/pe-holiday-calc` at
  `https://codeberg.org/minervarr/pe-holiday-calc`, pinned to the child's
  `master`. A fresh recursive clone can now configure without a nested checkout
  standing in for it.

- **Week counter added to core** (`weekOfYearUtc`): the Sunday-based week-of-year
  the weekly view shows (merged.md §5.2), asserted in `event_test`. It is the US
  reading — week 1 contains 1 January and may be short — chosen because the
  product's week starts Sunday, not Monday.
- **Phase 7 planned, not faked.** `android/README.md` now states exactly what
  the platform layer must do (push the private directory into `arc::fs`, request
  the storage permission, link the same `core/`) and that it is unbuilt because
  no NDK/SDK was present.

- **Phase 6 complete — alarms fire.** Core gained the pure scheduling
  (`alarmsIn`/`dueAlarms`/`nextAlarm`, `alarm_test`), and a new frontend
  `alarm/` (targets `klend_ario_alarm`, `klend_ario_alarmd`) polls the calendar
  and delivers each event's T-offset and T=0 triggers. Delivery is a `Notifier`
  seam: libnotify when present (it is here, 0.8.8), else a stderr log; `--notify
  log` forces the log, and a libnotify that cannot initialise now FALLS BACK
  rather than swallowing the alarm. `engine_test` drives the poll window and
  de-duplication with a fake notifier. **PipeWire sound is not implemented** —
  notifications only, and the gap is named in `MEMORY.md` §9.

- **Phase 5 complete — the MCP server exists and works.** New frontend `mcp/`
  (targets `klend_ario_mcp`, `klend_ario_mcp_server`): JSON-RPC over stdio, one
  message per line, the five tools from `merged.md` §7, and read/write/
  destructive permission tiers configurable with `--permissions`. JSON is
  hand-rolled (`mcp/src/json.*`) because the dependency rule allows no JSON
  library. It opens the database through core, with the same `--key-file` /
  `$KLEND_ARIO_KEY` handling as the CLI. Tests: `json_test` (parser/serializer,
  escapes, depth/format errors) and `server_test` (initialize, tools/list, all
  five tools over an in-memory DB, tier denial, protocol errors). Verified by
  hand over real stdio as well.

- **Phase 4 started — the weekly view's geometry is done and tested.**
  `gui/src/week_layout.{hh,cc}` (target `klend_ario_view`) computes the frame
  margins (1cm-equivalent at FHD+, full-bleed below), the seven Sunday-first
  day columns (tiling the width exactly, remainder spread so there is no seam),
  each event's box on a 00:00–24:00 axis with clipping flags, and the hatched
  "nothing else today" tail. It is pure — no Canvas, no OS, no window — so
  `gui/tests/week_layout_test.cc` asserts the whole view with no GPU, which is
  the only way it can be trusted before it is drawn. **The vk_canvas renderer
  is not built yet**: this machine has the Vulkan headers/loader but no
  `slangc` shader compiler and no display, so that half is left for an
  environment that can actually run it.

- **Phase 3 complete.** The plain SQLite amalgamation is replaced by
  **SQLite3MultipleCiphers 2.6.1** (`libs/thirdparty/sqlite3mc/`, checksum
  recorded). `Db::openEncrypted()` runs `PRAGMA cipher='chacha20'` then
  `sqlite3_key()`, so the format is pinned and a wrong passphrase fails at open.
  The CLI gained `--key-file` and honours `$KLEND_ARIO_KEY`; without a key an
  encrypted file fails with a hint rather than looking empty. The sync policy is
  settled in `MEMORY.md` §7: one file, conflict copies **detected not merged**
  (`klend_ario::syncConflictCopies`), and every write stamps `meta.modified_utc`.
  Tests: `schema_test` proves the encrypted file does not start with the SQLite
  magic and that a wrong/absent key fails; `sync_test` pins the conflict-name
  matcher; `e2e_test` drives the encrypted path through the CLI.

- **Phase 2 complete (MVP reached).** The CLI's command layer moved out of
  `main()` into `cli/src/commands.{hh,cc}` so it can be driven directly by a
  test; `main()` is now `argv -> parseArgs -> runCommand`. `add` gained
  `--notes` and `--hidden`. A new `cli/tests/e2e_test.cc` exercises all four
  verbs against a temporary database, through the real parse→dispatch→core
  path, and asserts `kExitOk`/`kExitError`/`kExitUsage`. (`edit`/update is
  still not a CLI verb; `Db::updateEvent` exists and is covered by
  `schema_test` — it gets its user-facing path in Phase 5's MCP surface.)

- **Phase 1 complete.** `core/` now carries the whole model: `updateEvent`
  (bumps `updated_utc`, preserves `created_utc`, errors on a stale id), the
  `meta` key/value API, a real migration policy (schema version in `meta`; a
  database from a NEWER build is refused, not misread), and the pure product
  helpers — `hueForImportance` (violet→red), `borderFactorForUrgency`,
  `styleFor`, `weekdayUtc`/`startOfWeekUtc` (week starts Sunday), and
  `alarmTriggersUtc` (T-offset then T=0). Tests grew to cover all of it;
  `event_test` + `schema_test` remain narrow.

- **Phase 0 complete.** The walking skeleton is green end to end: `cmake -B build
  -G Ninja -DCMAKE_BUILD_TYPE=Debug` → `ninja -C build` → `ctest` (3/3:
  `event_test`, `schema_test`, `args_test`) → `./build/cli/klend_ario_cli --help`
  → `./scripts/build.sh`. The CLI's `add`/`list`/`show`/`rm` already work
  (text + `--json`) over a real SQLite file in the `state` dir, so the whole
  stack is proven early; Phase 2 hardens rather than builds.
- **Build tool added** — `scripts/` (stdlib-only Python zipapp, Linux-only),
  ported from `git_Wrapper`. Files: the seven the plan named, plus `errors.py`
  and `process.py` (the two-level error/exit-code split and single-place
  subprocess echo, both lifted from the template's proven shape) and
  `__main__.py`. `scripts/tests/` has 9 passing pytest assertions.
- **sqlite3 vendored** — 3.46.1 amalgamation copied from `Matrix_Player`
  (Phase 3 will swap in SQLite3MultipleCiphers).
- **archive_engine is a real submodule** — pinned `c44bf97`, registered in
  `.gitmodules`; `ARC_BUILD_FS=ON`, everything else forced `OFF`.
- **Core slice** — `event` (pure model + ISO-8601 UTC + Hinnant civil-date
  arithmetic), `schema` (embedded `schema.sql`), `db` (`arc::Result`-returning
  RAII connection over `:memory:`/file). Schema carries the `merged.md`
  constraints.
- **Known gaps at end of Phase 0** — CLI has no `--hidden`/`--notes`/`--updated`
  yet (Phase 2); `core` has no `updateEvent`. Both are deliberate: Phase 2 is
  where the CLI MVP is hardened.
- *(scaffold created)* — `essentialAgent/` manual, root `AGENTS.md`,
  `opencode.jsonc`.
