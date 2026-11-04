# ROADMAP — what is left to finish

**Status:** 2026-11-04 · handoff after `8731a06`
**Next action:** measure the edit path (§4.4, §17). Everything else in
Milestone 1 is functional.
**Baseline:** 37/37 CTest suites pass; `demo/klend_ario_demo.py` exits `0`;
the weekly view builds in Debug and Release; headless capture writes 6/6
scenarios at 1920×1080 and 1280×720.
**Relationship to `PLAN.md`:** this file is the forward-looking finish line.
[`essentialAgent/PLAN.md`](essentialAgent/PLAN.md) remains the detailed phase
tracker and status log. Close a milestone in both files in the same change.

**Verification rule (2026-11-04): never launch the GUI or take pointer/keyboard
focus for a check.** Use `ctest` and the headless `klend_ario_capture`; a live
window is only for a change that cannot be verified headlessly, and only after
the user agrees. Moving focus interrupts real work and is not a harmless test
step. The live `F12` path was already verified in Debug and Release at
`b947d08`; do not re-run it for unrelated changes.

---

## 1. What "done" means

### Desktop 1.0 — the release that can replace the CLI-only workflow

A Linux user can install the app, create an encrypted calendar on first run, and
live in the weekly view:

- create / edit / delete every CLI-parity field from the GUI, safely alongside
  the CLI and MCP writers;
- alarms fire on time with a notification **and sound**, from a daemon that
  starts at login, with no duplicate triggers;
- the app captures its own window with `F12` in Debug and Release;
- local time is shown with UTC still visible; DST days are 23/25 hours;
- the install is user-local (no root), appears in the desktop menu, and ships
  the daemon unit, completion, fonts, shaders and icon;
- `ctest`, the demo, the install smoke test and the headless captures are green;
- the docs match the code, and `opencode.jsonc` is present and verified.

### Captured scope after 1.0 (from `REQUIREMENTS.md`)

- **1.1 Sleep** — the 10-hour dynamic sleep window, auto-generated,
  hideable/showable, alarms included.
- **1.2 Notes** — notes reference `events.id`; never copy event data; survive
  export/import.
- **1.3 Time and extras** — NTP as a runtime concern; overlay and print;
  storage benchmark only if the measured target demands it.

### Deferred / research

- **2.0 Android** — Phase 7, reopened only after desktop 1.x.
- **G-SHOCK** — a research spike first; no BLE dependency before a go/no-go.
- **Windows** — not a target. `core/` stays portable, but the GUI is
  Linux/Wayland-first and there is no Windows release promise.

The CLI is **frozen** (2026-10-17) and the MCP surface is **five tools**. Any
milestone that wants a new CLI verb or a sixth MCP tool must record that
decision first; otherwise the feature lives in the GUI or in existing fields.

---

## 2. Where the project is

### Done

- Phases 0–3: scaffold, core/storage, CLI MVP, encryption + Syncthing policy.
- CLI hardening stages 1–6: output, edits, relative time, queries,
  conflicts/free/alarms, stats/doctor, export/import, read-only modes.
- Phase 5: MCP server, five tools, permission tiers, structured content.
- Phase 6: alarm core + daemon, libnotify, log fallback (sound still open).
- Phase 4a: weekly view, pure scene/layout, DST-aware local days, RasterFont,
  demand-driven repaint, headless capture.
- 4b (2026-11-04): modal editor, text input, writable DB with revision CAS,
  local-time inverse with DST flags, `event:<id>` scene hit targets and
  selection. Create/edit/delete verified in a live Debug and Release window at
  `b947d08`; from now on, use headless capture instead.
- R8 editor time display (2026-11-04): each local start/end field shows the
  resolved `UTC YYYY-MM-DD HH:MMZ` instant underneath. The hint shares
  `resolveEditorField` with Save, including the unchanged fall-back rule; a
  malformed field shows `UTC --`. Pinned by `editor_test`/`editor_scene_test`.
- `F12` captures the live window natively in Debug and Release, including the
  modal; the headless fixture tool is unchanged.
- `opencode.jsonc` restored; the repo skill and MCP server load from the
  checkout.
- 37 suites, including the seven `pe-holiday-calc` child suites.
- Demo: 21 CLI verbs, five MCP tools, one alarm trigger, encrypted DB checks.

### Not done

- 4c overlay/print.
- 4b refinements: a new event starts at "now", not the clicked slot; `Esc`
  discards a dirty form without warning; notes is single-line; the next-alarm
  display is optional. None blocks Milestone 2.
- PipeWire sound (the named Phase 6 gap); daemon autostart/single-instance.
- Key UX: no passphrase prompt; no first-run encrypted setup; encryption is
  opt-in via file/env despite the recorded "encrypted by default" decision.
- Packaging/install: no `install()` rules, `.desktop`, icon, systemd unit,
  release artifact.
- Time zones: the editor shows the resolved UTC instant under each local
  start/end field, but the GUI has no launch-time `--timezone` yet and uses the
  system zone. The CLI is UTC-only. That part of R8/D5 is still open.
- NTP (R9), notes (R3), sleep (R5), G-SHOCK (R4), Android (Phase 7).
- Storage benchmark (R7).

---

## 3. Milestone 0 — close the loop (done 2026-11-04)

- [x] Restore `opencode.jsonc`; verify the MCP server and the `klend-ario`
      skill load from the checkout.
- [x] Fix the stale statements: suite count, GUI layout line, MEMORY ordering,
      CLI status.
- [x] Add this roadmap and link it from the README/manual.
- [x] Record decisions D1–D5 in `essentialAgent/MEMORY.md`.
- [ ] Verify the full OpenCode round-trip (create → list → update → delete)
      through the restored MCP registration.

---

## 4. Milestone 1 — Phase 4b: edit in the weekly view (functionally done 2026-11-04)

The editor, write path, revision CAS, `F12` screenshot and UTC time display are
shipped. What remains is the performance measurement and three optional
refinements; the milestone's functional gate is met, but do not mark the section
complete until the edit path is measured.

### 4.1 Core safety — done

- [x] **Optimistic concurrency.** `events.revision` is the compare-and-swap
      token; `Db::updateEvent` refuses a stale save instead of overwriting an
      MCP/CLI edit. `schema_test` covers the stale save, the blind write, and a
      real v1 file migrating to v2.
- [x] **Atomic write.** The row update and `meta.modified_utc` commit together.
- [x] **Local time → UTC.** `localTimeToUtc` returns the instant plus
      `ambiguous`/`nonexistent`; `local_time_test` pins Lima, Berlin
      spring/fall and UTC.

### 4.2 Editable scene — done

- [x] **Event hit targets.** The scene emits `event:<id>` targets and paints
      the selected box; `scene_test` asserts the geometry, the Alarm-row skip
      and the selection stroke.
- [x] **Editor chrome.** Modal panel draw ops, field boxes, focus ring, toggle,
      buttons and status line; `editor_scene_test` asserts the ids and focus.
- [x] **Selection navigation.** Click selects; second click/Enter edits; `N`
      creates; `Delete` asks twice. Week navigation is disabled while the
      modal is open.
- [ ] **Optional: create at the clicked slot.** `N` starts at the current
      instant, not the empty slot under the pointer. Later refinement, not a
      blocker and not part of the 1.0 gate.

### 4.3 Editor UI

- [x] **Pure form state.** `gui/src/editor` turns an Event into local-time
      fields and back, validates every field, and refuses/flags DST wall
      clocks. `editor_test` pins the round-trip and the invalid cases.
- [x] Fields: title, start/end, type, importance, urgency, alarm offset,
      hidden, notes (single-line in v1). Reuses `TextFieldState` and the host
      clipboard; no second text engine.
- [x] Defaults: new event starts now, one hour; title focused; `Esc` cancels.
- [ ] **Optional: dirty-state warning.** `Esc` currently discards a modified
      form without asking. Add only if the form grows enough to make that loss
      likely; not a 1.0 blocker.
- [x] Time input: the form shows the local wall clock **and** the resolved UTC
      instant under each field, from the same resolver Save uses. A nonexistent
      DST time is refused; an ambiguous one requires a second Save and names the
      earlier occurrence.
- [x] Error surface: stale edit, constraint failure, wrong key at open,
      read-only deployment.
- [x] **Not in 4b:** drag-to-move/resize, recurrence, multi-select, multiline
      notes. Recorded as later work.

### 4.4 Write path and refresh

- [x] Open writable (`Db::open` / `openEncrypted`) by default; `--read-only`
      keeps `openReadOnly`/`openEncryptedReadOnly` and the must-exist rule.
- [x] Save → reload the week → arm one frame. The editor closes on success.
- [x] External change while the editor is open: `dataVersion` reload shows a
      warning; the save carries the original revision and is refused if stale.
- [ ] **Measure the edit path** — save time (insert and CAS update over a
      throwaway DB), frames from the keypress/closing of the modal to the
      redrawn week, and whether text is re-baked. Record before/after in
      `GUI_PERFORMANCE.md`. Prefer a timed test or a headless driver; **do not
      launch a window or steal focus for this**. The screenshot path does not
      affect the idle repaint policy.
- [x] **In-app screenshot** — `F12` renders with present disabled, reads the
      frame back, writes PNG to `--screenshot-dir`/the app's data directory,
      then re-enables present. Works in Debug and Release, modal included.

### 4.5 Alarms in the window

- [x] The daemon is the only alarm owner (recorded D2). The GUI does not fire
      its own.
- [x] The selected event's alarm offset is an editable field.
- [ ] **Optional: display the next upcoming alarm** in the window. The daemon
      remains the delivery path.

**Done when:** create/edit/delete survive restart; the CLI and MCP see the
changes; a stale save is rejected; `ctest` and the demo stay green; captures are
updated; the edit path is measured. Functional work is done; the measurement is
the only open gate.

---

## 5. Milestone 2 — finish the alarm stack (M)

- [ ] **PipeWire sound.** Decide link vs spawn (`pw-play`/`paplay`), make it
      optional at build time, and keep the log fallback. Record the dependency
      decision in `MEMORY.md` §9.
- [ ] Add a sound asset (or synthesize a tone), volume, and a `--sound` /
      `--notify` matrix. One trigger = one notification + one sound.
- [ ] **Single owner / single instance.** A lock or socket so a GUI, a manual
      `--once` run and the daemon cannot double-fire.
- [ ] **Autostart.** Ship a systemd user unit (`Restart=on-failure`) and, if
      needed, an XDG autostart fallback. Document the key source.
- [ ] Notification content: title, start time, T-offset/T=0; optional action to
      open the event in the GUI (decide).
- [ ] Tests: notifier seam; engine dedupe; install smoke; a manual audio check
      on a real session (headless CI cannot prove sound).

**Done when:** a due event produces exactly one notification and one sound on a
clean login; the daemon restarts after a crash; disabling the GUI does not
disable alarms.

---

## 6. Milestone 3 — key and encryption UX (S/M)

The product says "one encrypted SQLite file", but today a user gets plaintext
unless they know `--key-file` or `$KLEND_ARIO_KEY`.

- [x] Decide D3: encrypted by default for new calendars; explicit opt-out;
      existing plaintext files stay readable.
- [ ] **CLI:** TTY prompt with echo off when an encrypted DB is opened without a
      key; keep `--key-file`/env for scripts. Never accept the passphrase on
      argv.
- [ ] **GUI:** a first-run / unlock dialog before the window opens; or a keyring
      integration if D3 chooses one (that is a new dependency decision).
- [ ] **Daemon:** a 0600 key file or systemd credential; no prompt at boot.
- [ ] **MCP:** env/file only; the host has no TTY. Keep the current hint.
- [ ] Precedence: explicit flag > env > prompt > error. Test the resolver.
- [ ] First-run flow: create → confirm → open; wrong key gives the existing
      actionable error, never a silent empty calendar.
- [ ] Tests: resolver precedence; encrypted create/open through each frontend;
      wrong/empty key paths. Prompt behavior is a manual check.

**Done when:** a new user can create an encrypted calendar from the GUI or CLI
without editing environment variables, and every frontend can open it.

---

## 7. Milestone 4 — install and operate (M)

- [ ] CMake `install()` rules: `klend_ario_cli`, `klend_ario_gui`,
      `klend_ario_mcp_server`, `klend_ario_alarmd`; fonts, shaders, icon,
      `.desktop`, completion, man page, systemd user unit.
- [ ] `scripts/build.sh` gains `install`/`package` (prefix, `DESTDIR`, version
      from `manifest.json`); produce a user-local tarball with a checksum.
- [x] Decide D4: user-local `~/.local` prefix, no root; systemd user unit plus
      XDG autostart fallback; tarball. AppImage/Flatpak out of scope unless
      asked.
- [ ] Uninstall instructions; do not delete the user's `state` database.
- [ ] Distribution decision: if binaries leave this machine, add a `LICENSE` and
      a short user guide (install, keys, sync, alarm autostart).
- [ ] Smoke test: install to a temp prefix; `--version`; `desktop-file-validate`;
      `systemd-analyze verify`; capture a week; `alarmd --once`.
- [ ] Version bump and release notes.

**Done when:** a clean Linux user can install without root, launch from the app
menu, and have alarms start at login.

---

## 8. Milestone 5 — time, data safety, performance (M)

### 8.1 Time display (R8)

- [x] Decide D5: system local zone by default; a launch-time `--timezone`
      (applied before threads start) for another zone; the GUI shows UTC
      alongside. The CLI stays UTC-only.
- [x] GUI shows **local time and UTC**. The editor draws `UTC YYYY-MM-DD HH:MMZ`
      under each local start/end field; a malformed field shows `UTC --`, never
      a stale value.
- [ ] `--timezone` is applied before threads start; `local_time` keeps the
      23/25-hour day contract.
- [x] Tests: Lima, Berlin spring/fall, ambiguous/nonexistent local times. The
      inverse is pinned by `local_time_test`; `editor_test` pins the UTC hint,
      including the unchanged fall-back field keeping its opened instant.

### 8.2 Sync and data safety

- [ ] Document the SQLite journal mode and the Syncthing copy behavior; verify a
      copy made during a write is either consistent or detected.
- [ ] Use `meta.modified_utc` at open: warn when the file is older/newer than the
      marker (the "timestamp check at open" from `merged.md` §8).
- [ ] Manual two-device Syncthing test: edit on A, edit on B, confirm the
      conflict copy is surfaced by `doctor` and never auto-merged.
- [ ] Backup/restore drill: export → import into a fresh encrypted DB; after
      notes land, include notes in the backup contract.

### 8.3 Performance gate (R6/R7)

- [ ] Define measurable SLOs for the real operations: open, weekly range query,
      insert/update, save from the GUI, frame time, RSS at 10k events.
- [ ] Build a repeatable benchmark harness; record SQLite (plaintext and
      encrypted) numbers.
- [ ] JSON is a **candidate only**: prototype and compare on the same operations
      (open, range, write, file size, memory, conflict copy). No storage rewrite
      without a measured win; record the decision in `MEMORY.md`.

**Done when:** UTC is visible, the DST tests pass, a conflict is surfaced rather
than merged, and the performance numbers are written down.

---

## 9. Desktop 1.0 release gate

- [ ] `ctest` 100% (including the holiday child suites).
- [ ] Demo exits 0 on Debug and Release; `--pause`, `--keep`, `--release`.
- [ ] Install smoke test on a clean prefix; desktop entry, icon, systemd unit.
- [ ] Headless captures reviewed at 1920×1080 and 1280×720.
- [ ] Encrypted first-run and unlock tested on each frontend.
- [ ] Alarm notification + sound + autostart tested on a real login.
- [ ] Docs: README, GUI keys/editor, install, keys, sync, alarms.
- [ ] `PLAN.md`, `MEMORY.md`, `TESTING.md`, `REQUIREMENTS.md` updated; version
      bumped; no stale "next" claims.
- [ ] Tag/release through `./git_wrapper`, submodules first.

---

## 10. Release 1.1 — sleep scheduler (M/L)

Source: R5. This is the user's daily requirement, and it feeds G-SHOCK, so it
comes before notes in this roadmap.

- [ ] Decide D6: settings surface (GUI panel vs a CLI/MCP exception), scheduler
      owner (daemon vs GUI), and override semantics.
- [ ] `meta` keys: enabled, duration (default 36,000 s = 10 h), bedtime anchor,
      auto-hide policy, last-generated marker.
- [ ] Pure scheduler: `ensureSleepWindow(now, prefs, existing)` creates or
      updates the `sleep` row and the `wake` trigger idempotently; handles local
      DST and midnight; never duplicates rows; never overwrites a user-pinned
      override without asking.
- [ ] Runner: on startup, at local midnight, and after a settings change; one
      owner; no duplicate generation when both GUI and daemon run.
- [ ] Hide/show: define whether "automatic" means hide during the day, show near
      bedtime, or follow a user setting; test the chosen rule.
- [ ] Alarms: bed and wake triggers through the existing alarm core.
- [ ] Query/export: sleep and wake rows stay normal rows; stats/conflicts keep
      their current semantics.
- [ ] Tests: idempotency, override, DST, midnight, duplicate-owner, alarm times.

**Done when:** with the feature enabled, a 10-hour sleep block appears and
updates itself, the user can override it, hiding it still fires the alarm, and
no duplicates accumulate.

---

## 11. Release 1.2 — notes companion (L)

Source: R3; open question 5 in `REQUIREMENTS.md`.

- [ ] Decide D7: one note per event vs many; plain text vs Markdown; surface
      (GUI panel, CLI verb, MCP); export shape; whether `events.notes` remains
      the short field and the companion is separate.
- [ ] Migration v3: `notes` table with `event_id` foreign key, body,
      created/updated; define `ON DELETE CASCADE` vs orphan policy. Additive,
      versioned, refused in the wrong direction as usual.
- [ ] Core CRUD + query by event; notes never copy title/time text.
- [ ] UI: a side panel or editor tied to the selected event; create/edit/delete;
      no free-floating notes.
- [ ] Export/import: notes must survive a backup. Extend the interchange format
      with a versioned notes section (CSV/JSONL) or a second file; document the
      contract. ICS has no notes reference.
- [ ] MCP: prefer extending `get_event`/`update_event` with notes (keeps five
      tools) over adding a sixth tool; record the decision.
- [ ] CLI: keep frozen unless the settings/notes surface cannot live in the GUI;
      if reopened, update completion, demo and `TESTING.md` together.
- [ ] Tests: migration, FK/cascade, round-trip, UI controller, MCP payload.

**Done when:** a note points at an event by id, survives an event rename, has a
defined delete policy, and survives export → import.

---

## 12. Release 1.3 — NTP and extras

### 12.1 NTP (R9)

- [ ] Decide D8: rely on the OS time daemon and report sync status, or add an
      in-app SNTP check. Do **not** silently set the system clock; display a
      drift warning and let the user act.
- [ ] Runtime-only; no network in `core/` or tests. A fake time source for
      tests; fail-soft when offline.
- [ ] Decide whether alarms use the corrected time or warn on skew.

### 12.2 Overlay and print (Phase 4c)

- [ ] Decide D9. The overlay needs an always-on-top surface; Wayland generally
      means layer-shell, which `app_shell` does not expose today. That is a
      child-library change first, then a pin advance here.
- [ ] Print: reuse the pure scene → SVG (text, no new dependency) → PDF/print.
      Paper sizes and margins; do not duplicate the scene logic.
- [ ] If overlay is not feasible on the target compositor, record that and drop
      it rather than faking it.

### 12.3 Small close-outs

- [ ] Left panel: `merged.md` §5.3 says "navigation only for v1"; the header
      buttons already navigate. Record that deviation or build the panel.
- [ ] MCP protocol `2026-07-28`: adopt when the host does; not a blocker.
- [ ] Output schemas for MCP remain deferred (already recorded).

---

## 13. Release 2.0 — Android (XL, gated)

Reopen Phase 7 only after desktop 1.x is complete and explicitly requested.

- [ ] Resolve the storage/sync conflict: the DB must live in app-private `state`
      for speed, but Syncthing needs access. Decide SAF/copy/companion strategy
      **before** writing the platform layer.
- [ ] NDK build of `core`, `sqlite3mc`, `pe-holiday-calc`; JNI platform layer
      pushes the private directory into `arc::fs`; request the storage permission.
- [ ] Gradle project + `app_shell` Android host; native notifications and audio;
      IME already exists in the framework.
- [ ] UI decision: native Android weekly view vs reusing vk_canvas; do not
      promise parity before the decision.
- [ ] MCP transport: local vs remote (open item 6 in `merged.md` §11).
- [ ] Sync path with Syncthing on Android.
- [ ] Tests: build + device smoke; alarms with the screen off; DB open/close;
      rotation/lifecycle.

---

## 14. Research — G-SHOCK (spike, then optional)

Source: R4; open question 6.

- [ ] Identify the exact watch model, BLE GATT services, pairing/bonding, and
      whether alarms can be written at all. Battery and background limits.
- [ ] Write a go/no-go memo in `REQUIREMENTS.md`/`MEMORY.md`.
- [ ] Only after a "go": sync the sleep/wake alarms (so this depends on 1.1),
      with a fake transport for tests and no BLE dependency before the memo.
- [ ] If "no-go": record it and stop; do not leave a half-integration.

---

## 15. Decisions

| # | Decision | State | Record |
|---|---|---|---|
| D1 | 4b edit surface: modal; text fields; optimistic save; no drag/resize | **decided 2026-11-04** | `MEMORY.md` §4 |
| D2 | Alarm owner: daemon-only; GUI may display, not deliver | **decided 2026-11-04** | `MEMORY.md` §4/§9 |
| D3 | Encryption by default; explicit opt-out; flag > env > prompt > error | **decided 2026-11-04**, implementation pending | `MEMORY.md` §7 |
| D4 | User-local install; tarball; systemd unit + XDG fallback | **decided 2026-11-04**, implementation pending | `MEMORY.md` §4 |
| D5 | System local + UTC display; launch-time `--timezone`; CLI UTC-only | **decided 2026-11-04**; editor UTC display implemented, `--timezone` pending | `MEMORY.md` §4 |
| D6 | Sleep settings surface, scheduler owner, auto-hide rule, override | open — 1.1 | `REQUIREMENTS.md` R5 |
| D7 | Notes cardinality, surface, export shape, MCP/CLI exception | open — 1.2 | `REQUIREMENTS.md` Q5 |
| D8 | NTP: OS status vs in-app SNTP; warn vs set clock | open — 1.3 | `REQUIREMENTS.md` R9 |
| D9 | Overlay feasibility (layer-shell) and print format | open — 1.3 | `merged.md` §5.4–5.5 |
| D10 | Android UI stack, MCP transport, Syncthing access to `state` | open — 2.0 | `REQUIREMENTS.md` R2 |
| D11 | G-SHOCK model/protocol; go/no-go | open — research | `REQUIREMENTS.md` R4 |
| D12 | Benchmark SLOs and the JSON gate | open — gate | `MEMORY.md` R6/R7 |

---

## 16. Continuous rules

- Every change: Debug build with no new warnings; `ctest`; a test that fails
  without the change; docs updated in the same change; `./git_wrapper` for the
  commit; submodules pushed before the parent pin.
- Every GUI change: a headless capture review; measure any repaint-path change
  before/after (`GUI_PERFORMANCE.md`). **Do not launch the GUI or move focus for
  a check.** The live window is a last resort and needs the user's agreement;
  the headless fixture captures are the default review surface.
- Every surface change: the demo still exits `0`; the MCP smoke test still
  passes; completion and `TESTING.md` stay in sync.
- Every release: install smoke, encrypted first-run, alarm sound/autostart, and
  a clean `state` DB are checked by hand.
- Keep `core/` portable: no OS headers, no frontend knowledge, no network, no
  timezone reads.

---

## 17. Start here (new session)

Last commits: `8731a06` editor UTC hint · `b947d08` 4b editor + `F12`.
Everything in the baseline is green; no known runtime regression.

Verification, headless only:

```bash
ninja -C build
ctest --test-dir build --output-on-failure
cd build/gui && ./klend_ario_capture --out /tmp/klend-shots
python3 demo/klend_ario_demo.py --no-color
```

Work order:

1. **Measure the edit path** (Milestone 1's only open gate): save time, frames
   to the redrawn week, text re-bake. Record it in `GUI_PERFORMANCE.md`; use a
   timed test or a headless driver, never a focused window.
2. **Milestone 2** — PipeWire sound, daemon single-instance/autostart.
3. **Milestone 3** — encrypted-by-default first run and key prompts.
4. **Milestone 4** — install rules, desktop entry, systemd unit.
5. **Release 1.1** — the 10-hour sleep scheduler.

Do not reopen the CLI (frozen) or add a sixth MCP tool without a recorded
decision. Do not run the live GUI for verification.
