# Klend-ario demo

`klend_ario_demo.py` is the permanent, narrated end-to-end showcase for the
built binaries. It is a **consumer**, not a second implementation: every action
goes through the real CLI, MCP server or alarm daemon.

```bash
ninja -C build
python3 demo/klend_ario_demo.py
```

Use it as a tutorial, as a smoke test after a build, or as a starting point for
your own scripts. It exercises all 21 CLI verbs, the five MCP tools, encryption,
export/import and the alarm engine.

## What a run shows

| # | Section | Surfaces |
|---|---------|----------|
| 1 | Setup and isolation | `--version`, `help add`, the temporary directory |
| 2 | Create | `add`, all four types, `--start/--end/--duration`, `--title`, notes, importance/urgency, alarm offset, hidden |
| 3 | Read and formats | `today`, `week`, `next`, `agenda`, `list`, `search`, `show`; text/table/json/csv/tsv; filters, sort, limit, hidden flags |
| 4 | Change | `edit` (partial fields, `--shift`, `--duration`), `duplicate`, `rm` (idempotent) |
| 5 | Intelligence | `conflicts`, `free`, `alarms`, `stats` |
| 6 | Health and interop | `doctor`, `holidays`, `export` (CSV/JSONL/ICS), `import` (dry-run, append, replace, preserve ids, rejected bad file) |
| 7 | Scripting | parse `--format json` from Python and build a small day plan |
| 8 | Shell completion | `completion bash`, `zsh`, `fish` (printed, not installed) |
| 9 | Encryption | key file, plaintext rejection, right key, wrong key |
| 10 | MCP server | `initialize` (instructions), `tools/list` (titles/annotations, tier-filtered), create/get/list/update/delete, read-only permission denial |
| 11 | Alarm engine | `klend_ario_alarmd --once --notify log` with a trigger due now |
| 12 | Done | a short checklist, then cleanup |

Sections 10 and 11 skip with a visible note when their binaries have not been
built. The CLI story always runs.

## Options

| Option | Meaning |
|---|---|
| `--pause` | Wait for Enter between sections. The default is non-interactive. |
| `--keep` | Keep the temporary directory and print its path for inspection. |
| `--no-color` | Disable ANSI color. Color is also off automatically when stdout is not a TTY. |
| `--cli PATH` | Use a specific `klend_ario_cli` binary. |
| `--release` | Prefer `build_release/` binaries over `build/`. |

Binary discovery is, in order: `--cli`, the build directory next to the chosen
CLI (or `build/`, then `build_release/`), and finally `PATH`. A missing CLI is
an error with the CMake/Ninja command to build it; a missing MCP server or alarm
daemon only skips that section.

## Focused MCP smoke test

`mcp_smoke.py` is the quiet companion to the demo: it exercises only the MCP
server, over the same newline-delimited JSON-RPC transport a real host uses.

```bash
ninja -C build
python3 demo/mcp_smoke.py
```

It asserts the `initialize` handshake and instructions, the five tool schemas
with titles/annotations, a full create/get/list/update/delete round-trip,
structured content, strict-input errors, relative time and durations, query
visibility/search/sort/limit, update semantics, the default "now"/range,
protocol resilience (malformed frames, string ids, batching, unknown methods),
encryption through `--key-file` and `KLEND_ARIO_KEY`, the read-only tier on a
write-protected database, the `read,write` middle tier, startup options, and
that stdout carries protocol frames and nothing else. When `klend_ario_cli` is
built it also reads the MCP-written rows through the CLI on the same database,
proving both frontends share one SQLite file.

```bash
python3 demo/mcp_smoke.py --release   # prefer build_release/
python3 demo/mcp_smoke.py --keep      # inspect the temporary database
python3 demo/mcp_smoke.py -v          # print every JSON-RPC frame
```

Exit codes: `0` every check passed, `1` a check failed, `2` bad arguments or a
missing binary. The script uses one `klend_ario_mcp_smoke-*` temporary
directory, passes an explicit `--db` to every process, drops `KLEND_ARIO_KEY`
from the child environment, and removes the directory on exit, `SIGINT` and
`SIGTERM`. It never touches the real `state` database or `opencode.jsonc`.

## Clean-run guarantee

Every run creates one `klend_ario_demo-*` directory under the system temporary
directory. The database, exports, key files and imported database live there.
Every database-backed command receives an explicit `--db` pointing into that
directory.

The real `state` database (`arc::fs::layout("klend_ario").state`) is never
opened, created or inspected. Nothing is written inside the repository, and no
network is used.

Cleanup runs on normal exit, `SIGINT` and `SIGTERM`. `--keep` is the only way to
preserve a run. The script also needs no third-party Python packages: it is
stdlib-only.

Time expressions are relative (`today+9h`, `tomorrow+450m`, `now-1m`), so the
demo works on any day without editing fixtures. The alarm section uses
`--horizon 86400`, so `--pause` cannot make its trigger miss the poll window.

## Exit codes

- `0` — every step behaved as expected.
- `1` — the demo itself failed, or a command returned an unexpected result.
- `2` — bad arguments to the demo (from `argparse`).

Expected failures are part of the tour and are labelled: a usage error is `2`,
a runtime/data error is `1`. For example, opening an encrypted database without
its key must exit `1`, and a bad import must exit `1` without changing the
database.

## Notes for maintainers

- The CLI is frozen; the demo is the first caller to break when a verb, flag or
  output shape changes. Fix the demo in the same change.
- Parse JSON/CSV/JSONL for values; do not scrape human text for decisions.
  Text output is displayed because the demo is about the human view.
- Do not add a demo-only flag or a third-party dependency. Every command shown
  must already exist in `--help`.
- This is a showcase and a smoke test, not a CTest suite. `ctest` remains the
  correctness gate; see `essentialAgent/DEMO.md`.
