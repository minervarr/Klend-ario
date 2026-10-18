# AGENTS.md

The full agent manual for this repository lives in **`essentialAgent/`**.
Read [`essentialAgent/README.md`](essentialAgent/README.md) first, then
[`essentialAgent/AGENTS.md`](essentialAgent/AGENTS.md).

The non-negotiables, so they apply even before you read further:

- **`core/` is portable** — no OS SDK includes, no JNI, no `#ifdef _WIN32`, and
  it must not know a frontend exists.
- **The database lives in `arc::fs::layout("klend_ario").state`** — never in
  `home`/`data`, never beside the executable.
- **Never create the user's directories** — startup uses `LayoutScope::App`;
  Music/Downloads/Pictures/Documents are resolved from `user-dirs.dirs`, not
  invented or created by opening a database.
- **Namespace `klend_ario::`; targets `klend_ario_core` / `klend_ario_cli`** —
  never reuse the `ae`, `arc`, or `vce` prefixes (they belong to sibling libs).
- **CMake + Ninja only.** Tests are plain `assert()` binaries registered with
  CTest, Debug-only.
- **Errors are `arc::Result<T>` values**, not exceptions across modules.
- **Holidays come from `pe_holiday_calc`** — the first-party submodule at
  `libs/firstparty/pe-holiday-calc`. Core exposes them through
  `klend_ario/holidays.hh` and adapts `std::optional` to `arc::Result`; never
  duplicate date math, and never edit the submodule from the parent.
- **Desktop-first** — Linux is the only active platform; Android is deferred,
  so no JNI/NDK/gradle work until Phase 7 is reopened.
- **UTC and half-open days** — store Unix seconds UTC; a day is `[0, 86400)`;
  local zones are display-only, and NTP belongs to the runtime.
- **Performance first, but measured** — SQLite stays until a benchmark beats it;
  JSON is a candidate, not a decision.
- **Captured requirements** — notes, 10 h dynamic sleep, G-SHOCK sync, and NTP
  are planned post-MVP; see `essentialAgent/REQUIREMENTS.md`. Do not build ahead.
- **Commit/push with `./git_wrapper`** — submodules first, single identity, no
  `Co-Authored-By` trailers.
- **The demo is a consumer** — `demo/klend_ario_demo.py` drives the built
  binaries with a throwaway `--db`; it never touches the real state DB, links
  `core/`, or adds Python dependencies. See `essentialAgent/DEMO.md`.
- **Stay in the current phase.** See `essentialAgent/PLAN.md`.
