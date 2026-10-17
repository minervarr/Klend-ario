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

The CLI uses three exit codes: `0` success, `1` a failed operation or invalid
data, and `2` a command-line usage error. `doctor` uses `1` for a warn/fail
report; `import` uses `1` for a bad file.

## Run

```bash
./build/cli/klend_ario_cli --help
./build/cli/klend_ario_cli --version
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

Completion is generated, not installed globally:

```bash
# bash
source <(./build/cli/klend_ario_cli completion bash)

# zsh (after compinit)
source <(./build/cli/klend_ario_cli completion zsh)

# fish
./build/cli/klend_ario_cli completion fish | source
```

Instants are UTC. Absolute ISO-8601 (`2026-10-16T09:00:00Z`), date-only
(`2026-10-16`), relative words (`now`, `today`, `tomorrow`, `week`,
`sunday..saturday`) and offsets (`today+2h`, `+90m`) all work. For views,
`--format` is `text`, `table`, `json`, `csv`, or `tsv`; `--json` remains an
alias. Flags are scoped to the verbs that use them: an unrelated option is a
usage error, not a silent no-op.

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
written, and empty input is rejected.

`doctor` and `export` open the database read-only and never create or migrate
a file. An explicit `--db` on a read-only view must already exist too; view
verbs may still create the default state path on first use. `completion
bash|zsh|fish` prints a script for your shell.

The CLI feature set is frozen as of 2026-10-17; new feature work goes to the
GUI. Bug fixes and tests still belong here.

## Weekly view (GUI)

The weekly view is built behind one option because the window needs the Vulkan
SDK, the Slang compiler and the Wayland/xkb development packages:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DKLEND_BUILD_GUI=ON
ninja -C build
./build/gui/klend_ario_gui --help
./build/gui/klend_ario_gui                    # read-only, the state database
./build/gui/klend_ario_gui --db /tmp/demo.db  # or a throwaway one
```

Keys: `Left`/`Right` week, `T` today, `H` toggle hidden blocks, `Esc` quit;
the header's `< today >` buttons do the same with the mouse. The view is
read-only for now — the CLI and the MCP server write — but it notices their
writes within a second through `Db::dataVersion()`.

Shaders and `fonts/ui/` are copied beside the binary; `FileAssetReader` looks
there. Headless screenshots (no window, no compositor) come from the capture
tool:

```bash
cd build/gui
./klend_ario_capture --out /tmp/shots          # 1920x1080 PNGs
./klend_ario_capture --frame 1280x720          # full-bleed layout
```

See [`gui/README.md`](gui/README.md) for the three layers and the rules behind
them.

## AI client (OpenCode)

The MCP server is the AI-facing surface. This repository's
[`opencode.jsonc`](opencode.jsonc) already registers it as a local command with
all permission tiers enabled; the same block in a user config is:

```jsonc
{
  "mcp": {
    "klend_ario": {
      "type": "local",
      "command": ["./build/mcp/klend_ario_mcp_server"],
      "enabled": true
    }
  }
}
```

Add `"--permissions", "read"` (or `"read,write"`) to keep an agent away from
destructive edits. `initialize` carries the calendar instructions the model
reads, and `list_events` accepts the same relative time grammar as the CLI.
See [`mcp/README.md`](mcp/README.md) for the tool contract.

## Demo

The permanent end-to-end showcase drives the built binaries against a throwaway
database and narrates every public surface:

```bash
python3 demo/klend_ario_demo.py
python3 demo/klend_ario_demo.py --pause   # step through it
```

It never touches the real state database; `--keep` preserves the temporary
directory for inspection. See [`demo/README.md`](demo/README.md).

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
demo/    the end-to-end CLI/MCP/alarm showcase (a consumer; see demo/README.md)
```

Commits and pushes go through [`./git_wrapper`](essentialAgent/AGENTS.md) — never
plain `git commit`/`git push`.
