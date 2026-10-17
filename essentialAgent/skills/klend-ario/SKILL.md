---
name: Klend-ario
description: Work in the Klend-ario calendar repo — build/test with CMake+Ninja, and add a feature end-to-end (core function, test, CLI verb) following its conventions.
---

# Klend-ario workflow

Use this when asked to change code in the **Klend-ario** repository. The rules
live in `essentialAgent/AGENTS.md`; this is the step-by-step procedure.

## Before touching code

1. Read `essentialAgent/MEMORY.md` (what/why/decisions),
   `essentialAgent/REQUIREMENTS.md` (captured 2026-10-16 requirements), and
   `essentialAgent/AGENTS.md` (invariants).
2. Check `essentialAgent/PLAN.md` for the current phase. Do not build ahead of
   it without being asked.
3. Decide which repo owns the change: application behaviour goes in `core/`;
   holiday **data or rules** belong in `libs/firstparty/pe-holiday-calc`, whose
   own `essentialAgent/` governs it. Never edit the submodule from the parent.

## Build & test loop

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C build
ctest --test-dir build --output-on-failure
./build/cli/klend_ario_cli --help
```

Tests are plain `assert()` binaries registered with CTest (`TESTING.md`).

## Adding a feature end-to-end (the standard slice)

Work inside-out: model → storage → CLI. Never put logic in the CLI.

0. **Time input** — if the feature accepts a date, instant or duration, extend
   `core/include/klend_ario/timeexpr.hh` (and `timeexpr_test`) rather than
   parsing it in `cli/`.
1. **Decide where it lives.** Business logic in `core/`; only argument parsing
   and printing in `cli/`. `core/` must stay portable (no OS headers, no JNI).
2. **Model** — add/extend a header in `core/include/klend_ario/`. Return
   `arc::Result<T>` on failure paths; do not throw.
3. **Storage** — if it needs the DB, add SQL to `core/sql/schema.sql`
   (additive/versioned) and a function in `core/src/db.cc`. The DB path comes
   from `arc::fs::layout("klend_ario").state`.
4. **Core test** — add `core/tests/<name>_test.cc` (`#undef NDEBUG`, asserts),
   register it in `core/CMakeLists.txt` with `klend_ario_add_test(...)`. Use an
   **in-memory** DB (`:memory:`) for storage tests. Make it fail first, then make
   it pass.
5. **CLI** — parse any new flags in `cli/src/args.cc` (std-only, unit-tested in
   `cli/tests/args_test.cc`), call the core function from
   `cli/src/commands.cc`, and render through `cli/src/format.cc` (test the bytes
   in `format_test` when output changes). No logic here.
6. **Prove it.** `ninja -C build && ctest --test-dir build --output-on-failure`
   must be green, and the manual CLI check must behave.
7. **Record** — if you made a decision or found a gotcha, write it into
   `essentialAgent/` (usually `MEMORY.md` or the `PLAN.md` status log).
8. **Commit** — `./git_wrapper save "…"`. If a submodule changed, it is pushed
   first automatically; never use plain `git commit`/`git push`.

## Guardrails

- Do not edit `libs/firstparty/archive_engine` for our convenience — it is shared
  with Matrix_Player and the streamer. If a real gap appears, note it in
  `MEMORY.md` and push there first.
- Do not duplicate holiday logic. Query through `klend_ario/holidays.hh`; keep
  civil-date math in the submodule, and remember that an unknown country is
  `Err` while "no holiday today" is `Ok(nullopt)`.
- Do not add a dependency without recording the decision.
- Do not start the GUI, MCP, or encryption before their phase.
- **Desktop-first.** Do not add Android/JNI/NDK/gradle work before Phase 7 is
  reopened.
- **UTC + half-open days.** Store Unix seconds UTC; the interval is
  `[0, 86400)`. Local zones are display-only; never put `<ctime>`/local-zone
  logic or NTP in `core/`.
- **Queries go through core.** Build an `EventQuery` and call
  `Db::queryEvents`; never filter a `listEvents` result in a frontend and never
  interpolate user text into SQL. `hidden` is a view flag: `list`/`search` see
  hidden rows, view verbs filter them.
- **No speculative storage rewrite.** SQLite stays until a benchmark proves a
  JSON store faster on the real operations; encryption is not a blocker.
- **Captured ≠ built.** Notes, the 10 h sleep scheduler, G-SHOCK sync, and NTP
  are in `REQUIREMENTS.md`; do not implement them ahead of their phase.
