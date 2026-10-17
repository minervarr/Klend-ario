# klend_ario_mcp_server — the AI-facing calendar surface

The local MCP (Model Context Protocol) server. It is a **frontend**: it links
`core/`, opens the database through `Db`, and knows nothing about the CLI or the
GUI. The product decisions are in [`../merged.md`](../merged.md) §7; the rules
are in [`../essentialAgent/AGENTS.md`](../essentialAgent/AGENTS.md).

- **Transport:** newline-delimited JSON-RPC over stdin/stdout — MCP's local
  `command` kind. **stdout carries protocol frames and nothing else.**
- **Tools:** five, intentionally: `list_events`, `get_event`, `create_event`,
  `update_event`, `delete_event`. `list_events` carries the query surface
  (search, type, visibility, sort, limit, offset) instead of adding tools.
- **Time:** UTC. Inputs accept ISO-8601, date-only, and the core time grammar
  (`today`, `tomorrow`, `week`, `+90m`, `today+2h`); durations accept `90m`,
  `2h`, `1h30m`, `1d`.

## Build and run

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C build
./build/mcp/klend_ario_mcp_server --help
```

Options:

```text
--db PATH            database file (default: arc::fs::layout("klend_ario").state)
--key-file PATH      first line is the passphrase (else $KLEND_ARIO_KEY; else plaintext)
--permissions LIST   any of read,write,destructive (default: all)
--version
```

A **read-only** deployment (`--permissions read`) opens the database with
`Db::openReadOnly`/`Db::openEncryptedReadOnly`: it never creates, migrates or
writes a file. A missing database is a startup error, not an empty calendar.

## OpenCode

`merged.md` §7 makes OpenCode the AI client. The repository config
([`../opencode.jsonc`](../opencode.jsonc)) registers the built binary with all
tiers on. A minimal user config is:

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
destructive edits. The server's `initialize.instructions` and each tool's
`annotations` are what a host shows the model and the user; the demo and the
smoke test assert they are present.

If the database is encrypted, use `--key-file` or `KLEND_ARIO_KEY`; never put a
real passphrase in a tracked config.

## Tool contract

| Tool | Tier | Semantics |
|---|---|---|
| `list_events` | read | Half-open `[from, to)` overlap. Defaults: today 00:00 UTC, +7 days, visibility `any`, sort `start`, 100 rows (max 500). `to` is exclusive. `search` is a literal ASCII case-insensitive substring of title+notes. |
| `get_event` | read | One row by positive integer id, including notes. |
| `create_event` | write | `title` required; `start` defaults to now; give `end` **xor** `duration`. Returns `{id}`. |
| `update_event` | write | Only supplied fields change. `start` alone keeps the stored duration; `duration` recomputes the end; `end` and `duration` are mutually exclusive. Returns the row. |
| `delete_event` | destructive | Idempotent: a missing id is a no-op success. Returns `{deleted}`. |

Validation is strict on purpose: unknown arguments, non-integral ids, string
booleans, out-of-range importance/urgency, and `end < start` are errors with an
actionable message. `tools/list` advertises only the enabled tiers.

### Visibility and alarms

`hidden` is a weekly-view flag, not deletion. `list_events` defaults to `any`
(data verbs see hidden rows); pass `visibility: "visible"` for a clean agenda or
`"hidden"` to find sleep blocks. Hidden rows still fire alarms.

### Results

Successful data calls put compact JSON in `content[0].text` (what OpenCode
feeds to the model) and a host-renderable value in `structuredContent`. For a
single event or the small `{id}`/`{deleted}` results those are the same object;
`list_events` keeps the bare array in text (the CLI's JSON shape) and wraps it
as `{"events": [...]}`, because the 2025-era `structuredContent` field is an
object. Output schemas are deliberately deferred: they are paid for in every
`tools/list` prompt, so they wait for a consumer that validates them.

## Protocol policy

The server advertises **`2025-06-18`**, the revision it actually implements.
MCP `2026-07-28` (stateless requests, `server/discover`, `resultType`) is a
separate adoption, not a default; OpenCode's SDK still defaults to the 2025
handshake. When it lands, it will be a recorded decision and a new test.

## Tests

- `mcp/tests/json_test.cc` — the hand-rolled parser/serializer.
- `mcp/tests/server_test.cc` — JSON-RPC dispatch, instructions, tool metadata,
  strict inputs, query semantics, tiers, structured content.
- `demo/mcp_smoke.py` — end-to-end over real stdio against a throwaway database.
