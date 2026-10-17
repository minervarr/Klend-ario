# AGENTS — the operating manual

*Rules for writing code in Klend-ario. [`MEMORY.md`](MEMORY.md) says what and
why; this file says how. If a rule here surprises you, read `MEMORY.md` §5 — it
was probably paid for once already.*

---

## 1. Repository map

```
Klend-ario/
├── AGENTS.md                     # root pointer (auto-loaded) -> this folder
├── opencode.jsonc                # wires essentialAgent/ into OpenCode
├── essentialAgent/               # the agent manual (this folder)
│   └── REQUIREMENTS.md           # captured 2026-10-16 notes (source + normalized)
├── merged.md                     # the product plan ("Openseek")
├── manifest.json                 # name/version/platforms/build
├── CMakeLists.txt                # CMake + Ninja only
├── cmake/                        # build helpers (e.g. TestHelpers.cmake)
├── libs/
│   ├── firstparty/
│   │   ├── archive_engine/       # submodule: arc_util, arc_fs (we use arc_fs)
│   │   ├── pe-holiday-calc/      # submodule: C++17 holiday library (Peru)
│   │   └── framework/            # submodules: App_shell, Vk_Canvas_Lb_LAW (GUI)
│   └── thirdparty/               # vendored deps (sqlite3mc amalgamation)
├── core/                         # ALL logic. portable. include/ src/ tests/
├── cli/                          # the MVP frontend. links core.
├── mcp/                          # the local MCP server (links core)
├── alarm/                        # the alarm engine (links core)
├── android/                      # later: JNI + gradle (see MEMORY §5)
├── gui/                          # later: vk_canvas + app_shell
├── assets/fonts/                 # submodule
└── scripts/                      # stdlib-only Python zipapp build tool
```

## 2. Non-negotiable invariants

1. **`core/` is portable.** No `<windows.h>`, no JNI, no platform SDK includes,
   no `#ifdef _WIN32` / `__ANDROID__` inside `core/`. Anything platform-specific
   is *installed into* core from the outside (see `arc::fs::setPlatformDirs`)
   or lives under `android/` / `gui/`.
2. **`core/` has no frontend.** It must not know that a CLI, a GUI, or an MCP
   server exists. Frontends link core; core never links a frontend.
3. **The database lives in `state`.** Path is
   `arc::fs::layout("klend_ario").state`. Never `home`, `data`, or beside the
   executable. (`MEMORY.md` §5 explains the FUSE reason.)
4. **Naming:** namespace `klend_ario::`; targets `klend_ario_core`,
   `klend_ario_cli`; tests `<name>_test`. Never reuse `ae`, `arc`, or `vce`
   prefixes — those belong to sibling libraries.
5. **Errors are values.** Use `arc::Result<T>` / `arc::Error` from `arc_util`.
   Do not throw across a module boundary.
6. **CMake + Ninja only.** No Make wrappers, no other generators, no `uv` as a
   build dependency (the zipapp may *use* Python, the app may not).
7. **Only std, plus the C `sqlite3` API (via the vendored SQLite3MultipleCiphers),
   plus our first-party `pe_holiday_calc`, plus `arc_fs`/`arc_util`.** Adding a
   third-party dependency is a decision to write in `MEMORY.md` first.
8. **Holidays have one source.** Consume them through core
   (`klend_ario/holidays.hh`: `holidaysFor`, `isHoliday`, `holidayOn`), never by
   including the library's registry from a frontend. A frontend never gets its
   own holiday table.
9. **Desktop first.** Linux is the only active platform; Android is deferred
   (Phase 7). No JNI, NDK, gradle, or Android SDK work until that phase is
   explicitly reopened.
10. **UTC is canonical.** Store and compare instants as Unix seconds UTC. Local
    zones are display-only; showing local time must still leave UTC visible.
    `core/` never asks the OS for the local timezone. NTP is a runtime frontend
    concern, never a core dependency or a test dependency.
11. **A day is half-open.** The interval is `[00:00:00, next 00:00:00)` =
    **86,400 seconds**. 23:59:59 is the last named second (86,399 s after
    midnight), not the length. Use half-open ranges; never add an inclusive
    endpoint that duplicates a second.
12. **Performance first, but measured.** SQLite is the current store. JSON is a
    hypothesis to benchmark, not a default; do not rewrite storage without a
    measured win on the real operations. Encryption is implemented; do not
    expand or remove it while performance is the priority without a recorded
    decision.
13. **Captured requirements do not move the phase.** The notes companion, the
    10-hour sleep scheduler, G-SHOCK sync, and NTP are post-MVP
    (`REQUIREMENTS.md`); implement them only when their phase starts.
14. **Notes reference events; they do not copy them.** A future note links to
    `events.id`. Sleep is a first-class dynamic `sleep` event: default 10 hours,
    auto-generated, hideable/showable automatically — not a GUI special case.

## 3. Build & test

```bash
# configure (once) and build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C build

# run every test, with a one-line summary
ctest --test-dir build --output-on-failure

# run the CLI
./build/cli/klend_ario_cli --help
```

The zipapp build tool (once Phase 0 is done) is the friendly front end:

```bash
./scripts/build.sh            # Linux-only for now
```

Release builds exist but are not the dev loop; tests are **Debug-only**.

## 4. Conventions

- **C++17**, `-Wall`, no compiler extensions. C only where a library demands it
  (`sqlite3.c`).
- **Headers** go in `core/include/klend_ario/`, one per concept, `#pragma once`,
  documented at the top with the *reason* the thing exists, not a restatement of
  its name. Read any `arc_fs` header as the model: the comments explain trade-
  offs, not syntax.
- **Sources** in `core/src/`. Small files, one job each.
- **SQL** lives in `core/sql/schema.sql`, embedded at configure time. Migrations
  are additive and versioned in a `meta` table.
- **Holidays** are civil dates, not instants: use `holidays::Date`, never a
  timezone or `<ctime>`. `holidaysFor`/`isHoliday` treat an unknown country as
  `Err`, while "that day is not a holiday" is `Ok` (and `Ok(nullopt)` for
  `holidayOn`). Do not copy the Peru calendar into `core/`.
- **Time** is Unix seconds UTC. Keep calendar math pure integer; parse/format
  ISO-8601 UTC at the edge. A local zone is a rendering choice, never a stored
  value or a core dependency.
- **Day arithmetic** is half-open `[0, 86400)`. The notes' 80,000–81,000 s
  example is a test vector: 22:13:20 → 22:30:00, 1,000 s = 16 min 40 s.
- **CLI** is a thin shell: parse args → call a core function → print. No business
  logic in `cli/`.
- **CLI time expressions** (`today`, `week+7d`, `90m`) are UTC and parsed by
  `klend_ario/timeexpr.hh` in core, never by ad-hoc date code in `cli/`.
- **CLI output** is rendered by the pure functions in `cli/src/format.cc`
  (text/table/json/csv/tsv) and printed once. Do not `printf` rows from inside a
  command; that is what made the visible contract untestable.
- **Queries are a core value.** Build an `EventQuery` and call
  `Db::queryEvents`; do not filter a `listEvents` result in a frontend. Text is
  bound, `ORDER BY` is a whitelist, and the half-open overlap rule lives in one
  place (`listEvents` delegates). `hidden` is a weekly-view flag, not deletion:
  data verbs see hidden rows, view verbs filter them by default.
- **Intelligence is pure where it can be.** `findConflicts` and `freeSlots` are
  interval functions over a vector of events, tested without a database.
  `EventType::Alarm` rows are reminders, not busy blocks, and a hidden row still
  alarms — hiding is a display choice.
- **`doctor` is read-only.** `diagnose` never migrates, repairs, or deletes; it
  reports and lets a human decide. Its exit code is `0` all-ok, `1` warn/fail.
  Use `Db::openReadOnly`/`openEncryptedReadOnly`: no create, no migrate. A
  missing or zero-byte file must be reported, never initialised into a healthy
  empty calendar.
- **Options are allowlisted per verb.** A flag that the verb does not use is a
  usage error (exit `2`), not a silent no-op. Completion is generated from the
  same per-verb table.
- **Export is a separate contract from display.** `core/export` owns the
  CSV/JSONL/ICS bytes; do not reuse the CLI's compact `list` renderer. `--format`
  is verb-scoped, and ICS is one-way: it is an exporter, not a parser.
- **Import parses before it writes.** `parseImportedEvents` validates the whole
  document; `Db::importEvents` applies it in one transaction. Empty input is an
  error, append is the default, and `--replace`/`--preserve-ids` are explicit so
  a merge never silently becomes a restore.
- **A partial edit changes only supplied flags.** Carry a `*Given` flag in
  `Args` for every editable field; a default value cannot mean "leave alone".
- Every comment that explains a *surprising* choice is worth its weight. Every
  comment that restates the code is noise (`MEMORY.md` §5).

## 5. Definition of done

A change is done when:

1. It builds Debug with no new warnings.
2. `ctest` passes, **and** the new behaviour has a test that would fail without
   the change (`TESTING.md`).
3. `core/` still contains no platform or frontend dependency.
4. Any decision or gotcha it introduces is written into `essentialAgent/`.
5. If it captures or changes a requirement, `REQUIREMENTS.md`, `MEMORY.md`, and
   `PLAN.md` are updated in the same change.
6. It is committed with `./git_wrapper` (below).

## 6. Git

- **Commit and push with `./git_wrapper`, never plain `git commit`/`git push`.**
  It forces the single identity (`nava`), strips `Co-Authored-By:`/"Generated
  with" trailers, and pushes submodules before their parent.
- `./git_wrapper save "message"` = commit + push in one step.
- If a **submodule** changed, it must be pushed (in the submodule) before the
  parent pin is advanced. `git_wrapper push` does this in the correct order.
  This applies to `pe-holiday-calc` exactly as it does to `archive_engine`.
- No generated files, `build/`, or `*.pyz` in the index.

## 7. Do not

- Do not invent a GUI, an MCP server, or encryption early. Phases are in
  `PLAN.md`; stay in the current one unless asked.
- Do not add Android, JNI, NDK, or gradle work before Phase 7 is reopened.
- Do not replace SQLite with JSON, or remove encryption, without a measured
  benchmark and a decision in `MEMORY.md`.
- Do not make `core/` read the local timezone or perform network/NTP work.
- Do not implement the notes app, sleep scheduler, or G-SHOCK sync outside their
  phases.
- Do not put a SQLite file anywhere but `state`.
- Do not reach past `arc_fs` for filesystem work "just this once".
- Do not edit `libs/firstparty/archive_engine` to make this app's life easier
  without recording the change in `MEMORY.md` and pushing it there first — it is
  shared with Matrix_Player and the streamer.
- Do not edit `libs/firstparty/pe-holiday-calc` from here either. It is its own
  repository with its own `essentialAgent/`; a change there is made, tested, and
  pushed in the child, then its pin is advanced here.
