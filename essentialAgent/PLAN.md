# PLAN — phases, the scaffold, and status

*What gets built, in what order, and what exists right now. Update the checkboxes
as work lands; this file is the tracker.*

---

## Phase table

| Phase | Deliverable | Status |
|---|---|---|
| **0** | **Scaffold** — submodule, CMake+Ninja, pyz tool, docs, first test | **done** |
| 1 | Core — SQLite schema + storage + model, with tests | **done** |
| 2 | **CLI MVP** — `add` / `list` / `show` / `rm` | not started |
| 3 | Encryption (`SQLite3MultipleCiphers`) + Syncthing sync policy | not started |
| 4 | GUI — weekly 16:9 view (`vk_canvas` + `app_shell`) | not started |
| 5 | MCP server — local, read/write, minimal tool surface, tiers | not started |
| 6 | Alarms — libnotify + PipeWire / Android native | not started |
| 7 | Android platform layer + storage permissions | not started |

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

## Status log

Newest first. One line per meaningful change.

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
