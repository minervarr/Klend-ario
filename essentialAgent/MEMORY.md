# MEMORY — the anchor

*Why Klend-ario exists and every decision already made, so no agent has to guess
or re-litigate. If you are an agent: read this before proposing anything.*

---

## 1. What it is

Klend-ario is a **privacy-first, AI-native personal calendar**: one encrypted
SQLite file, synced by Syncthing, queryable through an MCP server, rendered as a
weekly 16:9 view. Written natively in **C/C++**, **desktop-first on Linux**.
Android remains a planned target but is **deferred** (the 2026-10-16 notes put
it after the desktop product because the platform is closed and hostile to FOSS
distribution). Windows is kept buildable where it is free, but never the reason
for a design.

The full product design is [`../merged.md`](../merged.md) — *"Openseek — Final
Plan (v3)"*. That document is the **product** plan; this folder is the **how-we-
build-it** plan. Where they differ, `merged.md` wins on *what the app does*, this
folder wins on *how the code is organised*.

The product is titled **Openseek** in that plan. The repository, the code
namespace, and every CMake target are **Klend-ario / `klend_ario`**. Do not
introduce an `openseek` identifier into the code; the name lives in prose only.

The 2026-10-16 notes add a notes companion, a 10-hour automatic sleep window,
G-SHOCK sleep/wake sync, NTP, and a storage-performance question. They are
captured in [`REQUIREMENTS.md`](REQUIREMENTS.md); **capture is not
implementation**. The holiday item in that note is already done — the C++17 port
lives in `pe-holiday-calc`.

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
Phase 0 — scaffold      CMake+Ninja, archive_engine submodule, pyz build tool, docs
Phase 1 — core          SQLite schema + storage layer + model, with tests
Phase 2 — CLI MVP       add / list / show / rm  (the "app" until the GUI exists)
Phase 3 — encryption    SQLite3MultipleCiphers + Syncthing sync policy
CLI power — stages 1-6  output/edit/time/query/intelligence/portability ← you are here
Phase 4 — GUI           weekly 16:9 view (vk_canvas + app_shell)   ← PAUSED
Phase 5 — MCP           local read/write server, minimal tool surface
Phase 6 — alarms        libnotify + PipeWire / Android native
Phase 7 — Android       platform layer, storage permissions   ← DEFERRED
Phase 8 — notes         notes companion cross-referenced to events   ← post-MVP
Phase 9 — sleep         10h automatic window, dynamic hide/show      ← post-MVP
Phase 10 — G-SHOCK      sleep/wake alarm sync (research, optional)   ← post-MVP
Gate — benchmark        SQLite vs JSON before any storage change
```

**MVP = Phases 0–2.**

**CLI before GUI (2026-10-17).** The desktop CLI is the product surface while
the GUI waits: output formats, editing, relative time, query power, calendar
intelligence, and import/export come first. Phase 4 is paused, not cancelled;
resume it after the CLI stages in [`PLAN.md`](PLAN.md) unless the order changes
again.

**Desktop-first.** Phase 7 is deferred, not cancelled. No Android/JNI/NDK work
until the desktop product is complete and that phase is explicitly reopened.
The post-MVP items (notes, sleep scheduler, G-SHOCK, NTP) are recorded in
[`REQUIREMENTS.md`](REQUIREMENTS.md); they do not move the current phase.

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
| Startup directories | `arc::fs::ensureLayout(layout, LayoutScope::App)` creates only `state`/`cache`/`config`/`data`. The user's `Music`/`Downloads`/`Pictures`/`Documents` are resolved from `user-dirs.dirs` and never created implicitly |
| Error type | `arc::Result<T>` / `arc::Error` from `arc_util` |
| `archive_engine` | git submodule at `libs/firstparty/archive_engine` |
| `pe-holiday-calc` | git submodule at `libs/firstparty/pe-holiday-calc` (Codeberg, pinned to the child's `master`); first-party C++17 holiday library, built with `add_subdirectory` (tests ON, example OFF). `klend_ario_core` links `pe_holiday_calc`; `core/src/holidays.cc` adapts its `std::optional` answers to `arc::Result` behind `core/include/klend_ario/holidays.hh` |
| Tests | plain `assert()` binaries, registered with **CTest** |
| CLI parsing | hand-rolled, `std::string_view` + `std::vector`, std-only |
| CLI output | **text/table/json/csv/tsv**; rendering is pure (`cli/src/format.cc`), so the bytes are tested. `--json` remains an alias for `--format json` |
| CLI time grammar | user-facing instants/durations are **pure core** (`klend_ario/timeexpr.hh`), UTC-only, resolved against an explicit `now` |
| CLI edits | `edit <id>` changes **only supplied flags** (`*Given` in `Args`); moving `--start` preserves duration unless `--end`/`--duration` says otherwise |
| CLI queries | Filters/sort/pagination are a core value (`klend_ario::EventQuery`) executed by `Db::queryEvents`; `listEvents` delegates. User text is bound; ORDER BY is a fixed whitelist. No in-memory filtering in a frontend |
| CLI search | Literal ASCII case-insensitive substring of **title + notes**. `%`/`_` are ordinary characters; a multi-word query is a phrase. No FTS until a benchmark asks for it |
| CLI hidden | `hidden` is a **weekly-view** flag, not deletion. `list`/`search` see hidden rows; `today`/`week`/`next`/`agenda` default to visible only. `--visible`/`--hidden-only`/`--include-hidden` override |
| CLI next | The first event with `end_utc > now`, so an event already underway counts. Visible only by default; limit 1, widened by `--limit` |
| CLI conflicts | Pairwise half-open overlaps. `EventType::Alarm` is a reminder, not a block, so it is ignored; touching endpoints and zero-length events never conflict. A triple overlap is three pairs. Hidden rows are filtered by the caller |
| CLI free | Merge busy intervals, clamp to the query range, emit gaps ≥ `--min-duration` (default 30m). Alarm rows never block; hidden rows only block with `--include-hidden` |
| CLI alarms | Upcoming T-offset + T=0 triggers, sorted by fire time. **Hidden rows are included by default**: `hidden` is a weekly-view choice, not a schedule cancellation. Events are scanned unbounded because an offset can move a trigger outside the event's own range |
| CLI stats | Default all time / all rows; `--from`/`--to`/`--type`/visibility narrow the set. `scheduled_seconds` is a **sum**, so overlaps double-count; `conflict_pairs` says how many overlaps exist. Alarm rows count in totals but not conflicts |
| CLI doctor | Read-only. Checks integrity, schema version, `modified_utc`, Syncthing copies and interval sanity; exit `1` on any warn/fail. It never migrates, fixes, or deletes |
| CLI export | Data access, not a view: all time/all rows by default, hidden included, stdout. `--format` is verb-scoped: `csv|jsonl|ics` (default csv). CSV/JSONL are the lossless interchange shapes; ICS is one-way |
| CLI import | Parse the whole file first, then write in one transaction. Append by default with fresh ids; `--replace` and `--preserve-ids` are explicit. Empty input is an error; `--dry-run` never opens the database |
| CLI completion | `completion bash|zsh|fish` prints a generated script. One per-verb table feeds all three shells, so suggestions cannot drift into invalid flags. The CLI is one binary, so completion must work from a fresh checkout without an install step |
| CLI database access | `doctor` and `export` use `Db::openReadOnly`/`openEncryptedReadOnly`: no create, no migrate, no writes. Explicit `--db` on a read-only view must exist; the default path may be created for first use |
| CLI status | Feature-complete and frozen 2026-10-17. New work is maintenance only; GUI Phase 4 is next |
| Build tool | stdlib-only Python **zipapp** (`scripts/`), **Linux only for now** |
| Demo | Permanent `demo/klend_ario_demo.py`: stdlib-only Python consumer of the built binaries; every run uses a throwaway temp `--db`, never the real `state` DB, and never links `core/`. See `DEMO.md` |
| Build system | **CMake + Ninja only** |
| Build tool & GUI | `scripts/klend_ario_builder.pyz` probes for `slangc`/`$VULKAN_SDK` and the Wayland/xkb `pkg-config` modules, then passes `-DKLEND_BUILD_GUI=ON`; `--gui`/`--no-gui` override, the menu asks. The CMake option defaults to OFF so a machine without the SDK still configures |
| GUI build | `-DKLEND_BUILD_GUI=ON` (default OFF): the window + capture tool need the Vulkan SDK, `slangc` and Wayland/xkb. `klend_ario_view` and its pure suites always build |
| GUI mode | **Read-only** (Phase 4a): `Db::openReadOnly`/`openEncryptedReadOnly`, CLI/MCP are the write paths. `--hidden` switches the query to `Visibility::Any` |
| GUI refresh | One `EventQuery` per visible week; a 1 s poll of `Db::dataVersion()` reloads when ANOTHER connection commits (that is how an MCP edit appears live). The now-line redraws on a timer |
| GUI local days | `gui/src/local_time` is the ONLY place a timezone is read; it returns local civil days with `[startUtc, endUtc)` bounds (23/25 h on DST days). Layout/scene take instants and never call the OS |
| GUI rendering | Three layers: pure geometry → pure `Scene` (draw ops) → a thin `vk_canvas` mapping. Colours are core (`rgbForStyle`, `textColorOn`); chrome lives in `gui/theme` |
| GUI text | **`RasterFont`** (per-size FreeType coverage atlas) through `Canvas::useMsdf` + `Renderer::initMsdf`; quads are `draw()`'s fifth argument. The legacy outline path is a missing-font fallback only — its winding fill is `SS=1` (1-bit). Not MTSDF: the sizes are fixed integers. See `GUI.md` §5 |
| GUI week label | "Week N · Mon YYYY" from `weekOfYearFromCivilDate`; a week containing 1 January is week **1 of the new year** even though it starts in December |
| GUI short events | Height stays proportional; labels are tiered by height (time, then title, then nothing). A five-minute event is a thin coloured line at its true start |
| GUI screenshots | `klend_ario_capture` renders fixture weeks through `VK_EXT_headless_surface` to PNG (Debug-only, `TZ=UTC0`), so the view is reviewable with no display |
| Frontends | `cli/` now, `gui/` later; Android later (deferred) |
| Scope | **Desktop-first**: Linux is the active platform; Android is deferred (Phase 7) |
| Time base | Unix seconds **UTC**; a day is the half-open interval `[0, 86400)` |
| Local time | A **display** concern only; other zones may be shown, always alongside UTC. No per-event offset yet |
| Time source | **NTP** at runtime (planned); local clock math is not a correctness source. Core has no network |
| Sleep default | **10 hours**, generated automatically as a dynamic `sleep` event; hide/show automatically; still user-overridable |
| Storage direction | **SQLite remains**; JSON is a benchmark candidate, not an adopted format. No rewrite without measured wins |
| Encryption priority | Implemented (Phase 3) and kept; **not a gate** for new work. Removing it is a separate security decision |
| Notes | Planned post-MVP companion; a note references an event by id, never copies it |
| G-SHOCK | Research spike; feasibility depends on the watch model/BLE protocol; no promise yet |

Product decisions (from `merged.md`, §1) that bind the code: events are
**individual rows**; importance → hue (violet 0 → red 100); urgency → border
thickness; week starts Sunday; the week counter is a Sunday-based week-of-year
(the US reading, not ISO 8601); sleep hours hideable; alarms fire at **T−offset
and T=0**; MCP is **read/write from day one** with permission tiers.

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
- **Do not create the user's directories.** `arc::fs::layout()` resolves
  Music/Downloads/Pictures/Documents from `user-dirs.dirs`, falling back to the
  conventional names only when it is absent. Klend-ario opens with
  `ensureLayout(..., LayoutScope::App)`, so a database open never leaves empty
  `~/Music`, `~/Downloads`, `~/Pictures`, or `~/Documents` behind.
- **Submodules push before parents.** A fresh recursive clone points at gitlinks
  the remote must already have. `./git_wrapper` enforces the order; a manual
  `git push` in the wrong order is how clones break.
- **Shared libraries are consumed by `add_subdirectory`, never packaged.**
  `archive_engine`, `pe_holiday_calc`, `app_shell`, `vk_canvas` are all pinned
  submodules. A change to one is pushed there first, then the pin advanced here.
- **A bare `std::optional` collapses two different answers.** The holiday
  library uses `nullopt` for both "unknown country" and "not a holiday today".
  `core/src/holidays.cc` resolves the provider first, so core can return `Err`
  for an unknown country and still return `Ok(nullopt)` for a valid day with no
  holiday — a frontend must be able to tell "I don't know that country" from
  "nothing special today".
- **View geometry is pure, and pure geometry is tested.** The weekly view's
  layout (`gui/src/week_layout.*`) takes pixels and instants and returns
  rectangles — no Canvas, no OS, no window. That is what lets it be asserted
  exhaustively on a machine with no GPU, and it is why the layout is the first
  half of Phase 4 rather than the last. Colours stay in core
  (`klend_ario::styleFor`) so every surface that draws an event agrees.
- **A vector outline is not a text cache.** The GUI's first slice emitted every
  glyph as Bézier records and let the compute winding fill decide inside/outside
  at `SS=1`; the result was 1-bit text (exactly two colours in a capture crop)
  while SDF boxes stayed smooth. `RasterFont` is the actual "Bézier outlines,
  rasterized once" design: FreeType bakes true coverage per (glyph, size,
  phase) into an R8 atlas, and each frame is quads. The `OverlayRasterizer`'s
  memcmp cache is full-scene, not a glyph cache. `initMsdf`/`useMsdf` name the
  shared pipeline, not the MTSDF choice.
- **The CLI command layer is separate from `main()`.** `cli/src/commands.cc`
  holds exactly one function per verb and a single `runCommand`; `main()` only
  builds the arg vector and calls it. That is what lets `cli/tests/e2e_test.cc`
  drive the REAL parse→dispatch→core path against a temp database — a
  parsing-only test cannot catch a verb that stopped reaching core.
- **Output is data, not a side effect.** `printf` inside a command made the
  user-visible contract untestable. Rendering pure strings (`format.cc`) means
  `format_test` asserts exact text/table/json/csv/tsv bytes, including CSV
  quoting and TSV escaping, with no stdout capture and no UTF-8 surprises in
  the test harness.
- **A partial edit needs "was it supplied?", not a default.** `--importance`
  absent must mean "leave it", while `--importance 50` means "set it to 50".
  `Args` therefore carries a `*Given` flag per editable field; defaults alone
  silently overwrite data.
- **Relative time is still calendar logic.** `today`, `week`, weekday names and
  `+90m` live in core (`timeexpr`) with `now` as a parameter, not in the CLI and
  not behind a clock call. That is what makes leap days, pre-epoch floors, and
  overflow assertable without a flaky test or a timezone database.
- **A filter is a query, not a post-processing loop.** Putting `EventQuery` in
  core and building one prepared statement means the CLI, MCP and the weekly
  view cannot disagree about what “overlapping” or “hidden” means; `listEvents`
  delegates rather than keeping a second SQL copy. User text is bound, and
  ORDER BY is selected from a whitelist — a search term can never change the
  shape of the statement.
- **“Hidden” is not “deleted”, and not “irrelevant”.** It is a property of the
  weekly view (sleep blocks). Data verbs (`list`, `search`) must still see those
  rows so they can be edited or unhidden; view verbs (`today`, `week`, `next`,
  `agenda`) filter them by default. Confusing the two either hides data with no
  way back or floods the view with sleep blocks.
- **“Next” follows the overlap rule.** A half-open range starting at `now`
  includes an event whose `end_utc > now`, so `next` does too. Defining it as
  `start_utc > now` would make it the only query that skips the meeting you are
  currently in.
- **A reminder is not a block.** `EventType::Alarm` is a row type for a trigger,
  not the alarm app and not occupied time. `conflicts` and `free` ignore those
  rows; counting them would make a reminder “double-book” its own target and
  remove free time that is actually free. The separate `alarm/` frontend still
  consumes every trigger through core.
- **Hidden is display, not cancellation.** The weekly view hides sleep blocks
  so the productivity view stays clean, but the sleep/wake alarms must still
  fire. That is why `conflicts`/`free` default to visible rows while `alarms`
  defaults to all rows; `--visible`/`--include-hidden` makes either choice
  explicit.
- **A sum is not a union.** `stats.scheduled_seconds` adds event durations, so
  two overlapping events double-count. That is deliberate: it answers "how much
  event time is on the calendar", while `conflict_pairs` says how much of it
  collides. Computing a union would hide the overlap instead of reporting it.
- **A doctor must be read-only.** `diagnose` never migrates or repairs: a user
  runs it to decide what to do. The exit code is the script contract (`0` all
  ok, `1` warn/fail), and the report names the file/paths a human must look at.
  Read-only is a SQLite access mode (`SQLITE_OPEN_READONLY` + `query_only`),
  not just a promise in the command layer. Opening with create/migrate made
  `doctor` turn a missing or zero-byte file into a healthy empty calendar and
  made `export` fail on a read-only backup.
- **Options are allowlisted per verb.** "Not a query command" is not a scope.
  Every flag names the exact verbs that use it; anything else is exit `2`.
  Otherwise `export --title Z` succeeds, exports everything, and the user
  believes a filter was applied. Completion uses the same lists.
- **One completion table, three shells.** Bash/zsh/fish are generated from the
  same per-verb option table. Tests assert negative cases (`stats` has no
  `--limit`, `next` has no `--sort`, `list` has no `--days`), not just that a
  verb appears somewhere.
- **Export is not display.** `list --format csv` is a compact human table;
  `export` is a stable interchange contract with every stored field. They are
  allowed to differ, and `core/export` must not depend on `cli/format`.
  `--format` is therefore verb-scoped: display formats for views, `csv|jsonl|ics`
  for export.
- **ICS is one-way.** Export writes UTC VEVENTs, a `VALARM` for the T-offset,
  and RFC 5545 escaping/folding. Importing arbitrary ICS (timezones, recurrence,
  attendees) is a different, much larger problem; do not pretend the exporter
  is a parser.
- **Parse before write, and reject empty input.** Import validates the entire
  document before opening the database. A bad row cannot leave a half-import,
  and a truncated file cannot `--replace` a calendar with nothing. The empty
  check is deliberately an error, not "zero events".
- **Append is safe; replace is explicit.** Import defaults to fresh ids so two
  calendars can be merged. `--replace` is a restore, `--preserve-ids` keeps the
  ids notes/links will use; a collision rolls the whole transaction back.
- **JSONL import is flat on purpose.** The parser reads one object per line with
  scalar values and ignores unknown scalar keys, but rejects nested values. A
  general JSON tree is not needed to restore our own export.
- **Completion is generated, not installed.** `completion bash|zsh|fish` prints
  a static script to stdout so a fresh checkout works without a setup step. The
  verb list is duplicated by necessity; `completion_test` pins the surface.
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
- **A day is half-open.** 23:59:59 is the last named second (86,399 s after
  midnight); the day's length is **86,400 s**, and the next midnight belongs to
  the next day. Use `[start, end)` everywhere; an inclusive end silently
  duplicates a second. The 2026-10-16 note's "86,399 s + 59 µs" is the inclusive
  last instant, not a duration. The note's `[0; 8700⟩` bound is inconsistent
  with both and stays an open question.
- **UTC is the model; a timezone is a viewport.** Store and compare Unix
  seconds UTC, format local only at the edge. NTP belongs to the runtime, never
  to `core/` or to a test.
- **Performance is a requirement, not a license to guess.** SQLite is the
  measured incumbent; a JSON rewrite must beat it on the real operations (open,
  weekly range query, insert/update, file size, sync conflicts) before it is
  considered. Encryption is already in place; measure its cost before treating
  it as the bottleneck, and never remove it silently.
- **Captured is not implemented.** The notes app, the 10-hour sleep scheduler,
  G-SHOCK sync, and NTP are recorded requirements; they do not move the current
  phase.
- **Vendor what must build identically everywhere.** sqlite3 is vendored rather
  than found on the system so Linux and Android compile the same version with no
  package to install — first the plain amalgamation (Phase 0), now the
  SQLite3MultipleCiphers amalgamation that carries the encryption (Phase 3).
  Record the exact version and the download's checksum beside the vendored
  files (`libs/thirdparty/sqlite3mc/README.md`).
- **A renderer is not a place to decide things.** Every visual choice — colour,
  border, which label fits, truncation, hatch, now-line, header text — is made
  in the pure `Scene` and asserted by `scene_test`. What is left in the
  `vk_canvas` adapter is a switch. That is the only way the view could be
  trusted on a machine where it could not be drawn, and it kept the untestable
  half small enough to read.
- **A local day is not 86,400 seconds.** The model is UTC, but the view's columns
  are local days; a spring-forward day is 23 hours and a fall-back one is 25.
  Passing `dayStart` alone and assuming a day length put every event after the
  transition in the wrong place — twice a year, only in some zones, which is
  exactly the bug that survives a release. Pass `[startUtc, endUtc)`.
- **Overlaps are a day-level question.** `dayTimeRect` can only ever return the
  full column width, so two events at the same time paint over each other. Ask
  the whole day (`layoutDay`): cluster intervals, give each cluster as many
  slots as its maximum concurrency, and keep `boxes` parallel to the input so
  the scene never needs an id map. Touching endpoints are sequential, not
  concurrent, because the range is half-open.
- **A week's number belongs to the week.** A week that contains 1 January is
  week 1 of the NEW year even when its Sunday is in December. Labelling from the
  Sunday alone produced "Week 53 · Dec 2026" and then jumped to week 2.
- **A viewer can be read-only and still live.** `PRAGMA data_version` changes
  when ANOTHER connection commits (not this one), so a one-second poll gives an
  open window an AI's edit with no socket, no watcher and no write access. That
  is the shape that lets the GUI be safe before it is editable.
- **A reminder row is not a block in the view either.** `EventType::Alarm` has no
  duration and is anchored to another event; drawing it would invent a meeting
  at the anchor's time. The scene skips those rows rather than the query,
  because `list`/`export` must still see them.
- **Pixels are a build product.** Shaders are compiled next to the build tree and
  copied beside the executable with the fonts, because `FileAssetReader` looks
  there. A build tree that cannot run is a build tree whose UI is never seen —
  and a headless capture (`VK_EXT_headless_surface`) is enough to see it.

## 6. Glossary

- **`arc_fs`** — `archive_engine`'s storage module: paths, layout, non-throwing
  walk, `listDir`, volume access mode. We consume only this module.
- **`pe_holiday_calc`** — the first-party Peru-holiday library
  (`libs/firstparty/pe-holiday-calc`). Core wraps its registry behind
  `klend_ario/holidays.hh` so callers see `arc::Result`, not `std::optional`.
- **`app_shell`** — the Host/AppView seam and per-app paths/metrics, lifted out
  of Matrix_Player. The GUI's window/host half.
- **`vk_canvas`** — the Vulkan 2D canvas + text engine. The GUI renderer.
- **Scene** — `gui/src/scene`: the weekly view as a list of draw ops and hit
  targets, built from a `WeekModel` and a `TextMetrics` seam. Pure, so
  `scene_test` asserts the whole visual contract without a GPU.
- **Capture** — `klend_ario_capture`: renders fixture weeks headlessly
  (`VK_EXT_headless_surface`) to PNG. Dev tooling, Debug-only.
- **`merged.md`** — the product plan. **`git_wrapper`** — the commit tool at the
  repo root (see `AGENTS.md`).
- **NTP** — network time source planned for the runtime. It is never used by a
  test and never reaches `core/`.
- **Sleep window** — the auto-generated `sleep` block, default **10 h**, dynamic
  (hidden/shown automatically by the scheduler).
- **G-SHOCK sync** — planned export of sleep/wake alarms to a Casio watch over
  its BLE protocol. A research item, not a dependency.
- **Notes companion** — a future notes surface whose notes reference calendar
  events by `events.id`; it does not copy event data.

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
- **A relative `--db` has no parent component.** `arc::fs::parent("klend_ario.db")`
  is empty, and listing `""` fails; the first version silently found no conflict
  copies for every relative path. `syncConflictCopies()` now treats an empty
  parent as `"."`. Tests run with relative temp names, so this was found by the
  doctor e2e, not by the pure matcher.

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
- **The surface is a prompt.** `initialize.instructions`, tool `title`s,
  descriptions and `annotations` are what a model reads before touching the
  calendar; they must stay accurate, concrete and short. OpenCode consumes
  `instructions` and the annotations.
- **Inputs are strict.** No coercion: unknown arguments, string ids, string
  booleans, non-integral numbers and `end < start` are errors with actionable
  messages. A silently wrong calendar is worse than a rejected call
  (`MEMORY.md` §5, the strict-parser lesson).
- **`list_events` is an `EventQuery`, not a special path.** It carries the same
  search/type/visibility/sort/limit/offset as the CLI, defaults to today 00:00
  UTC + 7 days, and returns at most 100 rows (max 500) because result size is
  context. A frontend never post-filters.
- **Time is core.** `parseInstantExpr` / `parseDurationExpr` / `startOfDayUtc`
  live in `core/timeexpr`; `mcp/` writes no date math. The server accepts
  `today`, `tomorrow`, `week`, `+90m`, date-only, and durations (`90m`, `2h`).
- **A read-only tier is a SQLite mode.** `--permissions read` opens with
  `openReadOnly`/`openEncryptedReadOnly` and `tools/list` advertises only
  list/get. A tool the server would refuse is not part of the surface. A
  missing DB is a startup error, never an empty calendar.
- **Results are text + structured.** Compact JSON stays in
  `content[0].text` (OpenCode feeds that to the model); `structuredContent`
  carries the host-renderable value. The 2025-era field is an OBJECT, so
  `list_events` keeps the bare array in text and wraps it as `{"events": []}`
  for structured content. `outputSchema` is deferred: it is paid for in every
  `tools/list` prompt and no host validates it yet.
- **Delete is idempotent.** `removeEvent` succeeds when nothing matched, the
  CLI `rm` relies on it, and `delete_event` keeps that contract; the
  instructions tell the model to `get_event` first.
- **OpenCode is wired from the repo.** `opencode.jsonc` registers
  `klend_ario` as a local MCP command with all tiers on. Restrict with
  `--permissions read` (or `read,write`) in the command.
- **Open failures are diagnosed like the CLI's.** A no-key open of an encrypted
  database gets the `--key-file` / `$KLEND_ARIO_KEY` hint; the read-only hint
  fires only when no key was supplied, so a wrong key is never blamed on
  read-only mode.
- **Protocol policy: advertise what we implement.** The server says
  `2025-06-18`. MCP `2026-07-28` (`server/discover`, `resultType`, per-request
  `_meta`) is a future adoption with no benefit while OpenCode defaults to the
  2025 handshake; it is not a silent default.

## 9. Alarms (Phase 6)

- **Deciding is pure; delivering is a seam.** WHEN an alarm fires is
  `klend_ario::alarmsIn` / `dueAlarms` / `nextAlarm` in core — a function of the
  events and a time window, asserted in `alarm_test`. HOW it is delivered is a
  `Notifier` installed by the `alarm/` frontend (libnotify on Linux), the same
  installed-backend shape as `arc_fs`'s media index.
- **Two triggers per event** (`merged.md` §6): T-offset first, then T=0, both
  from `event.hh`'s `alarmTriggersUtc`.
- **Hidden rows still alarm.** `hidden` hides a block in the weekly view; it
  does not cancel a trigger. The CLI `alarms` verb therefore defaults to all
  rows, while `conflicts`/`free` default to visible ones.
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

## 10. Captured requirements (2026-10-16)

The full note is [`REQUIREMENTS.md`](REQUIREMENTS.md). What binds the code now:

- **Desktop first.** Android work stops until Phase 7 is explicitly reopened.
- **UTC and half-open days.** The model already does this (`[0, 86400)`); keep
  it, and keep the 80,000 / 81,000 / 1,000-second vectors in `event_test`.
- **Performance over speculation.** SQLite stays until a benchmark says
  otherwise; encryption stays unless a separate decision removes it.
- **Sleep is dynamic.** Default 10 hours; the block can hide/show automatically.
- **Notes link to events.** A note references `events.id`; it never duplicates
  the event.
- **G-SHOCK is research.** No protocol, Bluetooth dependency, or promise yet.
- **NTP is runtime.** No network in `core/` or in tests.

The holiday item is already done: `pe-holiday-calc` is the C++17 port of the
original Rust generator, consumed through `klend_ario/holidays.hh`.
