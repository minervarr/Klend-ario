#!/usr/bin/env python3
"""Focused end-to-end smoke test for the Klend-ario MCP server.

The MCP server speaks newline-delimited JSON-RPC on stdin/stdout. This script
drives the real binary exactly as an MCP host would, against a throwaway
database, and then removes every trace:

    ninja -C build
    python3 demo/mcp_smoke.py

It asserts the handshake and instructions, tool schemas with titles/annotations,
all five tools, structured content, strict input errors, relative time and
durations, query visibility/search/sort/limit, update semantics, the default
"now" and default range, protocol resilience, encryption through `--key-file`
and `KLEND_ARIO_KEY`, the read-only permission tier (including a write-protected
database and the tier-filtered `tools/list`), the read-only startup contract,
stdout purity, and (when the CLI is built) that MCP writes are visible through
the CLI on the same database.

Unlike demo/klend_ario_demo.py this is quiet and focused: one line per passing
check, a non-zero exit on the first failure, and no narration. The contract
lives in demo/README.md. Standard library only; no third-party packages.
"""

from __future__ import annotations

import argparse
import atexit
import json
import math
import os
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

EXIT_OK = 0
EXIT_ERROR = 1
EXIT_USAGE = 2
ROOT_PREFIX = "klend_ario_mcp_smoke-"

# The revision the server implements. The smoke asserts the exact value so a
# accidental downgrade/upgrade is visible to the host-shaped test too.
PROTOCOL_VERSION = "2025-06-18"
PASSPHRASE = "mcp-smoke-passphrase"

# A response frame bigger than this is a bug (or a hostile server), not a
# calendar. The real tools/list is a few KiB.
MAX_FRAME_BYTES = 1 << 20

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

    def __init__(
        self,
        harness: "Harness",
        label: str,
        permissions: str | None = None,
        db: Path | None = None,
        key_file: Path | None = None,
        env: dict[str, str] | None = None,
    ) -> None:
        self.harness = harness
        self.label = label
        self.permissions = permissions
        self.db = db if db is not None else harness.db
        self.key_file = key_file
        self.env = dict(env or {})
        self.proc: subprocess.Popen[bytes] | None = None
        self.stderr_path = harness.tmp / f"{label}.stderr.log"
        self.stderr = open(self.stderr_path, "wb")
        self.request_id = 0
        self.last_request: dict[str, object] | None = None
        self.last_response: dict[str, object] | None = None
        self._buffer = b""

    def start(self) -> "McpServer":
        argv = [str(self.harness.server), "--db", str(self.db)]
        if self.key_file is not None:
            argv += ["--key-file", str(self.key_file)]
        if self.permissions is not None:
            argv += ["--permissions", self.permissions]
        harness = self.harness
        harness.note(f"{self.label}: {' '.join(argv)}")
        env = dict(harness.child_env)
        env.update(self.env)
        try:
            self.proc = subprocess.Popen(
                argv,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=self.stderr,
                bufsize=0,
                env=env,
            )
        except OSError as exc:
            self.stderr.close()
            raise Failure(f"{self.label}: cannot start {harness.server}: {exc}") from exc
        harness.servers.append(self)
        return self

    # -- transport -----------------------------------------------------

    def _write_line(self, line: str) -> None:
        if self.proc is None or self.proc.stdin is None:
            raise Failure(f"{self.label}: server is not running")
        self.harness.note(f"{self.label} -> {line}")
        try:
            self.proc.stdin.write((line + "\n").encode("utf-8"))
            self.proc.stdin.flush()
        except OSError as exc:
            raise Failure(f"{self.label}: cannot write to the server: {exc}") from exc

    def _write(self, message: dict[str, object]) -> None:
        self._write_line(json.dumps(message, separators=(",", ":"), ensure_ascii=False))

    def write_line(self, line: str) -> None:
        """Send one raw line without waiting for a response (notifications)."""
        self._write_line(line)

    def _read_line(self) -> dict[str, object]:
        assert self.proc is not None and self.proc.stdout is not None
        deadline = time.monotonic() + self.harness.timeout
        while True:
            newline = self._buffer.find(b"\n")
            if newline >= 0:
                raw = self._buffer[:newline]
                self._buffer = self._buffer[newline + 1 :]
                break
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise Failure(
                    f"{self.label}: no complete response within {self.harness.timeout}s "
                    f"(stderr: {self.diagnostics()})"
                )
            ready, _, _ = select.select([self.proc.stdout], [], [], remaining)
            if not ready:
                raise Failure(
                    f"{self.label}: no response within {self.harness.timeout}s "
                    f"(stderr: {self.diagnostics()})"
                )
            chunk = os.read(self.proc.stdout.fileno(), 65536)
            if not chunk:
                raise Failure(
                    f"{self.label}: server closed the connection "
                    f"(stderr: {self.diagnostics()})"
                )
            self._buffer += chunk
            if len(self._buffer) > MAX_FRAME_BYTES:
                raise Failure(
                    f"{self.label}: response frame exceeds {MAX_FRAME_BYTES} bytes"
                )

        text = raw.decode("utf-8").rstrip("\r")
        self.harness.note(f"{self.label} <- {text}")
        try:
            response = json.loads(text)
        except json.JSONDecodeError as exc:
            raise Failure(f"{self.label}: stdout is not JSON-RPC: {text!r}") from exc
        if not isinstance(response, dict):
            raise Failure(f"{self.label}: response is not an object: {text!r}")
        self.last_response = response
        return response

    def _validate_frame(self, response: dict[str, object]) -> None:
        if response.get("jsonrpc") != "2.0":
            raise Failure(f"{self.label}: missing jsonrpc 2.0 in response: {response}")
        if "result" not in response and "error" not in response:
            raise Failure(f"{self.label}: response has neither result nor error: {response}")

    def raw(self, line: str) -> dict[str, object]:
        """Send one raw frame and return the next response, with no id check."""
        self.last_request = {"raw": line}
        self._write_line(line)
        response = self._read_line()
        self._validate_frame(response)
        return response

    def notify(self, method: str, params: dict[str, object] | None = None) -> None:
        message: dict[str, object] = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            message["params"] = params
        self.last_request = message
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
        self._validate_frame(response)

        if response.get("id") != self.request_id:
            raise Failure(
                f"{self.label}: response id {response.get('id')!r} "
                f"does not match request {self.request_id}"
            )
        return response

    # -- tools ---------------------------------------------------------

    def call_result(
        self, tool: str, arguments: dict[str, object] | None = None
    ) -> dict[str, object]:
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
        rest = self._buffer + rest
        self._buffer = b""
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
        self.encrypted_db = self.tmp / "encrypted.db"
        self.key_file = self.tmp / "key.txt"
        self.wrong_key_file = self.tmp / "wrong-key.txt"
        self.missing_key_file = self.tmp / "missing-key.txt"
        self.option_db = self.tmp / "option.db"
        self.missing_db = self.tmp / "missing.db"
        self.servers: list[McpServer] = []
        self.checks = 0
        self.cleaned = False
        self.child_env = os.environ.copy()
        # An exported key would make the server open this plaintext test
        # database as encrypted, which is not what most checks exercise.
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


def json_of_result(result: dict[str, object], label: str) -> object:
    return parse_tool_json(text_of_result(result), label)


def expect_tool_ok(
    server: McpServer,
    tool: str,
    arguments: dict[str, object],
    label: str,
) -> dict[str, object]:
    result = server.call_result(tool, arguments)
    text = text_of_result(result)
    expect(not bool(result.get("isError")), f"{label} failed: {text}")
    return result


def expect_tool_error(
    server: McpServer,
    tool: str,
    arguments: dict[str, object],
    needle: str,
    label: str,
) -> str:
    result = server.call_result(tool, arguments)
    expect(bool(result.get("isError")), f"{label} was accepted")
    text = text_of_result(result)
    expect(needle in text, f"{label}: unexpected error: {text!r}")
    return text


def list_rows(
    server: McpServer, arguments: dict[str, object], label: str
) -> list[object]:
    result = expect_tool_ok(server, "list_events", arguments, label)
    return expect_list(json_of_result(result, label), f"{label} rows")


def close_server(server: McpServer, harness: Harness, label: str) -> None:
    rest = server.close()
    expect_no_trailing_output(rest, label)
    harness.ok(f"{label}: stdout carried only protocol frames")


def expect_no_trailing_output(rest: bytes, label: str) -> None:
    text = rest.decode("utf-8", errors="replace").strip()
    if text:
        raise Failure(f"{label}: wrote unexpected output after stdin close: {text!r}")


def run_server(
    harness: Harness, argv: list[str], label: str
) -> subprocess.CompletedProcess[str]:
    command = [str(harness.server), *argv]
    harness.note(f"{label}: {' '.join(command)}")
    try:
        return subprocess.run(
            command,
            input="",
            capture_output=True,
            text=True,
            timeout=harness.timeout,
            env=harness.child_env,
        )
    except OSError as exc:
        raise Failure(f"{label}: cannot start {harness.server}: {exc}") from exc


def run_cli(harness: Harness, argv: list[str]) -> subprocess.CompletedProcess[str]:
    assert harness.cli is not None
    command = [str(harness.cli), *argv]
    harness.note(f"cli: {' '.join(command)}")
    try:
        return subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=harness.timeout,
            env=harness.child_env,
        )
    except OSError as exc:
        raise Failure(f"cli: cannot start {harness.cli}: {exc}") from exc


# ---------------------------------------------------------------------------
# checks
# ---------------------------------------------------------------------------


def check_handshake(server: McpServer, harness: Harness, prefix: str = "") -> None:
    response = server.rpc(
        "initialize",
        {
            "protocolVersion": PROTOCOL_VERSION,
            "capabilities": {},
            "clientInfo": {"name": "mcp_smoke", "version": "1.0"},
        },
    )
    expect("error" not in response, f"initialize failed: {response}")
    result = expect_object(response.get("result"), "initialize result")
    info = expect_object(result.get("serverInfo"), "initialize serverInfo")
    expect(info.get("name") == "klend_ario", f"unexpected server name: {info.get('name')!r}")
    expect(
        isinstance(info.get("version"), str) and info["version"],
        "serverInfo.version is missing",
    )
    protocol = result.get("protocolVersion")
    expect(protocol == PROTOCOL_VERSION, f"protocolVersion is {protocol!r}, want {PROTOCOL_VERSION!r}")
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

    again = server.rpc("tools/list")
    again_result = expect_object(again.get("result"), "second tools/list result")
    again_tools = expect_list(again_result.get("tools"), "second tools/list tools")
    expect(again_tools == tools, "tools/list order or content changed between calls")

    by_name: dict[str, dict[str, object]] = {}
    for tool in tools:
        entry = expect_object(tool, "tools/list entry")
        name = entry.get("name")
        expect(isinstance(name, str), f"tool without a name: {entry}")
        by_name[str(name)] = entry

    expect(len(tools) == len(TOOL_NAMES), f"tools/list returned {len(tools)} tools, want {len(TOOL_NAMES)}")
    expect(len(by_name) == len(tools), "tools/list contains duplicate names")
    expect(set(by_name) == TOOL_NAMES, f"unexpected tool set: {sorted(by_name)}")
    harness.ok(f"tools/list: {len(by_name)} unique tools, deterministic order")

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
        expect(annotations.get("readOnlyHint") is (name in {"list_events", "get_event"}),
               f"{name} has the wrong readOnlyHint")
        expect(annotations.get("destructiveHint") is (name in {"update_event", "delete_event"}),
               f"{name} has the wrong destructiveHint")
        expect(annotations.get("idempotentHint") is (name != "create_event"),
               f"{name} has the wrong idempotentHint")

    list_events = by_name["list_events"]
    visibility = expect_object(
        list_events["inputSchema"]["properties"].get("visibility"),
        "list_events visibility",
    )
    expect(set(visibility.get("enum", [])) == {"any", "visible", "hidden"}, "wrong visibility enum")
    sort = expect_object(list_events["inputSchema"]["properties"].get("sort"), "list_events sort")
    expect(
        set(sort.get("enum", [])) == {"start", "-start", "importance", "urgency", "updated", "title"},
        "wrong sort enum",
    )
    limit = expect_object(list_events["inputSchema"]["properties"].get("limit"), "list_events limit")
    expect(limit.get("maximum") == 500, f"list_events limit maximum is {limit.get('maximum')!r}")

    create = by_name["create_event"]
    create_properties = expect_object(create["inputSchema"]["properties"], "create_event properties")
    expect("start" in create_properties, "create_event does not declare start")
    expect("duration" in create_properties, "create_event does not declare duration")
    expect("start" not in set(create["inputSchema"].get("required", [])),
           "create_event should default start to now")
    harness.ok("tools/list: titles, annotations, enums and bounds")


def check_protocol(server: McpServer, harness: Harness) -> None:
    broken = server.raw("{not json")
    error = expect_object(broken.get("error"), "malformed JSON error")
    expect(error.get("code") == -32700, f"malformed JSON returned {error.get('code')!r}")
    pong = server.rpc("ping")
    expect(isinstance(pong.get("result"), dict), "server did not survive malformed JSON")
    harness.ok("protocol: malformed JSON is -32700 and the server survives")

    string_id = server.raw('{"jsonrpc":"2.0","id":"smoke-string","method":"ping"}')
    expect(string_id.get("id") == "smoke-string", f"string id not echoed: {string_id!r}")
    harness.ok("protocol: a string JSON-RPC id is echoed unchanged")

    unknown = server.rpc("bogus")
    error = expect_object(unknown.get("error"), "unknown method error")
    expect(error.get("code") == -32601, f"unknown method returned {error.get('code')!r}")
    harness.ok("protocol: an unknown method is -32601")

    batch = server.raw('[{"jsonrpc":"2.0","id":1,"method":"ping"}]')
    error = expect_object(batch.get("error"), "batch error")
    expect(error.get("code") == -32600, f"batch returned {error.get('code')!r}")
    harness.ok("protocol: JSON-RPC batching is rejected")

    # An empty line is skipped by the stdio loop; the next response must still
    # be the response to the ping, not a parse error.
    server.write_line("")
    pong = server.rpc("ping")
    expect(isinstance(pong.get("result"), dict), "an empty line disturbed the session")
    harness.ok("protocol: an empty line is ignored")

    whitespace = server.raw("   ")
    error = expect_object(whitespace.get("error"), "whitespace frame error")
    expect(error.get("code") == -32700, f"whitespace frame returned {error.get('code')!r}")
    harness.ok("protocol: a whitespace-only frame is -32700")


def check_crud(server: McpServer, harness: Harness) -> int:
    result = expect_tool_ok(
        server,
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
        "create_event",
    )
    created = expect_object(json_of_result(result, "create_event"), "create_event result")
    event_id = created.get("id")
    expect(isinstance(event_id, int) and event_id > 0, f"create_event returned no id: {created!r}")
    expect(structured_of_result(result, "create_event") == created,
           "create_event structuredContent does not match text")
    harness.ok(f"create_event -> id {event_id}")

    result = expect_tool_ok(server, "get_event", {"id": event_id}, "get_event")
    event = expect_object(json_of_result(result, "get_event"), "get_event result")
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

    result = expect_tool_ok(
        server, "list_events", {"from": DAY_FROM, "to": DAY_TO}, "list_events"
    )
    rows = expect_list(json_of_result(result, "list_events"), "list_events result")
    expect(len(rows) == 1, f"list_events returned {len(rows)} rows, want 1")
    list_structured = expect_object(
        structured_of_result(result, "list_events"), "list_events structuredContent"
    )
    expect(list_structured.get("events") == rows, "list structuredContent mismatch")
    harness.ok("list_events: the range sees exactly one row")

    result = expect_tool_ok(
        server,
        "update_event",
        {"id": event_id, "title": "MCP smoke (updated)"},
        "update_event",
    )
    updated = expect_object(json_of_result(result, "update_event"), "update_event result")
    expect(updated.get("title") == "MCP smoke (updated)", f"update did not change the title: {updated!r}")
    expect(updated.get("start") == START, f"partial update moved the start: {updated.get('start')!r}")
    expect(updated.get("end") == END, f"partial update moved the end: {updated.get('end')!r}")
    expect(structured_of_result(result, "update_event") == updated,
           "update_event structuredContent mismatch")
    harness.ok("update_event: partial update keeps the fields it was not given")

    result = expect_tool_ok(server, "get_event", {"id": event_id}, "get_event after update")
    reread = expect_object(json_of_result(result, "get_event after update"), "get_event result")
    expect(reread.get("title") == "MCP smoke (updated)", f"stored title is stale: {reread.get('title')!r}")
    harness.ok("get_event: the update is persisted")

    return event_id


def check_update_semantics(server: McpServer, harness: Harness) -> None:
    result = expect_tool_ok(
        server,
        "create_event",
        {"title": "Update smoke", "start": START, "end": END},
        "update semantics create",
    )
    created = expect_object(json_of_result(result, "update semantics create"), "create result")
    event_id = created.get("id")
    expect(isinstance(event_id, int) and event_id > 0, "update semantics create returned no id")

    try:
        result = expect_tool_ok(
            server,
            "update_event",
            {"id": event_id, "start": "2030-01-15T11:00:00Z"},
            "update start-only",
        )
        moved = expect_object(json_of_result(result, "update start-only"), "update result")
        expect(moved.get("start") == "2030-01-15T11:00:00Z", f"wrong moved start: {moved.get('start')!r}")
        expect(moved.get("end") == "2030-01-15T11:30:00Z",
               f"start-only did not preserve duration: {moved.get('end')!r}")
        harness.ok("update_event: start-only keeps the stored duration")

        result = expect_tool_ok(
            server,
            "update_event",
            {"id": event_id, "duration": "45m"},
            "update duration",
        )
        resized = expect_object(json_of_result(result, "update duration"), "update result")
        expect(resized.get("start") == "2030-01-15T11:00:00Z",
               f"duration moved the start: {resized.get('start')!r}")
        expect(resized.get("end") == "2030-01-15T11:45:00Z",
               f"duration did not recompute the end: {resized.get('end')!r}")
        expect(structured_of_result(result, "update duration") == resized,
               "update duration structuredContent mismatch")
        harness.ok("update_event: duration recomputes the end")
    finally:
        expect_tool_ok(server, "delete_event", {"id": event_id}, "update semantics cleanup")


def check_query(server: McpServer, harness: Harness, event_id: int) -> None:
    result = expect_tool_ok(
        server,
        "create_event",
        {
            "title": "Sleep smoke",
            "start": "2030-01-15T22:00:00Z",
            "end": "2030-01-15T23:00:00Z",
            "type": "sleep",
            "hidden": True,
        },
        "query create sleep",
    )
    created = expect_object(json_of_result(result, "query create sleep"), "create result")
    sleep_id = created.get("id")
    expect(isinstance(sleep_id, int) and sleep_id > 0, "query create sleep returned no id")

    try:
        rows = list_rows(server, {"from": DAY_FROM, "to": DAY_TO}, "query any")
        expect(len(rows) == 2, f"query any returned {len(rows)} rows, want 2")

        visible = list_rows(
            server, {"from": DAY_FROM, "to": DAY_TO, "visibility": "visible"}, "query visible"
        )
        expect(len(visible) == 1 and visible[0].get("id") == event_id,
               f"query visible returned {visible!r}")

        hidden = list_rows(
            server, {"from": DAY_FROM, "to": DAY_TO, "visibility": "hidden"}, "query hidden"
        )
        expect(len(hidden) == 1 and hidden[0].get("id") == sleep_id,
               f"query hidden returned {hidden!r}")

        searched = list_rows(
            server, {"from": DAY_FROM, "to": DAY_TO, "search": "sleep smoke"}, "query search"
        )
        expect(len(searched) == 1 and searched[0].get("id") == sleep_id,
               f"query search returned {searched!r}")

        typed = list_rows(
            server, {"from": DAY_FROM, "to": DAY_TO, "type": "sleep"}, "query type"
        )
        expect(len(typed) == 1 and typed[0].get("id") == sleep_id, f"query type returned {typed!r}")

        sorted_rows = list_rows(
            server, {"from": DAY_FROM, "to": DAY_TO, "sort": "-start"}, "query sort"
        )
        expect(sorted_rows and sorted_rows[0].get("id") == sleep_id,
               f"query sort returned {sorted_rows!r}")

        empty = list_rows(
            server, {"from": DAY_FROM, "to": DAY_TO, "limit": 0}, "query limit"
        )
        expect(empty == [], f"limit 0 returned {empty!r}")
        harness.ok("list_events: visibility, search, type, sort and limit 0")
    finally:
        expect_tool_ok(server, "delete_event", {"id": sleep_id}, "query cleanup")


def check_defaults(server: McpServer, harness: Harness) -> None:
    result = expect_tool_ok(
        server,
        "create_event",
        {"title": "Now smoke", "duration": "30m"},
        "defaults create",
    )
    created = expect_object(json_of_result(result, "defaults create"), "create result")
    event_id = created.get("id")
    expect(isinstance(event_id, int) and event_id > 0, "defaults create returned no id")

    try:
        rows = list_rows(server, {}, "defaults list")
        ids = {row.get("id") for row in rows if isinstance(row, dict)}
        expect(event_id in ids, f"default list did not include the new now event: {ids}")
        harness.ok("create_event: start defaults to now; list_events defaults to today..+7d")
    finally:
        expect_tool_ok(server, "delete_event", {"id": event_id}, "defaults cleanup")


def check_cli_row(harness: Harness, event_id: int) -> None:
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


def check_errors(server: McpServer, harness: Harness, event_id: int) -> None:
    response = server.rpc("tools/call", {"name": "frobnicate", "arguments": {}})
    error = expect_object(response.get("error"), "unknown-tool protocol error")
    expect(error.get("code") == -32602, f"unknown tool returned code {error.get('code')!r}")
    expect("unknown tool" in str(error.get("message", "")), f"unexpected message: {error}")
    harness.ok("tools/call: an unknown tool is a protocol error")

    expect_tool_error(
        server, "create_event", {"title": "", "start": START, "end": END},
        "title", "empty title",
    )
    harness.ok("create_event: an empty title is rejected")

    expect_tool_error(
        server,
        "create_event",
        {"title": "Loose bool", "start": START, "duration": "30m", "hidden": "no"},
        "hidden must be true or false",
        "string hidden flag",
    )
    harness.ok("create_event: hidden must be a real boolean")

    expect_tool_error(
        server,
        "create_event",
        {"title": "Typo", "start": START, "duration": "30m", "starts": START},
        "unknown argument",
        "unknown argument",
    )
    harness.ok("create_event: unknown arguments are rejected")

    expect_tool_error(
        server,
        "create_event",
        {"title": "Backwards", "start": END, "end": START},
        "end must be at or after start",
        "backwards range",
    )
    harness.ok("create_event: end before start is rejected")

    expect_tool_error(
        server,
        "update_event",
        {"id": event_id},
        "at least one field",
        "empty update",
    )
    harness.ok("update_event: an id-only update is rejected")

    expect_tool_error(
        server,
        "create_event",
        {"title": "Both", "start": START, "end": END, "duration": "30m"},
        "end or duration",
        "both end and duration",
    )
    harness.ok("create_event: end and duration are mutually exclusive")

    expect_tool_error(server, "get_event", {"id": "1"}, "must be an integer", "string id")
    harness.ok("get_event: a string id is rejected")

    expect_tool_error(
        server, "list_events", {"limit": 501}, "between 0 and 500", "limit above the cap"
    )
    harness.ok("list_events: a limit above 500 is rejected")

    expect_tool_error(
        server, "list_events", {"search": "   "}, "search must not be empty", "empty search"
    )
    harness.ok("list_events: an empty search is rejected")


def check_delete(server: McpServer, harness: Harness, event_id: int) -> None:
    result = expect_tool_ok(server, "delete_event", {"id": event_id}, "delete_event")
    deleted = expect_object(json_of_result(result, "delete_event"), "delete_event result")
    expect(deleted.get("deleted") == event_id, f"delete_event did not report the id: {deleted!r}")
    expect(structured_of_result(result, "delete_event") == {"deleted": event_id},
           "delete_event structuredContent mismatch")
    harness.ok(f"delete_event -> id {event_id}")

    is_error, text = server.call("get_event", {"id": event_id})
    expect(is_error, f"get_event after delete did not report an error: {text}")
    harness.ok("get_event: a deleted row is an error")

    result = expect_tool_ok(
        server, "list_events", {"from": DAY_FROM, "to": DAY_TO}, "list after delete"
    )
    rows = expect_list(json_of_result(result, "list after delete"), "list after delete rows")
    expect(rows == [], f"list_events after delete returned {len(rows)} rows: {rows}")
    list_structured = expect_object(
        structured_of_result(result, "list after delete"), "list after delete structured"
    )
    expect(list_structured.get("events") == [], "list after delete structuredContent is not empty")
    harness.ok("list_events: the range is empty after delete")

    result = expect_tool_ok(
        server, "delete_event", {"id": 999999}, "delete missing id"
    )
    missing = expect_object(json_of_result(result, "delete missing id"), "delete missing result")
    expect(missing.get("deleted") == 999999, f"delete missing id returned {missing!r}")
    harness.ok("delete_event: a missing id is an idempotent success")

    if harness.cli is not None:
        shown = run_cli(harness, ["show", str(event_id), "--db", str(harness.db), "--format", "json"])
        expect(shown.returncode == 1, f"CLI show after delete exited {shown.returncode}, want 1")
        harness.ok("CLI cross-check: the MCP delete removed the row from SQLite")


def check_read_only(harness: Harness) -> None:
    original_mode = harness.db.stat().st_mode & 0o777
    readonly: McpServer | None = None
    os.chmod(harness.db, 0o444)
    try:
        readonly = McpServer(harness, "readonly", permissions="read").start()
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

        result = expect_tool_ok(
            readonly, "list_events", {"from": DAY_FROM, "to": DAY_TO}, "read-only list"
        )
        rows = expect_list(json_of_result(result, "read-only list"), "read-only rows")
        expect(rows == [], "read-only list is not empty")
        structured = expect_object(
            structured_of_result(result, "read-only list"), "read-only structured"
        )
        expect(structured.get("events") == [], "read-only structured is not empty")
        harness.ok("read-only: list works on a write-protected database")
    finally:
        try:
            if readonly is not None:
                close_server(readonly, harness, "read-only server")
        finally:
            os.chmod(harness.db, original_mode)

    completed = run_server(
        harness,
        ["--db", str(harness.missing_db), "--permissions", "read"],
        "read-only missing DB",
    )
    expect(completed.returncode == 1, f"read-only missing DB exited {completed.returncode}")
    expect(not harness.missing_db.exists(), "a read-only server created the missing database")
    expect("read-only server" in completed.stderr, f"missing DB hint absent: {completed.stderr!r}")
    harness.ok("read-only: a missing database is reported, not created")


def check_write_tier(harness: Harness) -> None:
    server: McpServer | None = None
    try:
        server = McpServer(harness, "readwrite", permissions="read,write").start()
        response = server.rpc("tools/list")
        result = expect_object(response.get("result"), "read,write tools/list result")
        tools = expect_list(result.get("tools"), "read,write tools")
        names = {tool.get("name") for tool in tools if isinstance(tool, dict)}
        expect(
            names == {"list_events", "get_event", "create_event", "update_event"},
            f"read,write server lists {sorted(names)}",
        )
        expect("delete_event" not in names, "read,write server advertises delete_event")
        harness.ok("read,write: tools/list omits the destructive tier")

        is_error, text = server.call("delete_event", {"id": 1})
        expect(is_error, f"read,write delete_event was not denied: {text}")
        expect("permission denied" in text, f"unexpected denial message: {text!r}")
        harness.ok("read,write: delete_event is denied")
    finally:
        if server is not None:
            close_server(server, harness, "read,write server")


def check_encryption(harness: Harness) -> None:
    harness.key_file.write_text(PASSPHRASE + "\n", encoding="utf-8")
    harness.key_file.chmod(0o600)
    harness.wrong_key_file.write_text("wrong passphrase\n", encoding="utf-8")
    harness.wrong_key_file.chmod(0o600)

    event_id: int | None = None
    server: McpServer | None = None
    try:
        server = McpServer(
            harness,
            "encrypted",
            db=harness.encrypted_db,
            key_file=harness.key_file,
        ).start()
        check_handshake(server, harness, prefix="encrypted: ")
        result = expect_tool_ok(
            server,
            "create_event",
            {"title": "Encrypted smoke", "start": START, "end": END},
            "encrypted create",
        )
        created = expect_object(json_of_result(result, "encrypted create"), "encrypted result")
        event_id = created.get("id")
        expect(isinstance(event_id, int) and event_id > 0, "encrypted create returned no id")
        fetched = expect_tool_ok(
            server, "get_event", {"id": event_id}, "encrypted get"
        )
        row = expect_object(json_of_result(fetched, "encrypted get"), "encrypted row")
        expect(row.get("title") == "Encrypted smoke", f"encrypted row is {row!r}")
        harness.ok("encryption: --key-file creates and reads an encrypted database")
    finally:
        if server is not None:
            close_server(server, harness, "encrypted server")

    if event_id is None:
        raise Failure("encryption: create_event returned no id")
    if harness.cli is not None:
        shown = run_cli(
            harness,
            [
                "show",
                str(event_id),
                "--db",
                str(harness.encrypted_db),
                "--key-file",
                str(harness.key_file),
                "--format",
                "json",
            ],
        )
        expect(shown.returncode == 0, f"CLI encrypted show exited {shown.returncode}: {shown.stderr}")
        row = expect_object(parse_tool_json(shown.stdout, "CLI encrypted show"), "CLI encrypted row")
        expect(row.get("title") == "Encrypted smoke", f"CLI encrypted row is {row!r}")
        harness.ok("encryption: the CLI reads the MCP-written encrypted row")
    else:
        harness.skip("encryption CLI cross-check: klend_ario_cli was not found")

    readonly: McpServer | None = None
    try:
        readonly = McpServer(
            harness,
            "encrypted-readonly",
            permissions="read",
            db=harness.encrypted_db,
            key_file=harness.key_file,
        ).start()
        rows = list_rows(
            readonly, {"from": DAY_FROM, "to": DAY_TO}, "encrypted read-only list"
        )
        expect(len(rows) == 1 and rows[0].get("title") == "Encrypted smoke",
               f"encrypted read-only rows: {rows!r}")
        harness.ok("encryption: read-only opens the encrypted database with --key-file")
    finally:
        if readonly is not None:
            close_server(readonly, harness, "encrypted read-only server")

    envreadonly: McpServer | None = None
    try:
        envreadonly = McpServer(
            harness,
            "encrypted-env",
            permissions="read",
            db=harness.encrypted_db,
            env={"KLEND_ARIO_KEY": PASSPHRASE},
        ).start()
        rows = list_rows(
            envreadonly, {"from": DAY_FROM, "to": DAY_TO}, "encrypted env list"
        )
        expect(len(rows) == 1, f"encrypted env rows: {rows!r}")
        harness.ok("encryption: KLEND_ARIO_KEY opens the encrypted database")
    finally:
        if envreadonly is not None:
            close_server(envreadonly, harness, "encrypted env server")

    completed = run_server(
        harness,
        ["--db", str(harness.encrypted_db), "--key-file", str(harness.wrong_key_file)],
        "encrypted wrong key",
    )
    expect(completed.returncode == 1, f"wrong key exited {completed.returncode}")
    expect("passphrase" in completed.stderr or "wrong key" in completed.stderr,
           f"wrong key stderr: {completed.stderr!r}")
    harness.ok("encryption: a wrong key is rejected")

    completed = run_server(
        harness,
        ["--db", str(harness.encrypted_db)],
        "encrypted no key",
    )
    expect(completed.returncode == 1, f"missing key exited {completed.returncode}")
    expect("encrypted" in completed.stderr or "key-file" in completed.stderr,
           f"missing key stderr: {completed.stderr!r}")
    harness.ok("encryption: an encrypted database without a key explains the hint")

    completed = run_server(
        harness,
        [
            "--db",
            str(harness.encrypted_db),
            "--key-file",
            str(harness.wrong_key_file),
            "--permissions",
            "read",
        ],
        "encrypted read-only wrong key",
    )
    expect(completed.returncode == 1, f"read-only wrong key exited {completed.returncode}")
    expect("passphrase" in completed.stderr, f"read-only wrong key stderr: {completed.stderr!r}")
    expect("read-only server" not in completed.stderr,
           f"a wrong key got the read-only hint: {completed.stderr!r}")
    harness.ok("encryption: a wrong key is not blamed on read-only mode")

    completed = run_server(
        harness,
        ["--db", str(harness.encrypted_db), "--key-file", str(harness.missing_key_file)],
        "missing key file",
    )
    expect(completed.returncode == 1, f"missing key file exited {completed.returncode}")
    expect("cannot read key file" in completed.stderr, f"missing key file stderr: {completed.stderr!r}")
    harness.ok("encryption: a missing key file is a clear startup error")


def check_server_options(harness: Harness) -> None:
    completed = run_server(
        harness, ["--version", "--db", str(harness.option_db)], "--version"
    )
    expect(completed.returncode == 0, f"--version exited {completed.returncode}")
    expect("klend_ario_mcp_server" in completed.stdout, f"unexpected version: {completed.stdout!r}")
    expect(not harness.option_db.exists(), "--version created the option database")
    harness.ok("--version")

    completed = run_server(
        harness,
        ["--permissions", "read,bogus", "--db", str(harness.option_db)],
        "unknown permission",
    )
    expect(completed.returncode == 2, f"unknown permission exited {completed.returncode}, want 2")
    expect("unknown permission" in completed.stderr, f"unexpected stderr: {completed.stderr!r}")
    expect(not harness.option_db.exists(), "the permission error opened the option database")
    harness.ok("--permissions: an unknown token is a usage error")

    completed = run_server(harness, ["--db"], "--db without value")
    expect(completed.returncode == 2, f"--db without a value exited {completed.returncode}")
    expect("needs a value" in completed.stderr, f"unexpected stderr: {completed.stderr!r}")
    harness.ok("--db: a missing value is a usage error")


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
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        print("error: --timeout must be a positive finite number", file=sys.stderr)
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
        check_protocol(primary, harness)
        event_id = check_crud(primary, harness)
        check_update_semantics(primary, harness)
        check_query(primary, harness, event_id)
        check_defaults(primary, harness)
        check_cli_row(harness, event_id)
        check_errors(primary, harness, event_id)
        check_delete(primary, harness, event_id)

        close_server(primary, harness, "primary server")
        primary = None

        check_read_only(harness)
        check_write_tier(harness)
        check_encryption(harness)
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
        if args.keep:
            print(f"kept: {harness.tmp}", flush=True)

    print(f"mcp_smoke: {harness.checks} checks passed")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
