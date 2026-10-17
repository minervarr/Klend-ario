#!/usr/bin/env python3
"""Klend-ario end-to-end demo.

This is a *consumer* of the built binaries, not a second implementation. It
drives the real CLI, MCP server and alarm daemon against a throwaway database,
shows every public verb and tool, and then removes the temporary directory.

The contract lives in essentialAgent/DEMO.md. Run it after a Debug build:

    ninja -C build
    python3 demo/klend_ario_demo.py

Use --pause to step through it, --keep to inspect the temporary database, and
--release (or --cli PATH) to choose a different binary set.
"""

from __future__ import annotations

import argparse
import atexit
import datetime as dt
import json
import os
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import textwrap
from pathlib import Path

EXIT_OK = 0
EXIT_ERROR = 1
EXIT_USAGE = 2
DEMO_PREFIX = "klend_ario_demo-"
TOTAL_SECTIONS = 12


class DemoError(RuntimeError):
    """An unexpected result that should stop the demo."""


class Style:
    def __init__(self, enabled: bool) -> None:
        self.enabled = enabled

    def _wrap(self, code: str, text: str) -> str:
        return f"\033[{code}m{text}\033[0m" if self.enabled else text

    def bold(self, text: str) -> str:
        return self._wrap("1", text)

    def dim(self, text: str) -> str:
        return self._wrap("2", text)

    def cyan(self, text: str) -> str:
        return self._wrap("36", text)

    def green(self, text: str) -> str:
        return self._wrap("32", text)

    def red(self, text: str) -> str:
        return self._wrap("31", text)

    def yellow(self, text: str) -> str:
        return self._wrap("33", text)


class Narrator:
    def __init__(self, style: Style) -> None:
        self.style = style

    def banner(self) -> None:
        print(self.style.bold("Klend-ario — end-to-end demo"))
        print(
            self.style.dim(
                "A narrated tour of the CLI, the MCP server and the alarm engine."
            )
        )
        print(self.style.dim("Every run starts from an empty temporary database."))

    def section(self, number: int, title: str, blurb: str) -> None:
        print()
        heading = f"[{number}/{TOTAL_SECTIONS}] {title}"
        print(self.style.bold(self.style.cyan(heading)))
        print(
            textwrap.fill(
                blurb,
                width=78,
                initial_indent="  ",
                subsequent_indent="  ",
            )
        )

    def step(self, text: str) -> None:
        print()
        print(
            textwrap.fill(
                text,
                width=78,
                initial_indent="  ",
                subsequent_indent="  ",
            )
        )

    def command(self, display: str) -> None:
        print("  " + self.style.dim("$ ") + display)

    def output(self, text: str) -> None:
        body = text.rstrip("\n")
        if not body:
            print("  " + self.style.dim("(no output)"))
            return
        for line in body.splitlines():
            print("  " + line)

    def note(self, text: str, *, wrap: bool = True) -> None:
        if wrap:
            text = textwrap.fill(
                text,
                width=78,
                initial_indent="  ",
                subsequent_indent="  ",
            )
        else:
            text = "  " + text
        print(self.style.dim(text))

    def code(self, text: str) -> None:
        for line in text.splitlines():
            print("  " + self.style.dim(line))

    def mcp(self, arrow: str, text: str) -> None:
        print("  " + self.style.yellow(arrow + " ") + text)

    def error(self, text: str) -> None:
        print(self.style.red("error: ") + text, file=sys.stderr)


class Demo:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.style = Style(sys.stdout.isatty() and not args.no_color)
        self.narrator = Narrator(self.style)
        self.root = Path(__file__).resolve().parent.parent
        self.prefer_release = args.release
        self.cli = self._find_cli()
        self.mcp = self._find_optional("klend_ario_mcp_server", "mcp")
        self.alarm = self._find_optional("klend_ario_alarmd", "alarm")

        self.tmp = Path(tempfile.mkdtemp(prefix=DEMO_PREFIX))
        self.calendar_db = self.tmp / "calendar.db"
        self.import_db = self.tmp / "import.db"
        self.secret_db = self.tmp / "secret.db"
        self.mcp_db = self.tmp / "mcp.db"
        self.alarm_db = self.tmp / "alarm.db"
        self.key_file = self.tmp / "key.txt"
        self.wrong_key_file = self.tmp / "wrong-key.txt"
        self.export_csv = self.tmp / "export.csv"
        self.export_jsonl = self.tmp / "export.jsonl"
        self.export_ics = self.tmp / "export.ics"
        self.bad_import = self.tmp / "bad.jsonl"

        self.keep = args.keep
        self.pause_enabled = args.pause
        self.cleaned = False
        self.cli_calls = 0
        self.mcp_calls = 0
        self.mcp_done = False
        self.alarm_done = False

        atexit.register(self.cleanup)
        signal.signal(signal.SIGINT, self._on_signal)
        signal.signal(signal.SIGTERM, self._on_signal)

    # ------------------------------------------------------------------
    # discovery / isolation
    # ------------------------------------------------------------------

    def _build_dirs(self) -> list[Path]:
        names = ["build_release", "build"] if self.prefer_release else ["build", "build_release"]
        return [self.root / name for name in names]

    def _find_cli(self) -> Path:
        if self.args.cli:
            path = Path(self.args.cli).expanduser()
            if not path.is_file() or not os.access(path, os.X_OK):
                raise DemoError(f"--cli is not an executable file: {path}")
            return path.resolve()

        for base in self._build_dirs():
            path = base / "cli" / "klend_ario_cli"
            if path.is_file() and os.access(path, os.X_OK):
                return path.resolve()

        found = shutil.which("klend_ario_cli")
        if found:
            return Path(found).resolve()

        raise DemoError(
            "cannot find klend_ario_cli; build it first:\n"
            "  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug\n"
            "  ninja -C build"
        )

    def _find_optional(self, name: str, subdir: str) -> Path | None:
        candidates: list[Path] = [self.cli.parent.parent / subdir / name]
        for base in self._build_dirs():
            candidates.append(base / subdir / name)
        found = shutil.which(name)
        if found:
            candidates.append(Path(found))

        for path in candidates:
            if path.is_file() and os.access(path, os.X_OK):
                return path.resolve()
        return None

    def cleanup(self) -> None:
        if self.cleaned:
            return
        self.cleaned = True
        if self.keep:
            return
        shutil.rmtree(self.tmp, ignore_errors=True)

    def _on_signal(self, signum: int, _frame: object) -> None:
        self.cleanup()
        sys.exit(128 + signum)

    def _display(self, argv: list[object]) -> str:
        parts: list[str] = []
        tmp = str(self.tmp)
        for item in argv:
            text = str(item)
            if text.startswith(tmp):
                text = "TMP" + text[len(tmp) :]
            if text == str(self.cli):
                text = "klend_ario_cli"
            elif self.mcp is not None and text == str(self.mcp):
                text = "klend_ario_mcp_server"
            elif self.alarm is not None and text == str(self.alarm):
                text = "klend_ario_alarmd"
            parts.append(shlex.quote(text))
        return " ".join(parts)

    # ------------------------------------------------------------------
    # command helpers
    # ------------------------------------------------------------------

    def run(
        self,
        args: list[object],
        *,
        db: Path | bool | None = None,
        key: Path | None = None,
        expect: int = EXIT_OK,
        show: bool = True,
        save_to: Path | None = None,
        label: str | None = None,
    ) -> subprocess.CompletedProcess[str]:
        argv: list[str] = [str(self.cli)] + [str(arg) for arg in args]
        if db is None:
            db = self.calendar_db
        if db is not False:
            argv += ["--db", str(db)]
        if key is not None:
            argv += ["--key-file", str(key)]

        if label:
            self.narrator.step(label)
        self.narrator.command(self._display(argv))

        completed = subprocess.run(argv, capture_output=True, text=True)
        self.cli_calls += 1
        combined = completed.stdout + completed.stderr
        if save_to is not None:
            save_to.write_text(completed.stdout)
        if show:
            self.narrator.output(combined)

        if completed.returncode != expect:
            raise DemoError(
                f"command exited {completed.returncode}, expected {expect}: "
                f"{self._display(argv)}"
            )
        if expect != EXIT_OK:
            self.narrator.note(f"(exit {completed.returncode} — expected)")
        return completed

    def add(self, args: list[object], *, db: Path | None = None, key: Path | None = None) -> int:
        completed = self.run(["add", *args, "--format", "json"], db=db, key=key)
        try:
            return int(json.loads(completed.stdout)["id"])
        except (KeyError, TypeError, ValueError, json.JSONDecodeError) as exc:
            raise DemoError(f"add did not print a JSON id: {completed.stdout!r}") from exc

    def pause(self) -> None:
        if not self.pause_enabled:
            return
        print("  " + self.style.dim("press Enter to continue..."), end="", flush=True)
        try:
            input()
        except EOFError:
            print()
            self.narrator.note("stdin closed; continuing without pauses.")
            self.pause_enabled = False

    def end_section(self) -> None:
        self.pause()

    # ------------------------------------------------------------------
    # sections
    # ------------------------------------------------------------------

    def run_demo(self) -> None:
        self.narrator.banner()
        self.section_setup()
        self.section_create()
        self.section_read()
        self.section_change()
        self.section_intelligence()
        self.section_health()
        self.section_scripting()
        self.section_shell()
        self.section_encryption()
        self.section_mcp()
        self.section_alarm()
        self.section_summary()

    def section_setup(self) -> None:
        self.narrator.section(
            1,
            "Setup and isolation",
            "The demo is a consumer of the built binaries. It creates one private "
            "temporary directory and passes an explicit --db to every database "
            "command, so the real state database is never opened.",
        )
        self.narrator.note(f"repo root: {self.root}", wrap=False)
        self.narrator.note(f"CLI:       {self.cli}", wrap=False)
        self.narrator.note(f"temp dir:  {self.tmp}", wrap=False)
        self.narrator.note(
            "In the commands below, TMP is that temporary directory. Every "
            "database command gets --db TMP/calendar.db; holidays, completion, "
            "help and --version never open a database."
        )
        self.run(["--version"], db=False)
        self.run(["help", "add"], db=False)
        self.end_section()

    def section_create(self) -> None:
        self.narrator.section(
            2,
            "Create",
            "Seven rows cover the first things a user meets: a normal event, an "
            "end-time instead of a duration, a --title option, the sleep/wake/alarm "
            "types, an alarm offset, notes, importance/urgency and a hidden row.",
        )

        self.ids: dict[str, int] = {}
        self.ids["focus"] = self.add(
            [
                "Focus block",
                "--start",
                "today+9h",
                "--duration",
                "90m",
                "--importance",
                "80",
                "--urgency",
                "40",
                "--alarm-offset",
                "600",
                "--notes",
                "Deep work on the demo",
            ]
        )
        self.ids["design"] = self.add(
            [
                "Design review",
                "--start",
                "today+570m",
                "--end",
                "today+615m",
                "--importance",
                "60",
                "--urgency",
                "70",
                "--notes",
                "Walk through the weekly view",
            ]
        )
        self.ids["lunch"] = self.add(
            [
                "--title",
                "Lunch with Ana",
                "--start",
                "today+13h",
                "--duration",
                "1h",
                "--importance",
                "30",
                "--urgency",
                "20",
                "--notes",
                "Patio if it is sunny",
            ]
        )
        self.ids["sleep"] = self.add(
            [
                "Sleep",
                "--start",
                "today+23h",
                "--duration",
                "8h",
                "--type",
                "sleep",
                "--notes",
                "The automatic 10h sleep block is post-MVP",
            ]
        )
        self.ids["wake"] = self.add(
            [
                "Wake-up",
                "--start",
                "tomorrow+450m",
                "--duration",
                "15m",
                "--type",
                "wake",
                "--alarm-offset",
                "300",
                "--notes",
                "Sunrise run",
            ]
        )
        self.ids["medication"] = self.add(
            [
                "Medication",
                "--start",
                "tomorrow+9h",
                "--duration",
                "5m",
                "--type",
                "alarm",
                "--importance",
                "90",
                "--urgency",
                "90",
                "--notes",
                "Take with water",
            ]
        )
        self.ids["private"] = self.add(
            [
                "Private note",
                "--start",
                "today+15h",
                "--duration",
                "30m",
                "--hidden",
                "--notes",
                "Only in hidden views",
            ]
        )

        if len(self.ids) != 7:
            raise DemoError("the create section did not produce seven ids")
        self.narrator.note("IDs were captured from add --format json; later sections use them.")
        self.end_section()

    def section_read(self) -> None:
        self.narrator.section(
            3,
            "Read and formats",
            "The same rows through the everyday views, the hidden-row flags, every "
            "output format, search, and a single record by id.",
        )
        self.run(["today"], label="Today in the default text format (visible rows only).")
        self.run(
            ["week", "--format", "table"],
            label="The Sunday-based week as a table. Compare it with agenda below.",
        )
        self.run(["next"], label="The next event still running or starting.")
        self.run(
            ["agenda", "--days", "2", "--format", "table"],
            label="A two-day agenda: from now, across the week boundary.",
        )
        self.run(
            ["list", "--from", "today", "--to", "today+2d", "--format", "json", "--limit", "2"],
            label="Machine-readable JSON. --limit keeps this example short.",
        )
        self.run(
            ["list", "--from", "today", "--to", "today+2d", "--format", "csv", "--limit", "3"],
            label="CSV carries the stable field set.",
        )
        self.run(
            [
                "list",
                "--from",
                "today",
                "--to",
                "today+2d",
                "--format",
                "tsv",
                "--no-header",
                "--limit",
                "3",
            ],
            label="TSV without a header, for cut/awk pipelines.",
        )
        self.run(
            [
                "list",
                "--from",
                "today",
                "--to",
                "today+2d",
                "--min-importance",
                "70",
                "--sort",
                "importance",
                "--format",
                "table",
            ],
            label="Filters and sorting compose: importance >= 70, ascending.",
        )
        self.run(["search", "demo", "--format", "table"], label="Literal search in titles and notes.")
        self.run(
            ["today", "--hidden-only", "--format", "table"],
            label="Hidden rows are opt-in in the day view.",
        )
        self.run(
            ["show", self.ids["focus"], "--format", "json"],
            label="One event by id. JSON is one object, not an array.",
        )
        self.end_section()

    def section_change(self) -> None:
        self.narrator.section(
            4,
            "Change",
            "Edits are partial: only the flags you pass change. Moving a start "
            "preserves duration, --shift moves the whole event, and rm is idempotent.",
        )
        self.run(
            ["edit", self.ids["focus"], "--duration", "2h"],
            label="Change one field. The start is untouched; the duration becomes two hours.",
        )
        self.run(
            ["edit", self.ids["lunch"], "--shift", "30m"],
            label="Move the whole event: the start and end shift together.",
        )
        self.run(
            [
                "edit",
                self.ids["lunch"],
                "--title",
                "Lunch with Ana and Luis",
                "--notes",
                "Moved to the patio",
            ],
            label="A second partial edit changes only title and notes.",
        )
        self.run(
            ["show", self.ids["lunch"], "--format", "text"],
            label="The result of those two edits.",
        )
        self.run(
            ["edit", self.ids["private"], "--unhide"],
            label="Unhide the private row so the day view can show it.",
        )
        self.run(["today", "--format", "table"], label="Now it appears in the visible view.")
        self.run(
            ["edit", self.ids["private"], "--hidden"],
            label="Hide it again; the row is not deleted.",
        )
        copy = self.run(
            ["duplicate", self.ids["lunch"], "--start", "today+16h", "--format", "json"],
            label="Duplicate copies every field and prints the new id.",
        )
        try:
            copy_id = int(json.loads(copy.stdout)["id"])
        except (KeyError, TypeError, ValueError, json.JSONDecodeError) as exc:
            raise DemoError(f"duplicate did not print a JSON id: {copy.stdout!r}") from exc
        self.run(["rm", copy_id], label="Delete the copy.")
        self.run(
            ["rm", copy_id],
            label="Delete it again: removing a missing id succeeds, so scripts need no pre-check.",
        )
        self.end_section()

    def section_intelligence(self) -> None:
        self.narrator.section(
            5,
            "Intelligence",
            "Conflicts are pairwise overlaps; free time merges busy blocks; alarms "
            "show both T-offset and T=0 triggers; stats is the summary a screen "
            "or a report can start from.",
        )
        self.run(
            ["conflicts", "--from", "today", "--to", "today+2d", "--format", "table"],
            label="Focus and Design review overlap; the sleep/wake pair no longer does.",
        )
        self.run(
            [
                "free",
                "--from",
                "today+8h",
                "--to",
                "today+18h",
                "--min-duration",
                "30m",
                "--format",
                "table",
            ],
            label="Gaps of at least 30 minutes, with overlapping busy time merged.",
        )
        self.run(
            ["alarms", "--from", "today", "--to", "today+2d", "--format", "table"],
            label="Every trigger in the window, including the quieter T-offset rows.",
        )
        stats = self.run(
            ["stats", "--format", "json"],
            label="Stats is JSON-friendly: counts, types, span and conflict pairs.",
        )
        try:
            summary = json.loads(stats.stdout)
        except json.JSONDecodeError as exc:
            raise DemoError(f"stats did not print JSON: {stats.stdout!r}") from exc
        if summary.get("total") != 7 or summary.get("hidden") != 1:
            raise DemoError(f"unexpected stats: {summary}")
        if summary.get("by_type", {}).get("sleep") != 1:
            raise DemoError(f"stats lost the sleep row: {summary}")
        self.narrator.note("Seven rows total, one hidden, one sleep — the shape the demo created.")
        self.end_section()

    def section_health(self) -> None:
        self.narrator.section(
            6,
            "Health and interop",
            "doctor is a read-only health report. export writes CSV, JSONL or ICS; "
            "import validates the whole file before touching the database and can "
            "append or replace.",
        )
        self.run(["doctor", "--format", "table"], label="A fresh database should pass every check.")

        holidays = self.run(
            ["holidays", "--country", "PE", "--format", "table"],
            db=False,
            show=False,
            label="Holidays are a civil-date query; this verb never opens a database.",
        )
        lines = holidays.stdout.splitlines()
        shown = "\n".join(lines[:8])
        if len(lines) > 8:
            shown += "\n..."
        self.narrator.output(shown)
        self.narrator.note(f"({len(lines) - 1} holiday rows for Peru, current year)")

        self.run(
            ["export", "--format", "csv"],
            save_to=self.export_csv,
            label="CSV export: every stored field, written to stdout and captured here.",
        )
        self.narrator.note(f"saved to {self._display([self.export_csv])}")

        jsonl = self.run(
            ["export", "--format", "jsonl"],
            save_to=self.export_jsonl,
            show=False,
            label="JSONL export: one object per line, lossless.",
        )
        jsonl_lines = jsonl.stdout.splitlines()
        self.narrator.output("\n".join(jsonl_lines[:2]) + ("\n..." if len(jsonl_lines) > 2 else ""))
        self.narrator.note(f"saved {len(jsonl_lines)} lines to {self._display([self.export_jsonl])}")

        ics = self.run(
            ["export", "--format", "ics"],
            save_to=self.export_ics,
            show=False,
            label="ICS export is one-way interoperability, with a VALARM for the offset.",
        )
        ics_lines = ics.stdout.splitlines()
        self.narrator.output("\n".join(ics_lines[:12]) + ("\n..." if len(ics_lines) > 12 else ""))
        self.narrator.note(f"saved {len(ics_lines)} lines to {self._display([self.export_ics])}")

        self.run(
            ["import", self.export_jsonl, "--format", "jsonl", "--dry-run"],
            db=self.import_db,
            label="Dry-run: parse and validate, write nothing.",
        )
        self.run(
            ["import", self.export_jsonl, "--format", "jsonl"],
            db=self.import_db,
            label="Import into a second database: append with fresh local ids.",
        )
        imported = self.run(
            ["stats", "--format", "json"],
            db=self.import_db,
            label="The imported database has the same seven rows.",
        )
        try:
            imported_stats = json.loads(imported.stdout)
        except json.JSONDecodeError as exc:
            raise DemoError(f"imported stats did not print JSON: {imported.stdout!r}") from exc
        if imported_stats.get("total") != 7:
            raise DemoError(f"import lost rows: {imported_stats}")

        self.run(
            ["import", self.export_jsonl, "--format", "jsonl", "--replace", "--preserve-ids"],
            db=self.import_db,
            label="Restore in one transaction and keep the file's ids.",
        )
        restored = self.run(
            ["list", "--from", "today", "--to", "today+2d", "--format", "json"],
            db=self.import_db,
            show=False,
            label="Read the restored ids back.",
        )
        try:
            restored_rows = json.loads(restored.stdout)
        except json.JSONDecodeError as exc:
            raise DemoError(f"restored list did not print JSON: {restored.stdout!r}") from exc
        if not restored_rows or restored_rows[0]["id"] != self.ids["focus"]:
            raise DemoError(f"preserve-ids did not restore the first id: {restored_rows[:1]}")
        self.narrator.output(
            f"{len(restored_rows)} rows; first id={restored_rows[0]['id']} "
            f"({restored_rows[0]['title']})"
        )

        self.bad_import.write_text(
            '{"title":"Broken","start":"not-a-time","end":"2026-10-17T10:00:00Z"}\n'
        )
        self.run(
            ["import", self.bad_import, "--format", "jsonl", "--dry-run"],
            db=self.import_db,
            expect=EXIT_ERROR,
            label="A bad row rejects the whole file. This is an expected failure.",
        )
        still = self.run(
            ["stats", "--format", "json"],
            db=self.import_db,
            show=False,
            label="The failed import left the database untouched.",
        )
        try:
            still_stats = json.loads(still.stdout)
        except json.JSONDecodeError as exc:
            raise DemoError(f"stats after failed import did not print JSON: {still.stdout!r}") from exc
        if still_stats.get("total") != 7:
            raise DemoError(f"a rejected import changed the database: {still_stats}")
        self.narrator.output(f"still {still_stats['total']} rows")
        self.end_section()

    def section_scripting(self) -> None:
        self.narrator.section(
            7,
            "Scripting",
            "The JSON output is the stable contract. This section reads it with "
            "Python, builds a small day plan and totals scheduled minutes without "
            "scraping a human table.",
        )
        self.narrator.code(
            "rows = json.loads(cli('list', '--from', 'today', '--to', 'today+2d',\n"
            "                      '--visible', '--format', 'json'))\n"
            "for row in rows:\n"
            "    print(row['start'][11:16], row['title'])"
        )
        completed = self.run(
            [
                "list",
                "--from",
                "today",
                "--to",
                "today+2d",
                "--visible",
                "--format",
                "json",
            ],
            show=False,
            label="Capture the visible rows as JSON.",
        )
        try:
            rows = json.loads(completed.stdout)
        except json.JSONDecodeError as exc:
            raise DemoError(f"scripting list did not print JSON: {completed.stdout!r}") from exc
        if len(rows) != 6:
            raise DemoError(f"expected six visible rows, got {len(rows)}")

        def parse(value: str) -> dt.datetime:
            return dt.datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ").replace(
                tzinfo=dt.timezone.utc
            )

        total_minutes = 0.0
        for row in rows:
            total_minutes += (parse(row["end"]) - parse(row["start"])).total_seconds() / 60.0
        self.narrator.output(f"{len(rows)} visible rows")
        for row in rows:
            when = row["start"][5:16].replace("T", " ")
            self.narrator.output(f"  {when}  {row['title']}")
        self.narrator.output(f"{total_minutes:.0f} scheduled minutes")
        self.end_section()

    def section_shell(self) -> None:
        self.narrator.section(
            8,
            "Shell completion",
            "Completion scripts are printed, not installed. Source the one you "
            "want from your shell's startup file. This verb never opens a database.",
        )
        bash = self.run(
            ["completion", "bash"],
            db=False,
            show=False,
            label="Bash: a function plus the verb list.",
        )
        bash_lines = bash.stdout.splitlines()
        self.narrator.output("\n".join(bash_lines[:8]) + ("\n..." if len(bash_lines) > 8 else ""))
        self.narrator.note(f"({len(bash_lines)} lines; source it, do not run it)")
        for shell in ("zsh", "fish"):
            completed = self.run(
                ["completion", shell],
                db=False,
                show=False,
                label=f"{shell}: the same surface in that shell's dialect.",
            )
            lines = completed.stdout.splitlines()
            self.narrator.output(lines[0] if lines else "(no output)")
            self.narrator.note(f"({len(lines)} lines)")
        self.end_section()

    def section_encryption(self) -> None:
        self.narrator.section(
            9,
            "Encryption",
            "A key file turns the same database into an encrypted one. Plaintext "
            "open fails with a hint; the right key succeeds; the wrong key fails. "
            "The key here is a throwaway literal inside the temporary directory.",
        )
        self.key_file.write_text("demo-passphrase-not-a-secret\n")
        os.chmod(self.key_file, 0o600)
        self.wrong_key_file.write_text("definitely-the-wrong-key\n")
        os.chmod(self.wrong_key_file, 0o600)

        encrypted_id = self.add(
            [
                "Encrypted planning",
                "--start",
                "today+16h",
                "--duration",
                "30m",
                "--notes",
                "Visible only with the key",
            ],
            db=self.secret_db,
            key=self.key_file,
        )
        self.narrator.note(f"created id {encrypted_id} in {self._display([self.secret_db])}")

        plain = self.run(
            ["list"],
            db=self.secret_db,
            expect=EXIT_ERROR,
            label="Without the key, the encrypted file is rejected. This is expected.",
        )
        if "if the database is encrypted" not in plain.stderr:
            raise DemoError("the plaintext error did not include the key-file hint")

        opened = self.run(
            ["list", "--format", "json"],
            db=self.secret_db,
            key=self.key_file,
            label="With the key, the same rows open normally.",
        )
        try:
            secret_rows = json.loads(opened.stdout)
        except json.JSONDecodeError as exc:
            raise DemoError(f"encrypted list did not print JSON: {opened.stdout!r}") from exc
        if len(secret_rows) != 1 or secret_rows[0]["title"] != "Encrypted planning":
            raise DemoError(f"unexpected encrypted rows: {secret_rows}")

        self.run(
            ["list"],
            db=self.secret_db,
            key=self.wrong_key_file,
            expect=EXIT_ERROR,
            label="The wrong key is rejected too. Also expected.",
        )
        self.narrator.note("key file mode is 0600 inside the temporary directory.")
        self.end_section()

    # ------------------------------------------------------------------
    # MCP
    # ------------------------------------------------------------------

    def _mcp_start(self, permissions: str | None = None) -> subprocess.Popen[str]:
        if self.mcp is None:
            raise DemoError("MCP server is not available")
        argv = [str(self.mcp), "--db", str(self.mcp_db)]
        if permissions is not None:
            argv += ["--permissions", permissions]
        self.narrator.command(self._display(argv))
        return subprocess.Popen(
            argv,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )

    def _mcp_rpc(
        self,
        proc: subprocess.Popen[str],
        request_id: int,
        method: str,
        params: dict[str, object] | None = None,
    ) -> dict[str, object]:
        request: dict[str, object] = {
            "jsonrpc": "2.0",
            "id": request_id,
            "method": method,
        }
        if params is not None:
            request["params"] = params
        self.narrator.mcp("→", json.dumps(request, separators=(",", ":"), ensure_ascii=False))
        assert proc.stdin is not None
        proc.stdin.write(json.dumps(request) + "\n")
        proc.stdin.flush()
        assert proc.stdout is not None
        line = proc.stdout.readline()
        if not line:
            assert proc.stderr is not None
            raise DemoError(f"MCP server closed the connection: {proc.stderr.read()}")
        self.mcp_calls += 1
        try:
            return json.loads(line)
        except json.JSONDecodeError as exc:
            raise DemoError(f"MCP server returned invalid JSON: {line!r}") from exc

    @staticmethod
    def _mcp_text(response: dict[str, object]) -> str:
        error = response.get("error")
        if isinstance(error, dict):
            return f"error {error.get('code')}: {error.get('message')}"
        result = response.get("result")
        if not isinstance(result, dict):
            return ""
        content = result.get("content")
        if isinstance(content, list) and content and isinstance(content[0], dict):
            text = content[0].get("text")
            if isinstance(text, str):
                return text
        return ""

    @staticmethod
    def _mcp_is_error(response: dict[str, object]) -> bool:
        result = response.get("result")
        return isinstance(result, dict) and bool(result.get("isError"))

    def _mcp_close(self, proc: subprocess.Popen[str]) -> None:
        if proc.stdin is not None:
            try:
                proc.stdin.close()
            except OSError:
                pass
        try:
            returncode = proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            returncode = proc.wait()
        if proc.stderr is not None:
            stderr = proc.stderr.read()
            if stderr.strip():
                self.narrator.note(stderr.strip())
        if returncode != EXIT_OK:
            raise DemoError(f"MCP server exited {returncode}")

    def section_mcp(self) -> None:
        self.narrator.section(
            10,
            "MCP server",
            "The MCP server speaks newline-delimited JSON-RPC on stdin/stdout. "
            "A client can create, read, update and delete through the same core "
            "as the CLI; permissions decide which tiers are exposed.",
        )
        if self.mcp is None:
            self.narrator.note("skipped: build/mcp/klend_ario_mcp_server was not found")
            self.end_section()
            return

        now = dt.datetime.now(dt.timezone.utc).replace(microsecond=0)
        start = (now + dt.timedelta(hours=18)).strftime("%Y-%m-%dT%H:%M:%SZ")
        end = (now + dt.timedelta(hours=18, minutes=30)).strftime("%Y-%m-%dT%H:%M:%SZ")

        proc: subprocess.Popen[str] | None = None
        try:
            proc = self._mcp_start()
            init = self._mcp_rpc(proc, 1, "initialize", {})
            result = init.get("result", {})
            if not isinstance(result, dict):
                raise DemoError(f"initialize returned no result: {init}")
            info = result.get("serverInfo", {})
            if not isinstance(info, dict):
                info = {}
            self.narrator.mcp(
                "←",
                f"{info.get('name', '?')} {info.get('version', '?')} "
                f"(protocol {result.get('protocolVersion', '?')})",
            )
            instructions = result.get("instructions")
            if not isinstance(instructions, str) or not instructions.strip():
                raise DemoError("MCP initialize did not return instructions")
            self.narrator.mcp(
                "←", f"instructions: {len(instructions.splitlines())} lines of calendar guidance"
            )

            listed = self._mcp_rpc(proc, 2, "tools/list")
            result = listed.get("result", {})
            tools = result.get("tools", []) if isinstance(result, dict) else []
            names = [tool.get("name") for tool in tools if isinstance(tool, dict)]
            expected_tools = {
                "list_events",
                "get_event",
                "create_event",
                "update_event",
                "delete_event",
            }
            if set(names) != expected_tools:
                raise DemoError(f"unexpected MCP tool set: {names}")
            for tool in tools:
                if not isinstance(tool, dict) or not isinstance(tool.get("title"), str):
                    raise DemoError(f"MCP tool without a human title: {tool}")
                if not isinstance(tool.get("annotations"), dict):
                    raise DemoError(f"MCP tool without annotations: {tool}")
            self.narrator.mcp("←", f"{len(names)} tools: {', '.join(str(name) for name in names)}")
            self.narrator.mcp("←", "titles and annotations: list/get are read-only, delete is destructive")

            create = self._mcp_rpc(
                proc,
                3,
                "tools/call",
                {
                    "name": "create_event",
                    "arguments": {
                        "title": "MCP sync",
                        "start": start,
                        "end": end,
                        "importance": 55,
                        "urgency": 45,
                        "notes": "created over JSON-RPC",
                    },
                },
            )
            created_text = self._mcp_text(create)
            self.narrator.mcp("←", created_text)
            try:
                event_id = int(json.loads(created_text)["id"])
            except (KeyError, TypeError, ValueError, json.JSONDecodeError) as exc:
                raise DemoError(f"MCP create_event did not return an id: {created_text!r}") from exc

            fetched = self._mcp_rpc(proc, 4, "tools/call", {"name": "get_event", "arguments": {"id": event_id}})
            fetched_text = self._mcp_text(fetched)
            self.narrator.mcp("←", fetched_text)
            try:
                fetched_event = json.loads(fetched_text)
            except json.JSONDecodeError as exc:
                raise DemoError(f"MCP get_event did not return JSON: {fetched_text!r}") from exc
            if fetched_event.get("title") != "MCP sync":
                raise DemoError(f"MCP get_event returned the wrong row: {fetched_event}")

            updated = self._mcp_rpc(
                proc,
                5,
                "tools/call",
                {
                    "name": "update_event",
                    "arguments": {"id": event_id, "title": "MCP sync (updated)"},
                },
            )
            updated_text = self._mcp_text(updated)
            self.narrator.mcp("←", updated_text)
            try:
                updated_event = json.loads(updated_text)
            except json.JSONDecodeError as exc:
                raise DemoError(f"MCP update_event did not return JSON: {updated_text!r}") from exc
            if updated_event.get("title") != "MCP sync (updated)":
                raise DemoError(f"MCP update_event did not change the title: {updated_event}")

            listed_events = self._mcp_rpc(
                proc,
                6,
                "tools/call",
                {
                    "name": "list_events",
                    "arguments": {"from": start, "to": end},
                },
            )
            listed_text = self._mcp_text(listed_events)
            try:
                mcp_rows = json.loads(listed_text)
            except json.JSONDecodeError as exc:
                raise DemoError(f"MCP list_events did not return JSON: {listed_text!r}") from exc
            if len(mcp_rows) != 1:
                raise DemoError(f"MCP list_events returned {len(mcp_rows)} rows")
            self.narrator.mcp(
                "←",
                f"{len(mcp_rows)} event: [{mcp_rows[0]['id']}] {mcp_rows[0]['title']}",
            )

            deleted = self._mcp_rpc(
                proc,
                7,
                "tools/call",
                {"name": "delete_event", "arguments": {"id": event_id}},
            )
            self.narrator.mcp("←", self._mcp_text(deleted))

            gone = self._mcp_rpc(
                proc,
                8,
                "tools/call",
                {"name": "get_event", "arguments": {"id": event_id}},
            )
            gone_text = self._mcp_text(gone)
            self.narrator.mcp("←", f"isError={self._mcp_is_error(gone)}: {gone_text}")
            if not self._mcp_is_error(gone):
                raise DemoError("MCP get_event after delete did not report an error")
        finally:
            if proc is not None:
                self._mcp_close(proc)

        restricted: subprocess.Popen[str] | None = None
        try:
            restricted = self._mcp_start("read")
            self._mcp_rpc(restricted, 1, "initialize", {})
            self.narrator.mcp("←", "read-only session initialised")
            read_list = self._mcp_rpc(restricted, 2, "tools/list")
            read_result = read_list.get("result", {})
            read_tools = read_result.get("tools", []) if isinstance(read_result, dict) else []
            read_names = sorted(
                str(tool.get("name")) for tool in read_tools if isinstance(tool, dict)
            )
            if read_names != ["get_event", "list_events"]:
                raise DemoError(f"read-only server advertised {read_names}")
            self.narrator.mcp("←", f"read-only tools: {', '.join(read_names)}")
            denied = self._mcp_rpc(
                restricted,
                3,
                "tools/call",
                {
                    "name": "create_event",
                    "arguments": {"title": "Nope", "start": start, "end": end},
                },
            )
            denied_text = self._mcp_text(denied)
            self.narrator.mcp("←", f"isError={self._mcp_is_error(denied)}: {denied_text}")
            if not self._mcp_is_error(denied) or "permission denied" not in denied_text:
                raise DemoError(f"read-only server did not deny create_event: {denied}")
        finally:
            if restricted is not None:
                self._mcp_close(restricted)

        self.mcp_done = True
        self.narrator.note(
            "Five tools exercised; the read-only server advertised only list/get and denied the write tier."
        )
        self.end_section()

    # ------------------------------------------------------------------
    # alarm
    # ------------------------------------------------------------------

    def section_alarm(self) -> None:
        self.narrator.section(
            11,
            "Alarm engine",
            "The alarm daemon is its own frontend. A one-poll run with --notify log "
            "makes it deterministic: the event starts in the past, so both its "
            "T-offset and T=0 triggers are already due.",
        )
        if self.alarm is None:
            self.narrator.note("skipped: build/alarm/klend_ario_alarmd was not found")
            self.end_section()
            return

        self.add(
            [
                "Alarm demo",
                "--start",
                "now-1m",
                "--duration",
                "15m",
                "--alarm-offset",
                "60",
                "--notes",
                "Due before the daemon polls",
            ],
            db=self.alarm_db,
        )
        argv = [
            str(self.alarm),
            "--db",
            str(self.alarm_db),
            "--once",
            "--notify",
            "log",
            "--horizon",
            "86400",
        ]
        self.narrator.command(self._display(argv))
        completed = subprocess.run(argv, capture_output=True, text=True)
        self.narrator.output(completed.stderr)
        if completed.returncode != EXIT_OK:
            raise DemoError(f"alarm daemon exited {completed.returncode}")
        if "alarm: Alarm demo" not in completed.stderr:
            raise DemoError("alarm daemon did not fire the demo event")
        self.alarm_done = True
        self.narrator.note("--horizon 86400 keeps the trigger due even if --pause slows the tour.")
        self.end_section()

    # ------------------------------------------------------------------
    # summary
    # ------------------------------------------------------------------

    def section_summary(self) -> None:
        self.narrator.section(
            12,
            "Done",
            "One run exercised every public surface against a database that no "
            "other run can see. Nothing outside the temporary directory was touched.",
        )
        self.narrator.output("21 CLI verbs exercised (add through holidays)")
        self.narrator.output("5 MCP tools exercised" if self.mcp_done else "MCP tools skipped (binary not built)")
        self.narrator.output(
            "alarm daemon fired a due trigger"
            if self.alarm_done
            else "alarm daemon skipped (binary not built)"
        )
        self.narrator.output("encrypted database: plaintext rejected, right key accepted, wrong key rejected")
        self.narrator.output(f"{self.cli_calls} CLI commands; {self.mcp_calls} MCP requests")
        self.narrator.output("the real state database was never opened")

        if self.keep:
            self.narrator.note(f"kept demo directory: {self.tmp}", wrap=False)
            self.narrator.note(f"inspect: sqlite3 {self.calendar_db}", wrap=False)
        else:
            self.cleanup()
            self.narrator.note("temporary demo directory removed")
        print()
        print(self.style.bold(self.style.green("Demo complete.")))


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Narrated end-to-end demo of the Klend-ario CLI, MCP server and alarm daemon.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Every run uses a fresh temporary database. --keep preserves the directory.\n"
            "The real state database is never opened."
        ),
    )
    parser.add_argument("--pause", action="store_true", help="wait for Enter between sections")
    parser.add_argument("--keep", action="store_true", help="keep the temporary demo directory")
    parser.add_argument("--no-color", action="store_true", help="disable ANSI color")
    parser.add_argument("--cli", metavar="PATH", help="explicit klend_ario_cli binary")
    parser.add_argument(
        "--release",
        action="store_true",
        help="prefer build_release/ binaries over build/",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    demo: Demo | None = None
    try:
        demo = Demo(args)
        demo.run_demo()
        return EXIT_OK
    except DemoError as exc:
        if demo is not None:
            demo.narrator.error(str(exc))
        else:
            print(f"error: {exc}", file=sys.stderr)
        return EXIT_ERROR
    except KeyboardInterrupt:
        print("\ninterrupted", file=sys.stderr)
        return 130
    finally:
        if demo is not None:
            demo.cleanup()


if __name__ == "__main__":
    sys.exit(main())
