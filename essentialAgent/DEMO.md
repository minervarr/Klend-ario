# DEMO — the permanent showcase

*One script that proves the product surface works and teaches it. The work is
tracked in [`PLAN.md`](PLAN.md); this file is the contract.*

---

## 1. Why it exists

The CLI has 21 verbs and a deep flag surface. The README lists them and `ctest`
proves the parts, but neither shows a human what using the calendar *feels*
like, and neither catches a verb that still compiles but no longer composes with
the rest.

`demo/klend_ario_demo.py` is a narrated end-to-end tour. It creates real events
through the real binaries, queries and edits them, exports and re-imports them,
talks to the MCP server, and fires an alarm — then deletes every trace.

It is a **showcase and a smoke test**, not the correctness gate. `ctest` remains
that (`TESTING.md`). If the demo and a test disagree, the test is right.

## 2. What it is

| | |
|---|---|
| Path | `demo/klend_ario_demo.py`, plus a short `demo/README.md` |
| Language | Python 3, standard library only |
| Build dependency | none; it consumes binaries CMake already built |
| Normal run | `python3 demo/klend_ario_demo.py` |
| Options | `--pause`, `--keep`, `--no-color`, `--cli PATH`, `--release` |
| Writes | one per-run temporary directory, and nothing else |
| Lifetime | committed permanently; every run starts from zero |

It is deliberately **not** part of CMake, CTest, the zipapp build tool, or the
app's runtime dependencies.

## 3. The clean-run contract

This is the part that must not erode. "Like the first time" is a property of the
design, not a cleanup step.

1. **One temp root per run.** `tempfile.mkdtemp(prefix="klend_ario_demo-")` is
   the only writable location. `calendar.db`, `import.db`, `key.txt`, and every
   export live there.
2. **Every database-backed subprocess gets an explicit `--db`.** A single helper
   injects `--db <temp>/calendar.db` (and `--key-file` where encryption is
   shown). No call site may omit it. The only exceptions are the verbs that are
   *defined* not to open a database — `holidays`, `completion`, `help`,
   `--version` — and those reject `--db` by design.
3. **The real database is never touched.** The default `state` path and any
   user-supplied database are not opened, created, migrated, or inspected. The
   demo does not call `arc_fs`; it only knows paths it made.
4. **Cleanup is guaranteed.** Normal exit, `SIGINT`, and `SIGTERM` remove the
   temp root. `--keep` is the only opt-out and prints the path it preserved.
5. **Time is relative.** Seeds use `today+9h`, `+2m`, `week`, `90m` — never a
   hard-coded date — so the same script works tomorrow and next year.
6. **No side effects outside the temp root.** No network, no pip install, no
   shell-profile edits, no files written inside the repository.

If any rule cannot be honoured, the script stops with a clear error. It never
"falls back" to a shared database.

## 4. What a run demonstrates

The sections are ordered so each one can use what the previous ones created.

| # | Section | Public surface exercised |
|---|---|---|
| 1 | Setup | `--version`, `help add`, temp DB creation |
| 2 | Create | `add`: positional title, `--start/--duration/--end`, `event/sleep/wake/alarm`, importance/urgency, `--alarm-offset`, `--notes`, `--hidden`, `--format json` |
| 3 | Read | `list`, `today`, `week`, `next`, `agenda`, `search`, `show`; text/table/json/csv/tsv; filters, sort, limit/offset, visibility |
| 4 | Change | `edit` (partial fields, `--shift`, duration-preserving move), `duplicate`, hide/unhide |
| 5 | Intelligence | `conflicts`, `free --min-duration`, `alarms`, `stats` |
| 6 | Health & interop | `doctor`, `holidays`, `export csv/jsonl/ics`, `import --dry-run`, append, `--replace --preserve-ids` |
| 7 | Scripting | parse `--format json` with Python and compute a small day plan; prove an export → import round-trip |
| 8 | Shell | `completion bash`, `zsh`, `fish` (shown, not installed) |
| 9 | Encryption | key file, plaintext open fails with the hint, encrypted open succeeds |
| 10 | MCP (optional) | `initialize`, `tools/list`, `create_event`, `list_events`, `get_event`, `update_event`, `delete_event`; `--permissions read` denial |
| 11 | Alarm (optional) | `klend_ario_alarmd --once --notify log` with a trigger due now |
| 12 | Summary | a checklist of what passed, then temp-root removal |

MCP and alarm are skipped with a visible note when their binaries are not built;
the CLI section is always mandatory.

## 5. UX contract

- **Narrated, not dumped.** Each section prints a short header, why it matters,
  then `$ command` followed by its captured output.
- **Non-interactive by default.** `--pause` waits for Enter between sections;
  it never becomes the default.
- **Exit codes are asserted.** Success is `0`; an unexpected code stops the run.
  Expected failures are labelled and asserted: usage errors are `2`, runtime
  errors are `1`.
- **Colour only on a TTY.** `--no-color` forces plain output.
- **JSON is for values, text is for the reader.** The script parses
  `--format json` when it needs an id or a count; it does not scrape human text
  to make decisions. Text output is displayed because the demo is about display.
- **Secrets are fake and local.** The encryption passphrase is a throwaway
  literal, written to a `0600` file inside the temp root.

## 6. Binary discovery

The script resolves the repository root from `__file__` (`demo/` is one level
down) and looks for, in order:

1. `--cli PATH` (explicit, error if missing)
2. `build/cli/klend_ario_cli` (the dev loop), or with `--release`,
   `build_release/cli/klend_ario_cli` first
3. the other build directory as a fallback
4. `PATH`

A missing CLI exits `2` with the CMake/Ninja command to build it. The MCP server
and alarm daemon use the same search; their sections skip if absent.

## 7. Verification — definition of done

The demo is done when all of these hold:

- [ ] `ninja -C build` is green and no core/CLI files changed.
- [ ] `python3 demo/klend_ario_demo.py --no-color` exits `0`.
- [ ] Two consecutive runs both exit `0`, with a fresh database each time.
- [ ] `git status` shows only `demo/` (plus this documentation change).
- [ ] The real `state` database is unchanged (the script never receives its
      path; this is a design guarantee, checked once by hand).
- [ ] `--keep` leaves an inspectable temp root; normal exit leaves nothing.
- [ ] `--pause` works in a terminal; the default path needs no input.
- [ ] It is committed with `./git_wrapper`.

## 8. Maintenance rules

- **The CLI is frozen; the demo is the first caller to break.** If a verb, flag
  or output shape changes, fix the demo in the same change.
- **Do not invent a surface for the demo.** Every command shown must already
  exist in `--help`. No demo-only flags, no hidden mode.
- **Parse the stable shapes.** CSV/JSONL/JSON are contracts; text is allowed to
  be pretty. Assert exit codes and round-trip fields, not exact prose.
- **Keep it one stdlib file.** No pytest, no rich, no argparse framework beyond
  `argparse` and `subprocess`. The repo allows no new dependency for this.
- **Keep it disposable.** Any change that makes a run reuse a database, key, or
  export from an earlier run is a regression and must be rejected.

## 9. Non-goals

- Not a replacement for `ctest`; it cannot cover pure functions, error matrices,
  schema migrations, or encryption internals.
- Not a link into `core/`: no FFI, no C++ embedding, no Python bindings.
- Not a calendar implementation: no date math beyond formatting labels, no
  business rules, no SQL.
- Not an installer or a shell-setup script; completion is printed, not installed.
- Not a benchmark; `stats` output is shown, not timed.
- Not a holiday authority; it displays the verb and leaves the calendar to
  `pe_holiday_calc`.
