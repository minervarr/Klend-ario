# Klend-ario

A privacy-first, AI-native personal calendar: one encrypted SQLite file, synced
by Syncthing, queried through an MCP server, rendered as a weekly 16:9 view.
Written natively in **C++17**, running on Linux and Android.

The product design is [`merged.md`](merged.md) (*"Openseek — Final Plan (v3)"*).
The how-we-build-it manual — what, why, rules, plan, testing — lives in
[`essentialAgent/`](essentialAgent/README.md). **Read that first.**

## Build

CMake + Ninja only. Configure once, then build:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C build
```

The friendly front end (a stdlib-only Python zipapp) drives the same build:

```bash
./scripts/build.sh            # Linux-only for now
```

## Test

Tests are plain `assert()` binaries registered with CTest, built Debug-only:

```bash
ctest --test-dir build --output-on-failure
```

## Run

```bash
./build/cli/klend_ario_cli --help
```

## Layout

```
core/    all logic — portable, no OS headers, no frontend
cli/     the MVP frontend; links core
mcp/     the local MCP server (JSON-RPC over stdio); links core
alarm/   the alarm engine (libnotify, or a log); links core
gui/     the weekly view — pure geometry now, vk_canvas renderer later (Phase 4)
android/ later (Phase 7)
libs/    firstparty submodules (archive_engine) + vendored thirdparty (sqlite3mc)
scripts/ the zipapp build tool
```

Commits and pushes go through [`./git_wrapper`](essentialAgent/AGENTS.md) — never
plain `git commit`/`git push`.
