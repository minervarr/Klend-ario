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
├── merged.md                     # the product plan ("Openseek")
├── manifest.json                 # name/version/platforms/build
├── CMakeLists.txt                # CMake + Ninja only
├── cmake/                        # build helpers (e.g. TestHelpers.cmake)
├── libs/
│   ├── firstparty/
│   │   ├── archive_engine/       # submodule: arc_util, arc_fs (we use arc_fs)
│   │   └── framework/            # submodules: App_shell, Vk_Canvas_Lb_LAW (GUI)
│   └── thirdparty/               # vendored deps (sqlite3 amalgamation)
├── core/                         # ALL logic. portable. include/ src/ tests/
├── cli/                          # the MVP frontend. links core.
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
7. **Only std, plus the C `sqlite3` API, plus `arc_fs`/`arc_util`.** Adding a
   third-party dependency is a decision to write in `MEMORY.md` first.

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
- **CLI** is a thin shell: parse args → call a core function → print. No business
  logic in `cli/`.
- Every comment that explains a *surprising* choice is worth its weight. Every
  comment that restates the code is noise (`MEMORY.md` §5).

## 5. Definition of done

A change is done when:

1. It builds Debug with no new warnings.
2. `ctest` passes, **and** the new behaviour has a test that would fail without
   the change (`TESTING.md`).
3. `core/` still contains no platform or frontend dependency.
4. Any decision or gotcha it introduces is written into `essentialAgent/`.
5. It is committed with `./git_wrapper` (below).

## 6. Git

- **Commit and push with `./git_wrapper`, never plain `git commit`/`git push`.**
  It forces the single identity (`nava`), strips `Co-Authored-By:`/"Generated
  with" trailers, and pushes submodules before their parent.
- `./git_wrapper save "message"` = commit + push in one step.
- If a **submodule** changed, it must be pushed (in the submodule) before the
  parent pin is advanced. `git_wrapper push` does this in the correct order.
- No generated files, `build/`, or `*.pyz` in the index.

## 7. Do not

- Do not invent a GUI, an MCP server, or encryption early. Phases are in
  `PLAN.md`; stay in the current one unless asked.
- Do not put a SQLite file anywhere but `state`.
- Do not reach past `arc_fs` for filesystem work "just this once".
- Do not edit `libs/firstparty/archive_engine` to make this app's life easier
  without recording the change in `MEMORY.md` and pushing it there first — it is
  shared with Matrix_Player and the streamer.
