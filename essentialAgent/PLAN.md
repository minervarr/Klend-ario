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
| 4 | GUI — weekly 16:9 view (`vk_canvas` + `app_shell`) | **in progress** — geometry done; renderer pending SDK |
| 5 | MCP server — local, read/write, minimal tool surface, tiers | **done** |
| 6 | Alarms — libnotify + PipeWire / Android native | **done** (notify; PipeWire sound is a named gap) |
| 7 | Android platform layer + storage permissions | **not started** — needs the NDK/SDK (see `android/README.md`) |

**MVP = 0–2.** Detail for phases 3–7 is in [`../merged.md`](../merged.md).

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
- [x] `manifest.json` — `name: klend_ario`, version, description, platforms,
      `language: C++17`, `build: {linux: scripts/build.sh}`.
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
- [x] The child's seven suites run through the parent's CTest (18 suites
      total), so a parent `ctest` also protects the library.
- [ ] Android NDK build of the child — open; no NDK/SDK in this environment.

---

## Status log

Newest first. One line per meaningful change.

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
