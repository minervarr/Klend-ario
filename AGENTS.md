# AGENTS.md

The full agent manual for this repository lives in **`essentialAgent/`**.
Read [`essentialAgent/README.md`](essentialAgent/README.md) first, then
[`essentialAgent/AGENTS.md`](essentialAgent/AGENTS.md).

The non-negotiables, so they apply even before you read further:

- **`core/` is portable** — no OS SDK includes, no JNI, no `#ifdef _WIN32`, and
  it must not know a frontend exists.
- **The database lives in `arc::fs::layout("klend_ario").state`** — never in
  `home`/`data`, never beside the executable.
- **Namespace `klend_ario::`; targets `klend_ario_core` / `klend_ario_cli`** —
  never reuse the `ae`, `arc`, or `vce` prefixes (they belong to sibling libs).
- **CMake + Ninja only.** Tests are plain `assert()` binaries registered with
  CTest, Debug-only.
- **Errors are `arc::Result<T>` values**, not exceptions across modules.
- **Commit/push with `./git_wrapper`** — submodules first, single identity, no
  `Co-Authored-By` trailers.
- **Stay in the current phase.** See `essentialAgent/PLAN.md`.
