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
| Storage engine | **SQLite**, vendored `sqlite3.c`/`sqlite3.h` amalgamation now |
| sqlite3 version | **3.46.1**, vendored at `libs/thirdparty/sqlite3/` (taken from `Matrix_Player/third_party`); Phase 3 replaces the amalgamation with SQLite3MultipleCiphers |
| Timestamps | Unix **seconds, UTC**. ISO-8601 UTC is the interchange format (`2026-10-16T09:00:00Z`, seconds optional); a per-event UTC offset is deliberately deferred |
| Build directories | `build/` = Debug (the dev loop; tests are Debug-only), `build_release/` = Release |
| Encryption | **SQLite3MultipleCiphers**, Phase 3 (not vendored yet) |
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
- **A calendar stores instants, not wall-clock readings.** `events.start_utc`/
  `end_utc` are Unix seconds UTC, and the ISO-8601 parser/formatter is strict and
  UTC-only (`Z`) on purpose: a silently reinterpreted date is worse than a
  rejected one. Rendering an instant into a *local* week is a UI concern for a
  later phase, and a per-event offset (for "09:00 wherever I am") is an explicit
  open item, not an oversight.
- **Vendor what must build identically everywhere.** sqlite3 is vendored rather
  than found on the system so Linux and Android compile the same version with no
  package to install. `SQLite3MultipleCiphers` will be vendored the same way in
  Phase 3.

## 6. Glossary

- **`arc_fs`** — `archive_engine`'s storage module: paths, layout, non-throwing
  walk, `listDir`, volume access mode. We consume only this module.
- **`app_shell`** — the Host/AppView seam and per-app paths/metrics, lifted out
  of Matrix_Player. Relevant at Phase 4, not before.
- **`vk_canvas`** — the Vulkan 2D canvas + text engine. The GUI renderer.
- **`merged.md`** — the product plan. **`git_wrapper`** — the commit tool at the
  repo root (see `AGENTS.md`).
