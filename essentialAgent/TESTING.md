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
| Schema & storage (`db`, `schema`) | `core/tests/` | `src/db.cc src/schema.cc` + `sqlite3` | in-memory DB |
| CLI argument parsing | `cli/tests/` | `args.cc` only | none |
| CLI end-to-end (later) | `cli/tests/` | `klend_ario_core` | temp DB |

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
