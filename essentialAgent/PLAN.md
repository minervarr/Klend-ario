# PLAN — phases, the scaffold, and status

*What gets built, in what order, and what exists right now. Update the checkboxes
as work lands; this file is the tracker.*

---

## Phase table

| Phase | Deliverable | Status |
|---|---|---|
| **0** | **Scaffold** — submodule, CMake+Ninja, pyz tool, docs, first test | **done** |
| 1 | Core — SQLite schema + storage + model, with tests | **done** |
| 2 | **CLI MVP** — `add` / `list` / `show` / `rm` | **done** — CLI frozen 2026-10-17 |
| 3 | Encryption (`SQLite3MultipleCiphers`) + Syncthing sync policy | **done** |
| 4 | GUI — weekly 16:9 view (`vk_canvas` + `app_shell`) | **4a done** — read-only window + headless capture; edit UI next |
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
- [x] **Stage 6 — portability.**
  - [x] **6a — export and completion.** Pure `core/export` for CSV, JSONL and
        one-way ICS (UTC VEVENTs, `VALARM`, folding), plus `completion
        bash|zsh|fish`. Export is all-time/all-rows by default and writes to
        stdout.
  - [x] **6b — import.** CSV/JSONL parsing and a transactional
        `Db::importEvents`: append by default, `--replace`, `--dry-run`, and
        optional id preservation. Parse-before-write means a bad row cannot
        leave a partial import.
  - [x] **6c — audit close-out.** `Db::openReadOnly`/`openEncryptedReadOnly`;
        `doctor` and `export` never create, migrate or write a file. Options are
        allowlisted per verb, completion is generated from one per-verb table,
        and `--version` is available.

The CLI is frozen as of 2026-10-17. GUI Phase 4 resumes unless the order changes
again — see the status log.

---

## Demo showcase (shipped 2026-10-17)

A permanent, stdlib-only Python consumer of the built binaries. It is **not a
phase** and does not move one: it showcases the frozen CLI surface, exercises
the MCP server and alarm daemon, and proves a run starts from zero every time.
The contract is [`DEMO.md`](DEMO.md).

- [x] **Spec** — `DEMO.md`: clean-run contract, section list, verification.
- [x] **Skeleton** — `demo/klend_ario_demo.py`: binary discovery, temp root,
      one `--db` injection helper, narration, `--pause`/`--keep`/`--no-color`,
      cleanup on exit/`SIGINT`/`SIGTERM`.
- [x] **CLI story** — setup, create, read, change, intelligence,
      health/interop, Python parsing, completion, encryption.
- [x] **MCP + alarm** — JSON-RPC tour and a one-poll alarm; auto-skipped when
      those binaries are not built.
- [x] **Close-out** — `demo/README.md`, root `README.md` pointer, two clean
      runs, commit via `./git_wrapper`.
- [x] **Verified** — three clean runs, `--pause`, `--keep`, `--release`; no
      `klend_ario_demo-*` directory left behind and no `state` database touched.

The CLI is frozen; the demo is a consumer. A gap found here is a bug in the
docs or in the demo, not a reason to add a verb.

---

## MCP AI-readiness (2026-10-17)

Phase 5 shipped a working wire; this pass makes the surface legible to a model
and safe for a human. It stays inside the five-tool decision in `merged.md` §7
and adds no CLI or schema surface.

- [x] **Contract** — `mcp/README.md` documents the transport, tiers, DB/key
      handling, OpenCode config, tool semantics, error model and protocol
      policy before the code changes.
- [x] **Instructions** — `initialize` carries UTC/half-open/hidden/alarm/edit
      guidance, because OpenCode feeds `instructions` to the model.
- [x] **Metadata** — tool `title`s and `annotations` (read-only, destructive,
      idempotent, closed-world), real JSON Schema enums/bounds and
      `additionalProperties: false`; richer descriptions.
- [x] **Strict inputs** — no coercion; `end >= start`; `hidden` is a real bool;
      ids are positive integers; unknown arguments name the allowed set.
- [x] **Core time and queries** — `parseInstantExpr`/`parseDurationExpr`/
      `startOfDayUtc`; `list_events` is an `EventQuery` with search, type,
      visibility, sort, limit and offset. `startOfDayUtc` moved from a CLI copy
      into core and is pinned by `timeexpr_test`.
- [x] **Tier honesty** — `tools/list` advertises only enabled tiers; a
      read-only deployment opens the database read-only.
- [x] **Results** — compact JSON in text plus `structuredContent` (object
      results match the text; lists wrap their array as `{"events": [...]}`);
      output schemas deferred as a recorded decision.
- [x] **Proof** — `server_test` covers metadata, strict inputs, query
      semantics, tiers and structured content; `demo/mcp_smoke.py` drives the
      real binary including encryption, read-only on a write-protected DB,
      protocol resilience and option/startup errors; `opencode.jsonc` registers
      the server and `opencode mcp list` sees it.
- [x] **Records** — this section, `MEMORY.md` §8, `TESTING.md`, `DEMO.md`, the
      root README and `mcp/README.md` are updated in the same change.

---

## Phase 4 — the weekly view (4a shipped 2026-10-17)

Phase 4 paused on 2026-10-17 for the CLI freeze, then resumed. The pure geometry
shipped first; 4a adds the scene, the window and a way to look at the result
without a display. Read-only on purpose: the CLI and the MCP server are the
write paths, so the first GUI cannot corrupt a calendar.

- [x] **Geometry** — `week_layout`: frame margins, Sunday-first columns,
      proportional boxes, hatched tail. Pure, no GPU.
- [x] **DST-length days** — a day is passed as `[startUtc, endUtc)`. The old
      `dayStart + 86,400` was wrong for 23/25-hour days.
- [x] **Overlap clusters** — `layoutDay` splits concurrent events into
      side-by-side slots; touching intervals stay sequential.
- [x] **Local days at the edge** — `local_time` resolves the one timezone query
      and hands layout/scene UTC instants plus the civil date.
- [x] **Scene** — `buildWeekScene` emits draw ops + hit targets: colours
      (`rgbForStyle`), urgency borders, height-tiered labels, truncation via an
      injected `TextMetrics`, tail hatch, today tint, now-line, header, empty
      state. `scene_test` asserts it with no GPU.
- [x] **Week model** — `loadWeekModel`: one `EventQuery`, the week label
      (including the New-Year week rule) and Peru holidays.
- [x] **Window** — `klend_ario_gui`: `vk_canvas_wayland` + `app_shell`, arrow
      week navigation, `T`/`H`/`Esc`, mouse on the header buttons, read-only DB,
      one-second `Db::dataVersion()` poll so CLI/MCP writes appear live.
- [x] **Text** — `RasterFont` (per-size FreeType coverage atlas) through the
      shared text pipeline: `text_inputs` collects the codepoint/size set before
      the scene, `ensureGlyphs` + `initMsdf` bake it, and `draw()` gets the quads
      fifth. The legacy `Font` path is a missing-font fallback only. See
      [`GUI.md`](GUI.md) §5 for why (the outline path's winding fill is `SS=1`).
- [x] **Headless capture** — `klend_ario_capture` renders fixture weeks through
      `VK_EXT_headless_surface` to PNG, so the view can be reviewed (and
      regressions seen) with no compositor.
- [x] **Wiring** — `-DKLEND_BUILD_GUI=ON`; shaders via `VceShaders`, `slangc`
      from the cache/`$VULKAN_SDK`/`PATH`; fonts+shaders copied beside the exe.
- [x] **Performance pass (2026-10-20)** — demand-driven repaint (the timer is a
      poll, not a paint), midnight reload, device/present-queue selection in
      `vk_canvas`, opt-in windowed mode in `App_shell`, `Vsync`/2 images.
      Measured before/after per [`GUI_PERFORMANCE.md`](GUI_PERFORMANCE.md):
      0 frames while browsing, 1/minute on the current week, 0.00 % CPU.
      Landed before 4b because editing will add more redraw paths.
- [ ] **4b — editing** — create/edit/delete from the GUI with CLI-parity fields
      (`*Given` semantics), then alarms hooked into the window.
- [ ] **4c — overlay / print** — still post-MVP per `merged.md` §5.4–5.5.

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
- [x] The child's seven suites run through the parent's CTest (28 suites
      total), so a parent `ctest` also protects the library.
- [ ] Android NDK build of the child — open; no NDK/SDK in this environment.

---

## Status log

Newest first. One line per meaningful change.

- **GUI performance pass — demand-driven repaint (2026-10-20).** The weekly
  view no longer repaints once a second. `gui/src/refresh.*` is a pure
  None/Redraw/Reload decision over the clock, the shown week and
  `Db::dataVersion()`; `WeekView` arms a frame only for a data change, a local
  midnight, a week rollover, or a 60 s now-line step, and does nothing while
  browsing a week that contains no "now". Text bakes on a dirty flag keyed to
  the renderer extent, and `onHostResized()` calls `notifyResized()` so a resize
  is not one frame stale. Measured Debug: browsing 30 s = 0 frames, current week
  70 s = 3 frames (two startup), CPU 0 ticks. `vk_canvas` now requires
  `VK_KHR_swapchain` and a graphics+present queue family, prefers a non-CPU
  device, uses the chosen family instead of literal 0, and logs
  `Device: <name> (<type>)`. `App_shell` gained a pre-init
  `setWindowOptions()`; the view asks for 1280x720 windowed (`--fullscreen`
  restores the old mode). New `refresh_test` (10 cases) brings ctest to 34.
  Captures unchanged (200 edge colours). Child commits: vk_canvas `6e30e7f`,
  App_shell `86f8b38`; pins advanced. Decisions and the before/after table live
  in [`GUI_PERFORMANCE.md`](GUI_PERFORMANCE.md).

- **Build tool knows the GUI (2026-10-17).** `klend_ario_builder.pyz` now probes
  for the weekly view's build prerequisites (`slangc`/`$VULKAN_SDK` and the
  Wayland/xkb `pkg-config` modules) and passes `-DKLEND_BUILD_GUI=ON` when they
  are present, so `build --release` produces `build_release/gui/klend_ario_gui`
  without the user knowing the flag. `--gui`/`--no-gui` override, the menu asks,
  and the CMake option still defaults to OFF for machines without the SDK. The
  zipapp is rebuilt; `scripts/tests` covers the flag, the mutual exclusion and
  the confirm prompt (13 passing). No production code changed.

- **Phase 4a — the weekly view exists and renders (2026-10-17).** Added the pure
  scene (`gui/src/scene.*`), local-day resolution (`local_time.*`), theme/metrics,
  the DB→view bridge (`week_store.*`), the Canvas adapter, the `klend_ario_gui`
  window (read-only, arrow/T/H/Esc, header hit targets, 1 s `Db::dataVersion`
  poll so CLI/MCP writes appear live) and `klend_ario_capture` (headless PNG
  scenarios). `week_layout` now takes `[startUtc, endUtc)` (DST days are 23/25 h)
  and gains `layoutDay` overlap clusters; core gains `rgbForStyle`,
  `textColorOn`, `weekOfYearFromCivilDate` and `Db::dataVersion`. Five new
  suites (`local_time_test`, `theme_test`, `scene_test`, `week_store_test`, plus
  expanded `week_layout_test`) bring the parent to 32. Verified by building
  `-DKLEND_BUILD_GUI=ON`, four headless captures, and a live Wayland window that
  showed a CLI edit within a second. No submodule change.

- **GUI text: RasterFont + readable floor (2026-10-18).** The weekly view's text
  moved off the legacy `Font` outline path, whose winding fill is `SS=1` and
  therefore 1-bit (a capture crop had exactly two colours while the SDF boxes
  were smooth). It now uses `RasterFont`: `text_inputs` collects the codepoint
  and integer-size set before the scene, `ensureGlyphs` bakes true FreeType
  coverage into an R8 atlas (1455 cells, 4 MB, 11 ms for the mixed-week capture),
  `initMsdf` uploads it, and `draw()` receives the quads fifth. Also added the
  generated `kMinReadableTextSizePx` clamp (16 px for `ui.otf`, passed from the
  GUI so `klend_ario_view` stays pure) and a 0.30 em descent allowance in the
  text clip. New `text_inputs_test`; `theme_test` covers the floor. Verified at
  1920x1080 and 1280x720 (hundreds of edge colours, no clipped `p` tails) and in
  a live 1200x700 window. No submodule change; the decisions live in
  [`GUI.md`](GUI.md).

- **MCP smoke hardened (2026-10-17).** `demo/mcp_smoke.py` now covers 66 checks:
  protocol resilience (malformed/whitespace frames, string ids, batching,
  unknown methods), query visibility/search/sort/limit, update duration
  semantics, the default “now”/range, `--key-file` and `KLEND_ARIO_KEY`
  encryption, read-only on a `chmod 0444` database, the `read,write` middle
  tier, and option/startup errors. Its line reader has a deadline and a frame
  cap, `--keep` prints the temp path on failure, every one-shot child uses the
  cleaned environment and an explicit `--db`, and trailing stdout is now
  strictly rejected. Companion fix: the MCP prints the CLI’s encrypted-database
  hint on open failure and no longer blames a supplied wrong key on read-only
  mode. No submodule change.

- **MCP AI-readiness (2026-10-17).** Hardened Phase 5's surface without adding
  tools: `initialize.instructions`, tool titles/annotations, strict argument
  validation (`end >= start`, real booleans/integers, unknown-argument errors),
  core time expressions and durations, `list_events` as an `EventQuery`
  (search/type/visibility/sort/limit/offset), tier-filtered `tools/list`, a
  read-only SQLite open, and `structuredContent` alongside the compact text.
  `startOfDayUtc` moved from the CLI into `core/timeexpr` and is tested. Added
  `mcp/README.md`, wired `opencode.jsonc`, and extended `server_test` and
  `demo/mcp_smoke.py`; 28 suites pass and the live OpenCode client now sees the
  server's tools. Protocol stays 2025-06-18; 2026-07-28 is a recorded future
  adoption. No submodule change.

- **Demo showcase shipped (2026-10-17).** Added `demo/klend_ario_demo.py` and
  `demo/README.md`: a stdlib-only, narrated tour of all 21 CLI verbs, the five
  MCP tools and a one-poll alarm, with a throwaway database per run. Verified
  with three clean runs plus `--pause`, `--keep` and `--release`; no temporary
  directory was left behind and the real `state` database was never opened.

- **Demo showcase planned (2026-10-17).** Added `essentialAgent/DEMO.md` and
  wired it into `AGENTS.md`/`README.md` before writing code: a permanent,
  stdlib-only Python consumer of the built binaries, with one throwaway
  database per run, JSON for values, and expected-failure assertions. It is a
  showcase and smoke test, not a CTest suite. No production code changed.

- **CLI hardening, Stage 6c — audit close-out (2026-10-17).** Added
  `Db::openReadOnly`/`openEncryptedReadOnly`; `doctor` and `export` no longer
  create, migrate or write a missing/empty/read-only database. Read-only views
  with an explicit `--db` now require an existing file. Replaced the broad
  mutation-option check with per-verb allowlists, scoped `--country`/`--year`,
  `--days` and `--no-header`, and rejected output flags on `help`/`completion`.
  Completion is generated from one per-verb table for bash/zsh/fish. Added
  `--version`, help aliases (`update`/`copy`/`remove`), and a help
  short-circuit. Tests cover missing/empty/read-only databases, option
  scoping, and completion negatives; still 28 suites. No submodule change.

- **CLI hardening, Stage 6b — import (2026-10-17).** Added pure
  `core/import` CSV/JSONL parsers (RFC 4180, quoted newlines, reordered headers,
  BOM; flat JSONL with escapes) and transactional `Db::importEvents`. Append is
  the default with fresh ids; `--replace`, `--preserve-ids` and `--dry-run` are
  explicit. The whole file is parsed before the database is opened, and empty
  input is an error, so a truncated restore cannot wipe data. New `import_test`
  plus e2e round-trips bring the parent to 28 suites. No submodule change.

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
