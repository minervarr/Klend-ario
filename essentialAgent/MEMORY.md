# MEMORY — the anchor

*Why Klend-ario exists and every decision already made, so no agent has to guess
or re-litigate. If you are an agent: read this before proposing anything.*

---

## 1. What it is

Klend-ario is a **privacy-first, AI-native personal calendar**: one encrypted
SQLite file, synced by Syncthing, queryable through an MCP server, rendered as a
weekly 16:9 view. Written natively in **C/C++**, running on **Linux and Android**
(Windows kept buildable where it is free, but never the reason for a design).

The full product design is [`../merged.md`](../merged.md) — *"Openseek — Final
Plan (v3)"*. That document is the **product** plan; this folder is the **how-we-
build-it** plan. Where they differ, `merged.md` wins on *what the app does*, this
folder wins on *how the code is organised*.

The product is titled **Openseek** in that plan. The repository, the code
namespace, and every CMake target are **Klend-ario / `klend_ario`**. Do not
introduce an `openseek` identifier into the code; the name lives in prose only.

## 2. Why it exists

Same mission as its sibling projects: software that should be free and excellent,
built natively instead of borrowing someone else's ground. The calendar is the
organising surface for a life, kept private (no cloud, no account), and let the
user's own AI work on it locally. See [`../merged.md`](../merged.md) for the
voice and the "one-line vision".

## 3. Ordering of work (locked)

The **CLI is the first app**, not the GUI. It is the way the core is driven and
verified end to end. The **GUI comes last** and links the same core. Nothing in
`core/` may depend on a frontend.

```
Phase 0 — scaffold      CMake+Ninja, archive_engine submodule, pyz build tool, docs   ← you are here
Phase 1 — core          SQLite schema + storage layer + model, with tests
Phase 2 — CLI MVP       add / list / show / rm  (the "app" until the GUI exists)
Phase 3 — encryption    SQLite3MultipleCiphers + Syncthing sync policy
Phase 4 — GUI           weekly 16:9 view (vk_canvas + app_shell)
Phase 5 — MCP           local read/write server, minimal tool surface
Phase 6 — alarms        libnotify + PipeWire / Android native
Phase 7 — Android       platform layer, storage permissions
```

**MVP = Phases 0–2.**

## 4. Decisions made (do not re-open without a reason)

These were decided for this repository specifically, on top of `merged.md`:

| Decision | Value |
|---|---|
| C++ standard | **C++17**, matching the whole workspace |
| Namespace | `klend_ario::` |
| Targets | `klend_ario_core` (static lib), `klend_ario_cli` (executable) |
| `arc_fs` app name | `"klend_ario"` |
| Storage engine | **SQLite**, vendored as the **SQLite3MultipleCiphers 2.6.1** amalgamation (embeds SQLite 3.54.0) at `libs/thirdparty/sqlite3mc/`; one header gives both APIs |
| Timestamps | Unix **seconds, UTC**. ISO-8601 UTC is the interchange format (`2026-10-16T09:00:00Z`, seconds optional); a per-event UTC offset is deliberately deferred |
| Build directories | `build/` = Debug (the dev loop; tests are Debug-only), `build_release/` = Release |
| Encryption | **SQLite3MultipleCiphers 2.6.1**, page-level, pinned to **ChaCha20-Poly1305** (`PRAGMA cipher='chacha20'` before `sqlite3_key`); passphrase-derived, no keyfile format of our own |
| DB location | `arc::fs::Layout.state` — machine data, never `home`/`data` |
| Error type | `arc::Result<T>` / `arc::Error` from `arc_util` |
| `archive_engine` | git submodule at `libs/firstparty/archive_engine` |
| Tests | plain `assert()` binaries, registered with **CTest** |
| CLI parsing | hand-rolled, `std::string_view` + `std::vector`, std-only |
| Build tool | stdlib-only Python **zipapp** (`scripts/`), **Linux only for now** |
| Build system | **CMake + Ninja only** |
| Frontends | `cli/` now, `gui/` later, `android/` later |

Product decisions (from `merged.md`, §1) that bind the code: events are
**individual rows**; importance → hue (violet 0 → red 100); urgency → border
thickness; week starts **Sunday**; sleep hours hideable; alarms fire at
**T−offset and T=0**; MCP is **read/write from day one** with permission tiers.

## 5. Lessons inherited from the sibling projects

These are truths this workspace already paid for. Do not relearn them.

- **`arc`, not `ae`.** `archive_engine` renamed its namespace from `ae::`/`ae_*`
  to `arc::`/`arc_*` precisely because `audio_engine` already owns `ae`. Two
  `ae::Result` definitions would be a silent ODR violation. Never reuse another
  framework library's prefix.
- **`core/` does no platform work.** `archive_engine`'s `core/` has no SDK
  includes and no JNI; the two things that genuinely need a JVM (the private
  directory, the media index) are *pushed in* from `platform/android/`. We keep
  the same rule.
- **A walk must not throw.** `std::filesystem`'s recursive iterator throws on an
  unreadable directory; in a scan thread that kills the process. Use
  `arc::fs::walk()` / `arc::fs::listDir()`, which cannot throw and count errors.
- **Databases do not belong on Android shared storage.** Since Android 11 it is a
  FUSE mount: every metadata op is a userspace round-trip. That is exactly why
  `arc::fs::Layout` splits `state`/`cache` (fast, private) from
  `home`/`data` (user-visible, portable). The DB goes in `state`.
- **A companion filesystem layout is the user's, not ours.** Find sidecars with
  `arc::fs::ancestorsUpTo()` rather than assuming a depth.
- **Submodules push before parents.** A fresh recursive clone points at gitlinks
  the remote must already have. `./git_wrapper` enforces the order; a manual
  `git push` in the wrong order is how clones break.
- **Shared libraries are consumed by `add_subdirectory`, never packaged.**
  `archive_engine`, `app_shell`, `vk_canvas` are all pinned submodules. A change
  to one is pushed there first, then the pin advanced here.
- **View geometry is pure, and pure geometry is tested.** The weekly view's
  layout (`gui/src/week_layout.*`) takes pixels and instants and returns
  rectangles — no Canvas, no OS, no window. That is what lets it be asserted
  exhaustively on a machine with no GPU, and it is why the layout is the first
  half of Phase 4 rather than the last. Colours stay in core
  (`klend_ario::styleFor`) so every surface that draws an event agrees.
- **The CLI command layer is separate from `main()`.** `cli/src/commands.cc`
  holds exactly one function per verb and a single `runCommand`; `main()` only
  builds the arg vector and calls it. That is what lets `cli/tests/e2e_test.cc`
  drive the REAL parse→dispatch→core path against a temp database — a
  parsing-only test cannot catch a verb that stopped reaching core.
- **Migrations are additive and refused in the wrong direction.** The schema
  version lives in `meta.schema_version` and is checked on every open. Steps run
  upward only, in order. A database written by a *newer* build is **refused**
  rather than opened and quietly misread — forward compatibility is not ours to
  guess, and a silently wrong calendar is worse than a startup error.
- **A calendar stores instants, not wall-clock readings.** `events.start_utc`/
  `end_utc` are Unix seconds UTC, and the ISO-8601 parser/formatter is strict and
  UTC-only (`Z`) on purpose: a silently reinterpreted date is worse than a
  rejected one. Rendering an instant into a *local* week is a UI concern for a
  later phase, and a per-event offset (for "09:00 wherever I am") is an explicit
  open item, not an oversight.
- **Vendor what must build identically everywhere.** sqlite3 is vendored rather
  than found on the system so Linux and Android compile the same version with no
  package to install — first the plain amalgamation (Phase 0), now the
  SQLite3MultipleCiphers amalgamation that carries the encryption (Phase 3).
  Record the exact version and the download's checksum beside the vendored
  files (`libs/thirdparty/sqlite3mc/README.md`).

## 6. Glossary

- **`arc_fs`** — `archive_engine`'s storage module: paths, layout, non-throwing
  walk, `listDir`, volume access mode. We consume only this module.
- **`app_shell`** — the Host/AppView seam and per-app paths/metrics, lifted out
  of Matrix_Player. Relevant at Phase 4, not before.
- **`vk_canvas`** — the Vulkan 2D canvas + text engine. The GUI renderer.
- **`merged.md`** — the product plan. **`git_wrapper`** — the commit tool at the
  repo root (see `AGENTS.md`).

## 7. Encryption and sync (Phase 3)

Two of `merged.md`'s §11 open items were settled here; the reasoning is short
enough to keep, because both were decided against an obvious alternative.

### Encryption

- **Passphrase, not a keyfile.** The database is a single file that Syncthing
  copies between devices, so it must be self-contained: a passphrase is derived
  against a **salt the file carries**, and nothing else needs to travel with it.
  A separate key file would be a second thing to sync, to lose, and to keep in
  step with the database it opens.
- **ChaCha20-Poly1305, pinned.** `core/src/db.cc` runs `PRAGMA cipher =
  'chacha20'` **before** `sqlite3_key()`. ChaCha20 is also SQLite3MultipleCiphers'
  own default, but stating it makes the on-disk format our explicit choice rather
  than a library default that could change under an upgrade — a file written
  today must still open in a year.
- **A wrong passphrase fails at open**, not on a later read. The first schema
  query returns `SQLITE_NOTADB`, which `Db::openEncrypted` turns into "wrong key,
  or not a Klend-ario database". An **empty** passphrase is refused outright
  rather than silently meaning "plaintext".
- **Where the secret comes from is the frontend's job.** Core takes a
  `std::string`; the CLI reads `--key-file <path>` (first line) or
  `$KLEND_ARIO_KEY`, and does nothing about prompts yet.

### Sync

- **One file, last-writer-wins, and conflicts are SURFACED, not merged.**
  Syncthing does not merge a file; it keeps one copy and writes the other as
  `<stem>.sync-conflict-<date>-<time>-<device><ext>` (the marker goes *before*
  the extension). Auto-merging two calendars is not something we can do
  correctly, so the policy is to **detect and report** — `klend_ario::
  syncConflictCopies()` (pure matcher + a non-throwing `listDir`), and the CLI
  prints a warning on every open while one is present.
- **Every write stamps `meta.modified_utc`** (`Db::touchModified`), so "when was
  this last changed, and by which side of a conflict" has an answer in the file
  itself.
- A conflict copy is a **separate file**, never opened or read; resolving one is
  a human's call (rename it in, or delete it).

## 8. MCP (Phase 5)

- **stdio, one JSON-RPC message per line.** Transport is MCP's `command` kind:
  the host spawns `klend_ario_mcp_server` and speaks over stdin/stdout. **stdout
  carries protocol frames and nothing else** — a stray `printf` corrupts the
  session, so logging goes to stderr or nowhere.
- **Five tools, no more** (`merged.md` §7): `list_events`, `get_event`,
  `create_event`, `update_event`, `delete_event`. Context tokens are the cost
  model, so the surface stays small and the payloads stay shaped.
- **Permission tiers are real from day one** — `read` / `write` /
  `destructive`, set with `--permissions`. All default on, but the split exists
  now so restricting a deployment later is configuration, not a refactor.
- **JSON is hand-rolled** (`mcp/src/json.*`). The dependency rule allows no JSON
  library, and MCP's payloads are a small subset of the grammar — so it is a
  subset, depth-capped against a hostile payload, and asserted in `json_test`.
- The server is a **frontend**: it links core, opens the database through `Db`,
  and never reaches past it. `update_event` is the user-facing path for the
  `Db::updateEvent` added in Phase 1.

## 9. Alarms (Phase 6)

- **Deciding is pure; delivering is a seam.** WHEN an alarm fires is
  `klend_ario::alarmsIn` / `dueAlarms` / `nextAlarm` in core — a function of the
  events and a time window, asserted in `alarm_test`. HOW it is delivered is a
  `Notifier` installed by the `alarm/` frontend (libnotify on Linux), the same
  installed-backend shape as `arc_fs`'s media index.
- **Two triggers per event** (`merged.md` §6): T-offset first, then T=0, both
  from `event.hh`'s `alarmTriggersUtc`.
- **The poll window is (previous poll, now],** and the FIRST poll opens
  `now - horizon` back (default 60s) so a restart does not replay yesterday's
  alarms while a late start still catches a recent one. `engine_test` pins both
  the window and the no-replay behaviour.
- **A notifier that cannot work must not swallow the alarm.** libnotify is tried
  first; if it cannot initialise (headless, no session bus) the engine falls
  back to a stderr log, and `--notify log` forces that path. This was found the
  hard way: the first backend no-op'd silently.
- **PipeWire sound is NOT implemented yet.** Notifications fire; the audible
  half of `merged.md` §6 is an open gap, named rather than faked.
