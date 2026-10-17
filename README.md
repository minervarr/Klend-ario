# Klend-ario

A privacy-first, AI-native personal calendar: one encrypted SQLite file, synced
by Syncthing, queried through an MCP server, rendered as a weekly 16:9 view.
Written natively in **C++17**, **desktop-first on Linux**; Android is deferred.

The product design is [`merged.md`](merged.md) (*"Openseek — Final Plan (v3)"*).
The how-we-build-it manual — what, why, rules, plan, testing — lives in
[`essentialAgent/`](essentialAgent/README.md). **Read that first.**

## Scope and captured requirements

The 2026-10-16 product notes are recorded in
[`essentialAgent/REQUIREMENTS.md`](essentialAgent/REQUIREMENTS.md): desktop
first, UTC with local time as a display, a notes companion keyed to events, a
10-hour automatic sleep window, G-SHOCK sleep/wake sync (research), NTP, and the
SQLite-vs-JSON performance question. SQLite remains the store until a benchmark
says otherwise; encryption stays in place.

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
./build/cli/klend_ario_cli help add
./build/cli/klend_ario_cli add "Standup" --start today+9h --duration 30m
./build/cli/klend_ario_cli today --format table
./build/cli/klend_ario_cli week
./build/cli/klend_ario_cli next
./build/cli/klend_ario_cli agenda --days 3
./build/cli/klend_ario_cli search "standup"
./build/cli/klend_ario_cli conflicts
./build/cli/klend_ario_cli free --min-duration 45m
./build/cli/klend_ario_cli alarms --limit 5
./build/cli/klend_ario_cli stats
./build/cli/klend_ario_cli doctor
./build/cli/klend_ario_cli export --format csv > backup.csv
./build/cli/klend_ario_cli export --format ics > calendar.ics
./build/cli/klend_ario_cli import backup.csv
./build/cli/klend_ario_cli import backup.csv --replace --preserve-ids
./build/cli/klend_ario_cli completion bash
./build/cli/klend_ario_cli list --from today --to today+7d --min-importance 70
./build/cli/klend_ario_cli edit 1 --shift 1h
./build/cli/klend_ario_cli holidays --year 2026 --format csv
```

Instants are UTC. Absolute ISO-8601 (`2026-10-16T09:00:00Z`), date-only
(`2026-10-16`), relative words (`now`, `today`, `tomorrow`, `week`,
`sunday..saturday`) and offsets (`today+2h`, `+90m`) all work. For views,
`--format` is `text`, `table`, `json`, `csv`, or `tsv`; `--json` remains an
alias.

`today`, `week`, `next` and `agenda` show visible rows by default (hidden sleep
blocks stay out of the view); `list` and `search` include them so they can be
found and edited. Override with `--visible`, `--hidden-only`, or
`--include-hidden`. Search is a literal, case-insensitive substring of title and
notes — `%` and `_` are ordinary characters, and `next` includes an event that
is already underway.

`conflicts` reports pairwise overlaps (alarm rows are reminders, not blocks);
`free` merges busy time and lists the gaps; `alarms` lists T-offset and T=0
triggers. Alarms include hidden rows by default — hiding a sleep block in the
weekly view does not cancel its wake-up.

`stats` summarizes all rows by default (narrow with `--from`/`--to`/`--type`);
`doctor` is a read-only health report and exits `1` when it finds a warning
(Syncthing conflict copy, missing marker, bad schema version, or corrupt data).

`export` writes stdout, all rows and all time by default. Its `--format` is
`csv`, `jsonl`, or `ics`: CSV/JSONL carry every stored field, while ICS is
one-way interoperability (UTC events, a `VALARM` for the offset). `import`
reads the same CSV/JSONL shape and appends with fresh ids by default;
`--replace` restores in one transaction, `--preserve-ids` keeps ids, and
`--dry-run` only validates. The whole file is parsed before anything is
written, and empty input is rejected. `completion bash|zsh|fish` prints a
script for your shell.

## Layout

```
core/    all logic — portable, no OS headers, no frontend
cli/     the MVP frontend; links core
mcp/     the local MCP server (JSON-RPC over stdio); links core
alarm/   the alarm engine (libnotify, or a log); links core
gui/     the weekly view — pure geometry now, vk_canvas renderer later (Phase 4)
android/ later (Phase 7)
libs/    firstparty (archive_engine, pe-holiday-calc) + vendored thirdparty (sqlite3mc)
scripts/ the zipapp build tool
```

Commits and pushes go through [`./git_wrapper`](essentialAgent/AGENTS.md) — never
plain `git commit`/`git push`.
