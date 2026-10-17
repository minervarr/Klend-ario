#!/usr/bin/env python3
"""Focused end-to-end smoke test for the Klend-ario MCP server.

The MCP server speaks newline-delimited JSON-RPC on stdin/stdout. This script
drives the real binary exactly as an MCP host would, against a throwaway
database, and then removes every trace:

    ninja -C build
    python3 demo/mcp_smoke.py

It asserts the handshake, the tool schemas, all five tools, the error path, the
read-only permission tier, stdout purity, and (when the CLI is built) that MCP
writes are visible through the CLI on the same database.

Unlike demo/klend_ario_demo.py this is quiet and focused: one line per passing
check, a non-zero exit on the first failure, and no narration. The contract
lives in demo/README.md. Standard library only; no third-party packages.
"""

from __future__ import annotations

import argparse
import atexit
import json
import os
import select
import shutil
import signal
import subprocess
import sys
import tempfile
from pathlib import Path

EXIT_OK = 0
EXIT_ERROR = 1
EXIT_USAGE = 2
ROOT_PREFIX = "klend_ario_mcp_smoke-"

TOOL_NAMES = {"list_events", "get_event", "create_event", "update_event", "delete_event"}
TOOL_REQUIRED = {
    "list_events": set(),
    "get_event": {"id"},
    "create_event": {"title"},
    "update_event": {"id"},
    "delete_event": {"id"},
}

# Fixed fixtures: the temporary database is new on every run, so nothing can
# collide, and a fixed date keeps the test deterministic.
START = "2030-01-15T10:00:00Z"
END = "2030-01-15T10:30:00Z"
DAY_FROM = "2030-01-15T00:00:00Z"
DAY_TO = "2030-01-16T00:00:00Z"


class Failure(RuntimeError):
    """An assertion or protocol violation that should stop the run."""


class McpServer:
    """One `klend_ario_mcp_server` process and its line-oriented client."""

    def __init__(self, harness: "Harness", label: str, permissions: str | None = None) -> None:
        self.harness = harness
        self.label = label
        self.permissions = permissions
        self.proc: subprocess.Popen[bytes] | None = None
        self.stderr_path = harness.tmp / f"{label}.stderr.log"
        self.stderr = open(self.stderr_path, "wb")
        self.request_id = 0
        self.last_request: dict[str, object] | None = None
        self.last_response: dict[str, object] | None = None

    def start(self) -> "McpServer":
        argv = [str(self.harness.server), "--db", str(self.harness.db)]
        if self.permissions is not None:
            argv += ["--permissions", self.permissions]
        harness = self.harness
        harness.note(f"{self.label}: {' '.join(argv)}")
        try:
            self.proc = subprocess.Popen(
                argv,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=self.stderr,
                bufsize=0,
                env=harness.child_env,
            )
        except OSError as exc:
            self.stderr.close()
            raise Failure(f"{self.label}: cannot start {harness.server}: {exc}") from exc
        harness.servers.append(self)
        return self

    # -- transport -----------------------------------------------------

    def _write(self, message: dict[str, object]) -> None:
        if self.proc is None or self.proc.stdin is None:
            raise Failure(f"{self.label}: server is not running")
        line = json.dumps(message, separators=(",", ":"), ensure_ascii=False)
        self.harness.note(f"{self.label} -> {line}")
        try:
            self.proc.stdin.write((line + "\n").encode("utf-8"))
            self.proc.stdin.flush()
        except OSError as exc:
            raise Failure(f"{self.label}: cannot write to the server: {exc}") from exc

    def _read_line(self) -> dict[str, object]:
        assert self.proc is not None and self.proc.stdout is not None
        ready, _, _ = select.select([self.proc.stdout], [], [], self.harness.timeout)
        if not ready:
            raise Failure(
                f"{self.label}: no response within {self.harness.timeout}s "
                f"(stderr: {self.diagnostics()})"
            )
        raw = self.proc.stdout.readline()
        if not raw:
            raise Failure(
                f"{self.label}: server closed the connection "
                f"(stderr: {self.diagnostics()})"
            )
        text = raw.decode("utf-8").rstrip("\r\n")
        self.harness.note(f"{self.label} <- {text}")
        try:
            response = json.loads(text)
        except json.JSONDecodeError as exc:
            raise Failure(f"{self.label}: stdout is not JSON-RPC: {text!r}") from exc
        if not isinstance(response, dict):
            raise Failure(f"{self.label}: response is not an object: {text!r}")
        return response

    def notify(self, method: str, params: dict[str, object] | None = None) -> None:
        message: dict[str, object] = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            message["params"] = params
        self._write(message)

    def rpc(self, method: str, params: dict[str, object] | None = None) -> dict[str, object]:
        self.request_id += 1
        message: dict[str, object] = {
            "jsonrpc": "2.0",
            "id": self.request_id,
            "method": method,
        }
        if params is not None:
            message["params"] = params
        self.last_request = message
        self._write(message)
        response = self._read_line()
        self.last_response = response

        if response.get("jsonrpc") != "2.0":
            raise Failure(f"{self.label}: missing jsonrpc 2.0 in response: {response}")
        if response.get("id") != self.request_id:
            raise Failure(
                f"{self.label}: response id {response.get('id')!r} "
                f"does not match request {self.request_id}"
            )
        if "result" not in response and "error" not in response:
            raise Failure(f"{self.label}: response has neither result nor error: {response}")
        return response

    # -- tools ---------------------------------------------------------

    def call_result(self, tool: str, arguments: dict[str, object] | None = None) -> dict[str, object]:
        """Return the tools/call result object, or fail on a protocol error."""
        response = self.rpc("tools/call", {"name": tool, "arguments": arguments or {}})
        if "error" in response:
            raise Failure(f"{self.label}: tools/call {tool} was a protocol error: {response}")
        result = response.get("result")
        if not isinstance(result, dict):
            raise Failure(f"{self.label}: tools/call {tool} returned no result object")
        return result

    def call(self, tool: str, arguments: dict[str, object] | None = None) -> tuple[bool, str]:
        """Return (is_error, text) for one tools/call."""
        result = self.call_result(tool, arguments)
        return bool(result.get("isError")), text_of_result(result)

    # -- lifecycle -----------------------------------------------------

    def diagnostics(self) -> str:
        parts: list[str] = []
        if self.last_request is not None:
            parts.append(f"last request: {json.dumps(self.last_request)}")
        if self.last_response is not None:
            parts.append(f"last response: {json.dumps(self.last_response)}")
        try:
            stderr = self.stderr_path.read_text(encoding="utf-8", errors="replace").strip()
        except OSError:
            stderr = ""
        if stderr:
            parts.append(f"stderr: {stderr}")
        return "; ".join(parts) if parts else "no diagnostics"

    def close(self) -> bytes:
        """Close stdin, wait for exit, and return whatever stdout remains."""
        if self.proc is None:
            return b""
        proc = self.proc
        self.proc = None
        if proc.stdin is not None:
            try:
                proc.stdin.close()
            except OSError:
                pass
        try:
            returncode = proc.wait(timeout=self.harness.timeout)
        except subprocess.TimeoutExpired:
            proc.kill()
            returncode = proc.wait()
        rest = proc.stdout.read() if proc.stdout is not None else b""
        self.stderr.close()
        self.harness.servers.remove(self)
        self.harness.note(f"{self.label}: exited {returncode}")
        if returncode != 0:
            raise Failure(f"{self.label}: server exited {returncode} ({self.diagnostics()})")
        return rest


class Harness:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.root = Path(__file__).resolve().parent.parent
        self.timeout = args.timeout
        self.verbose = args.verbose
        self.server = self._find_server()
        self.cli = self._find_cli()

        self.tmp = Path(tempfile.mkdtemp(prefix=ROOT_PREFIX))
        self.db = self.tmp / "mcp.db"
        self.servers: list[McpServer] = []
        self.checks = 0
        self.cleaned = False
        self.child_env = os.environ.copy()
        # An exported key would make the server open this plaintext test
        # database as encrypted, which is not what the smoke test exercises.
        self.child_env.pop("KLEND_ARIO_KEY", None)

        atexit.register(self.cleanup)
        signal.signal(signal.SIGINT, self._on_signal)
        signal.signal(signal.SIGTERM, self._on_signal)

    # -- discovery / isolation -----------------------------------------

    def _build_dirs(self) -> list[Path]:
        names = ["build_release", "build"] if self.args.release else ["build", "build_release"]
        return [self.root / name for name in names]

    def _find_server(self) -> Path:
        if self.args.server:
            path = Path(self.args.server).expanduser()
            if not path.is_file() or not os.access(path, os.X_OK):
                raise Failure(f"--server is not an executable file: {path}")
            return path.resolve()

        candidates: list[Path] = []
        for base in self._build_dirs():
            candidates.append(base / "mcp" / "klend_ario_mcp_server")
        found = shutil.which("klend_ario_mcp_server")
        if found:
            candidates.append(Path(found))

        for path in candidates:
            if path.is_file() and os.access(path, os.X_OK):
                return path.resolve()
        raise Failure(
            "cannot find klend_ario_mcp_server; build it first:\n"
            "  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug\n"
            "  ninja -C build"
        )

    def _find_cli(self) -> Path | None:
        if self.args.cli:
            path = Path(self.args.cli).expanduser()
            if not path.is_file() or not os.access(path, os.X_OK):
                raise Failure(f"--cli is not an executable file: {path}")
            return path.resolve()

        candidates = [self.server.parent.parent / "cli" / "klend_ario_cli"]
        for base in self._build_dirs():
            candidates.append(base / "cli" / "klend_ario_cli")
        found = shutil.which("klend_ario_cli")
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
        for server in list(self.servers):
            if server.proc is not None:
                server.proc.kill()
                try:
                    server.proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    pass
            server.stderr.close()
        self.servers.clear()
        if self.args.keep:
            return
        shutil.rmtree(self.tmp, ignore_errors=True)

    def _on_signal(self, signum: int, _frame: object) -> None:
        raise SystemExit(128 + signum)

    # -- reporting -----------------------------------------------------

    def note(self, text: str) -> None:
        if self.verbose:
            print(f"      {text}")

    def ok(self, message: str) -> None:
        self.checks += 1
        print(f"ok    {message}")

    def skip(self, message: str) -> None:
        print(f"skip  {message}")


def expect(condition: object, message: str) -> None:
    if not condition:
        raise Failure(message)


def expect_object(value: object, message: str) -> dict[str, object]:
    expect(isinstance(value, dict), f"{message}: expected an object, got {value!r}")
    return value  # type: ignore[return-value]


def expect_list(value: object, message: str) -> list[object]:
    expect(isinstance(value, list), f"{message}: expected an array, got {value!r}")
    return value  # type: ignore[return-value]


def text_of_result(result: dict[str, object]) -> str:
    content = result.get("content")
    if isinstance(content, list) and content and isinstance(content[0], dict):
        text = content[0].get("text")
        if isinstance(text, str):
            return text
    return ""


def structured_of_result(result: dict[str, object], label: str) -> object:
    structured = result.get("structuredContent")
    expect(structured is not None, f"{label}: tools/call has no structuredContent")
    return structured


def parse_tool_json(text: str, label: str) -> object:
    try:
        return json.loads(text)
    except json.JSONDecodeError as exc:
        raise Failure(f"{label}: tool output is not JSON: {text!r}") from exc


def run_cli(harness: Harness, argv: list[str]) -> subprocess.CompletedProcess[str]:
    assert harness.cli is not None
    command = [str(harness.cli), *argv]
    harness.note(f"cli: {' '.join(command)}")
    return subprocess.run(
        command,
        capture_output=True,
        text=True,
        timeout=harness.timeout,
        env=harness.child_env,
    )


# ---------------------------------------------------------------------------
# checks
# ---------------------------------------------------------------------------


def check_handshake(server: McpServer, harness: Harness, prefix: str = "") -> None:
    response = server.rpc(
        "initialize",
        {
            "protocolVersion": "2025-06-18",
            "capabilities": {},
            "clientInfo": {"name": "mcp_smoke", "version": "1.0"},
        },
    )
    expect("error" not in response, f"initialize failed: {response}")
    result = expect_object(response.get("result"), "initialize result")
    info = expect_object(result.get("serverInfo"), "initialize serverInfo")
    expect(info.get("name") == "klend_ario", f"unexpected server name: {info.get('name')!r}")
    expect(isinstance(info.get("version"), str), "serverInfo.version is missing")
    protocol = result.get("protocolVersion")
    expect(isinstance(protocol, str) and protocol, "protocolVersion is missing")
    instructions = result.get("instructions")
    expect(isinstance(instructions, str) and instructions.strip(), "instructions are missing")
    capabilities = expect_object(result.get("capabilities"), "initialize capabilities")
    tools_capability = expect_object(capabilities.get("tools"), "tools capability")
    expect(tools_capability.get("listChanged") is False, "tools.listChanged should be false")
    harness.ok(
        f"{prefix}initialize handshake "
        f"({info.get('name')} {info.get('version')}, protocol {protocol})"
    )
    harness.ok(f"{prefix}initialize: {len(instructions.splitlines())} lines of instructions")

    server.notify("notifications/initialized")

    pong = server.rpc("ping")
    expect("error" not in pong, f"ping failed: {pong}")
    expect(isinstance(pong.get("result"), dict), f"ping returned no object: {pong}")
    harness.ok(f"{prefix}ping")


def check_tools(server: McpServer, harness: Harness) -> None:
    response = server.rpc("tools/list")
    expect("error" not in response, f"tools/list failed: {response}")
    result = expect_object(response.get("result"), "tools/list result")
    tools = expect_list(result.get("tools"), "tools/list tools")

    by_name: dict[str, dict[str, object]] = {}
    for tool in tools:
        entry = expect_object(tool, "tools/list entry")
        name = entry.get("name")
        expect(isinstance(name, str), f"tool without a name: {entry}")
        by_name[str(name)] = entry

    expect(set(by_name) == TOOL_NAMES, f"unexpected tool set: {sorted(by_name)}")
    harness.ok(f"tools/list: {len(by_name)} tools")

    for name, required in TOOL_REQUIRED.items():
        schema = expect_object(by_name[name].get("inputSchema"), f"{name} inputSchema")
        expect(schema.get("type") == "object", f"{name} inputSchema.type is not object")
        expect(
            schema.get("additionalProperties") is False,
            f"{name} inputSchema should reject unknown properties",
        )
        properties = expect_object(schema.get("properties"), f"{name} properties")
        reported = set(schema.get("required", []))
        expect(reported == required, f"{name} required fields are {sorted(reported)}, want {sorted(required)}")
        for field in required:
            expect(field in properties, f"{name} does not declare required property {field!r}")
    harness.ok("tools/list: schemas and required fields")

    for name, entry in by_name.items():
        expect(isinstance(entry.get("title"), str) and entry["title"], f"{name} has no title")
        annotations = expect_object(entry.get("annotations"), f"{name} annotations")
        expect(annotations.get("openWorldHint") is False, f"{name} should be closed-world")
        expect(isinstance(annotations.get("readOnlyHint"), bool), f"{name} readOnlyHint missing")
        expect(isinstance(annotations.get("destructiveHint"), bool), f"{name} destructiveHint missing")
        expect(isinstance(annotations.get("idempotentHint"), bool), f"{name} idempotentHint missing")

    expect(by_name["list_events"]["annotations"]["readOnlyHint"] is True, "list_events should be read-only")
    expect(by_name["delete_event"]["annotations"]["destructiveHint"] is True, "delete_event should be destructive")
    expect(
        by_name["create_event"]["annotations"]["idempotentHint"] is False,
        "create_event should not be idempotent",
    )
    visibility = expect_object(
        by_name["list_events"]["inputSchema"]["properties"].get("visibility"),
        "list_events visibility",
    )
    expect(set(visibility.get("enum", [])) == {"any", "visible", "hidden"}, "wrong visibility enum")
    harness.ok("tools/list: titles, annotations and enums")


def check_crud(server: McpServer, harness: Harness) -> int:
    result = server.call_result(
        "create_event",
        {
            "title": "MCP smoke",
            "start": START,
            "end": END,
            "importance": 55,
            "urgency": 45,
            "alarm_offset": 600,
            "notes": "created over JSON-RPC",
        },
    )
    is_error, text = bool(result.get("isError")), text_of_result(result)
    expect(not is_error, f"create_event failed: {text}")
    created = expect_object(parse_tool_json(text, "create_event"), "create_event result")
    event_id = created.get("id")
    expect(isinstance(event_id, int) and event_id > 0, f"create_event returned no id: {text}")
    structured = structured_of_result(result, "create_event")
    expect(structured == created, "create_event structuredContent does not match text")
    harness.ok(f"create_event -> id {event_id}")

    result = server.call_result("get_event", {"id": event_id})
    is_error, text = bool(result.get("isError")), text_of_result(result)
    expect(not is_error, f"get_event failed: {text}")
    event = expect_object(parse_tool_json(text, "get_event"), "get_event result")
    expect(event.get("id") == event_id, f"get_event returned id {event.get('id')!r}")
    expect(event.get("title") == "MCP smoke", f"wrong title: {event.get('title')!r}")
    expect(event.get("start") == START, f"wrong start: {event.get('start')!r}")
    expect(event.get("end") == END, f"wrong end: {event.get('end')!r}")
    expect(event.get("importance") == 55, f"wrong importance: {event.get('importance')!r}")
    expect(event.get("urgency") == 45, f"wrong urgency: {event.get('urgency')!r}")
    expect(event.get("alarm_offset") == 600, f"wrong alarm_offset: {event.get('alarm_offset')!r}")
    expect(event.get("notes") == "created over JSON-RPC", f"wrong notes: {event.get('notes')!r}")
    expect(structured_of_result(result, "get_event") == event, "get_event structuredContent mismatch")
    harness.ok("get_event: every field round-trips, text and structuredContent agree")

    result = server.call_result("list_events", {"from": DAY_FROM, "to": DAY_TO})
    is_error, text = bool(result.get("isError")), text_of_result(result)
    expect(not is_error, f"list_events failed: {text}")
    rows = expect_list(parse_tool_json(text, "list_events"), "list_events result")
    expect(len(rows) == 1, f"list_events returned {len(rows)} rows, want 1")
    list_structured = expect_object(
        structured_of_result(result, "list_events"), "list_events structuredContent"
    )
    expect(list_structured.get("events") == rows, "list structuredContent mismatch")
    harness.ok("list_events: the range sees exactly one row")

    result = server.call_result("update_event", {"id": event_id, "title": "MCP smoke (updated)"})
    is_error, text = bool(result.get("isError")), text_of_result(result)
    expect(not is_error, f"update_event failed: {text}")
    updated = expect_object(parse_tool_json(text, "update_event"), "update_event result")
    expect(updated.get("title") == "MCP smoke (updated)", f"update did not change the title: {text}")
    expect(updated.get("start") == START, f"partial update moved the start: {updated.get('start')!r}")
    expect(updated.get("end") == END, f"partial update moved the end: {updated.get('end')!r}")
    harness.ok("update_event: partial update keeps the fields it was not given")

    is_error, text = server.call("get_event", {"id": event_id})
    expect(not is_error, f"get_event after update failed: {text}")
    reread = expect_object(parse_tool_json(text, "get_event"), "get_event result")
    expect(reread.get("title") == "MCP smoke (updated)", f"stored title is stale: {reread.get('title')!r}")
    harness.ok("get_event: the update is persisted")

    return event_id


def check_duration_and_relative_time(server: McpServer, harness: Harness) -> None:
    result = server.call_result(
        "create_event",
        {
            "title": "Duration smoke",
            "start": "2030-01-15",
            "duration": "45m",
        },
    )
    is_error, text = bool(result.get("isError")), text_of_result(result)
    expect(not is_error, f"duration create_event failed: {text}")
    created = expect_object(parse_tool_json(text, "duration create_event"), "create result")
    event_id = created.get("id")
    expect(isinstance(event_id, int) and event_id > 0, "duration create returned no id")

    is_error, text = server.call("get_event", {"id": event_id})
    expect(not is_error, f"duration get_event failed: {text}")
    event = expect_object(parse_tool_json(text, "duration get_event"), "get result")
    expect(event.get("start") == "2030-01-15T00:00:00Z", f"date-only start resolved wrong: {event.get('start')!r}")
    expect(event.get("end") == "2030-01-15T00:45:00Z", f"duration resolved wrong: {event.get('end')!r}")
    harness.ok("create_event: date-only start and a duration")

    is_error, text = server.call("delete_event", {"id": event_id})
    expect(not is_error, f"duration delete_event failed: {text}")


def check_cli_crosscheck(harness: Harness, event_id: int) -> None:
    if harness.cli is None:
        harness.skip("CLI cross-check: klend_ario_cli was not found")
        return

    shown = run_cli(harness, ["show", str(event_id), "--db", str(harness.db), "--format", "json"])
    expect(shown.returncode == 0, f"CLI show exited {shown.returncode}: {shown.stderr.strip()}")
    try:
        row = json.loads(shown.stdout)
    except json.JSONDecodeError as exc:
        raise Failure(f"CLI show did not print JSON: {shown.stdout!r}") from exc
    expect(isinstance(row, dict), f"CLI show printed {type(row).__name__}, want an object")
    expect(row.get("title") == "MCP smoke (updated)", f"CLI sees the wrong title: {row.get('title')!r}")
    harness.ok("CLI cross-check: the MCP write is in the shared SQLite file")


def check_delete(server: McpServer, harness: Harness, event_id: int) -> None:
    is_error, text = server.call("delete_event", {"id": event_id})
    expect(not is_error, f"delete_event failed: {text}")
    deleted = expect_object(parse_tool_json(text, "delete_event"), "delete_event result")
    expect(deleted.get("deleted") == event_id, f"delete_event did not report the id: {text}")
    harness.ok(f"delete_event -> id {event_id}")

    is_error, text = server.call("get_event", {"id": event_id})
    expect(is_error, f"get_event after delete did not report an error: {text}")
    harness.ok("get_event: a deleted row is an error")

    is_error, text = server.call("list_events", {"from": DAY_FROM, "to": DAY_TO})
    expect(not is_error, f"list_events after delete failed: {text}")
    rows = expect_list(parse_tool_json(text, "list_events"), "list_events result")
    expect(rows == [], f"list_events after delete returned {len(rows)} rows: {rows}")
    harness.ok("list_events: the range is empty after delete")

    if harness.cli is not None:
        shown = run_cli(harness, ["show", str(event_id), "--db", str(harness.db), "--format", "json"])
        expect(shown.returncode == 1, f"CLI show after delete exited {shown.returncode}, want 1")
        harness.ok("CLI cross-check: the MCP delete removed the row from SQLite")


def check_errors(server: McpServer, harness: Harness) -> None:
    response = server.rpc("tools/call", {"name": "frobnicate", "arguments": {}})
    error = expect_object(response.get("error"), "unknown-tool protocol error")
    expect(error.get("code") == -32602, f"unknown tool returned code {error.get('code')!r}")
    expect("unknown tool" in str(error.get("message", "")), f"unexpected message: {error}")
    harness.ok("tools/call: an unknown tool is a protocol error")

    is_error, text = server.call("create_event", {"title": "", "start": START, "end": END})
    expect(is_error, f"an empty title was accepted: {text}")
    harness.ok("create_event: an empty title is rejected")

    is_error, text = server.call(
        "create_event",
        {"title": "Loose bool", "start": START, "duration": "30m", "hidden": "no"},
    )
    expect(is_error, f"a string hidden flag was accepted: {text}")
    expect("hidden must be true or false" in text, f"unexpected hidden message: {text!r}")
    harness.ok("create_event: hidden must be a real boolean")

    is_error, text = server.call(
        "create_event",
        {"title": "Typo", "start": START, "duration": "30m", "starts": START},
    )
    expect(is_error, f"an unknown argument was accepted: {text}")
    expect("unknown argument" in text, f"unexpected unknown-argument message: {text!r}")
    harness.ok("create_event: unknown arguments are rejected")

    is_error, text = server.call(
        "create_event",
        {"title": "Backwards", "start": END, "end": START},
    )
    expect(is_error, f"end before start was accepted: {text}")
    expect("end must be at or after start" in text, f"unexpected range message: {text!r}")
    harness.ok("create_event: end before start is rejected")


def check_read_only(harness: Harness) -> None:
    readonly = McpServer(harness, "readonly", permissions="read").start()
    try:
        check_handshake(readonly, harness, prefix="read-only: ")

        response = readonly.rpc("tools/list")
        result = expect_object(response.get("result"), "read-only tools/list result")
        tools = expect_list(result.get("tools"), "read-only tools")
        names = {tool.get("name") for tool in tools if isinstance(tool, dict)}
        expect(names == {"list_events", "get_event"}, f"read-only server lists {sorted(names)}")
        harness.ok("read-only: tools/list advertises only the read tier")

        is_error, text = readonly.call(
            "create_event", {"title": "denied", "start": START, "end": END}
        )
        expect(is_error, f"read-only create_event was not denied: {text}")
        expect("permission denied" in text, f"unexpected denial message: {text!r}")
        harness.ok("read-only: create_event is denied")

        is_error, text = readonly.call("list_events", {"from": DAY_FROM, "to": DAY_TO})
        expect(not is_error, f"read-only list_events failed: {text}")
        expect(parse_tool_json(text, "read-only list_events") == [], "read-only list is not empty")
        harness.ok("read-only: list_events still works")
    finally:
        rest = readonly.close()
        expect_no_trailing_output(rest, "read-only server")
        harness.ok("stdout: no trailing output after the read-only session")

    missing = harness.tmp / "missing.db"
    completed = subprocess.run(
        [str(harness.server), "--db", str(missing), "--permissions", "read"],
        input="",
        capture_output=True,
        text=True,
        timeout=harness.timeout,
    )
    expect(completed.returncode == 1, f"read-only missing DB exited {completed.returncode}")
    expect(not missing.exists(), "a read-only server created the missing database")
    expect("read-only server" in completed.stderr, f"missing DB hint absent: {completed.stderr!r}")
    harness.ok("read-only: a missing database is reported, not created")


def check_server_options(harness: Harness) -> None:
    completed = subprocess.run(
        [str(harness.server), "--permissions", "read,bogus"],
        input="",
        capture_output=True,
        text=True,
        timeout=harness.timeout,
    )
    expect(completed.returncode == 2, f"unknown permission exited {completed.returncode}, want 2")
    expect("unknown permission" in completed.stderr, f"unexpected stderr: {completed.stderr!r}")
    harness.ok("--permissions: an unknown token is a usage error")

    completed = subprocess.run(
        [str(harness.server), "--db"],
        input="",
        capture_output=True,
        text=True,
        timeout=harness.timeout,
    )
    expect(completed.returncode == 2, f"--db without a value exited {completed.returncode}")
    expect("needs a value" in completed.stderr, f"unexpected stderr: {completed.stderr!r}")
    harness.ok("--db: a missing value is a usage error")

    completed = subprocess.run(
        [str(harness.server), "--version"],
        input="",
        capture_output=True,
        text=True,
        timeout=harness.timeout,
    )
    expect(completed.returncode == 0, f"--version exited {completed.returncode}")
    expect("klend_ario_mcp_server" in completed.stdout, f"unexpected version: {completed.stdout!r}")
    harness.ok("--version")


def expect_no_trailing_output(rest: bytes, label: str) -> None:
    text = rest.decode("utf-8", errors="replace")
    for line in text.splitlines():
        if not line.strip():
            continue
        try:
            json.loads(line)
        except json.JSONDecodeError as exc:
            raise Failure(f"{label}: wrote non-JSON to stdout: {line!r}") from exc


# ---------------------------------------------------------------------------
# entry point
# ---------------------------------------------------------------------------


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Focused end-to-end smoke test for the Klend-ario MCP server.",
        epilog="The temporary database and server logs are removed on exit; --keep preserves them.",
    )
    parser.add_argument("--server", metavar="PATH", help="use a specific klend_ario_mcp_server binary")
    parser.add_argument("--cli", metavar="PATH", help="use a specific klend_ario_cli binary for the cross-check")
    parser.add_argument("--release", action="store_true", help="prefer build_release/ over build/")
    parser.add_argument("--keep", action="store_true", help="keep the temporary directory and print its path")
    parser.add_argument(
        "--timeout",
        type=float,
        default=5.0,
        metavar="SECONDS",
        help="per-response and per-process timeout (default: 5)",
    )
    parser.add_argument("-v", "--verbose", action="store_true", help="print every JSON-RPC frame")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.timeout <= 0:
        print("error: --timeout must be positive", file=sys.stderr)
        return EXIT_USAGE

    harness: Harness | None = None
    try:
        harness = Harness(args)
    except Failure as exc:
        print(f"error: {exc}", file=sys.stderr)
        return EXIT_USAGE

    assert harness is not None
    primary: McpServer | None = None
    try:
        print(f"  server: {harness.server}", flush=True)
        print(f"  database: {harness.db}", flush=True)
        if harness.cli is not None:
            print(f"  cli: {harness.cli}", flush=True)
        else:
            print("  cli: not found (cross-check will be skipped)", flush=True)

        primary = McpServer(harness, "primary").start()
        check_handshake(primary, harness)
        check_tools(primary, harness)
        event_id = check_crud(primary, harness)
        check_duration_and_relative_time(primary, harness)
        check_cli_crosscheck(harness, event_id)
        check_delete(primary, harness, event_id)
        check_errors(primary, harness)

        rest = primary.close()
        primary = None
        expect_no_trailing_output(rest, "primary server")
        harness.ok("stdout: protocol frames only, no trailing output")

        check_read_only(harness)
        check_server_options(harness)
    except Failure as exc:
        print(f"FAIL  {exc}", file=sys.stderr)
        return EXIT_ERROR
    except subprocess.TimeoutExpired as exc:
        print(f"FAIL  command timed out: {exc}", file=sys.stderr)
        return EXIT_ERROR
    finally:
        if primary is not None:
            try:
                primary.close()
            except Failure:
                pass
        harness.cleanup()

    if harness.args.keep:
        print(f"kept: {harness.tmp}")
    print(f"mcp_smoke: {harness.checks} checks passed")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
