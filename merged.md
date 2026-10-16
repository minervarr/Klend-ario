# Openseek — Final Plan (v3)

## 0. One-line vision

A privacy-first, AI-native personal calendar: one encrypted SQLite file, synced via Syncthing, queried by an MCP server, rendered as a weekly 16:9 view, cross-platform (Linux + Android), written in **native C++**.

---

## 1. Locked decisions

| # | Decision |
|---|---|
| Storage | **SQLite**, single file. |
| Encryption | **SQLite3MultipleCiphers** (open source, C++, page-level AEAD). |
| Event storage | **Individual rows** (one row per event) — MCP-queryable. |
| Sync | **Syncthing**, single file. |
| MCP | **Read/write from day one**, minimal tool surface, permission tiers. |
| AI client | **OpenCode** via local MCP server. |
| Priority colors | **Violet (low) → Red (high)**. |
| Urgency channel | **Border thickness** (importance = hue, urgency = border). |
| Alarm stack | **libnotify + PipeWire** (Linux), **native** (Android). |
| MVP | **Weekly view + sync**. |
| Stack | **C++, all native.** |
| Margins | 720p and below → **no margins**. FHD and up → **1cm-equivalent margins** on a 16cm×9cm baseline. |
| Week starts | **Sunday.** |
| Sleep hours | **24h format**, configurable, **hideable**. |
| Alarm default | Two triggers: **T−offset** (e.g. −10 min) and **T=0**. |
| nota1 bullets | **Directives for AI agents**, not product features. |

---

## 2. Architecture

```
┌──────────────────────────────────────────────────┐
│  UI Layer        Weekly view · Overlay · Print   │
├──────────────────────────────────────────────────┤
│  Alarm Engine    libnotify + PipeWire / Android  │
├──────────────────────────────────────────────────┤
│  Calendar Core   Events · Priority · Sleep       │
├──────────────────────────────────────────────────┤
│  Storage         SQLite + SQLite3MultipleCiphers │
├──────────────────────────────────────────────────┤
│  Sync            Syncthing (single file)         │
├──────────────────────────────────────────────────┤
│  Integration     MCP Server  →  OpenCode / AI    │
└──────────────────────────────────────────────────┘
```

All layers in **C++**. No framework. Platform abstractions only where the OS demands it (notifications, audio, filesystem paths).

---

## 3. Data model (SQLite schema, draft)

**Table `events`**
```
id            INTEGER PRIMARY KEY
title         TEXT NOT NULL
start_utc     INTEGER NOT NULL      -- Unix epoch, UTC
end_utc       INTEGER NOT NULL
type          TEXT NOT NULL         -- 'event' | 'sleep' | 'wake' | 'alarm'
importance    INTEGER NOT NULL      -- 0..100, maps to hue (violet→red)
urgency       INTEGER NOT NULL      -- 0..100, maps to border thickness
alarm_offset  INTEGER DEFAULT 0     -- seconds before start
hidden        INTEGER DEFAULT 0     -- 1 = hidden in weekly view (sleep blocks)
notes         TEXT
created_utc   INTEGER NOT NULL
updated_utc   INTEGER NOT NULL
```

**Table `meta`** — schema version, user prefs (sleep window, week start, margins override), color spectrum anchors.

**Table `alarms`** (optional, later) — derived or independent alarms; kept separate so the alarm app can evolve independently.

**Indices:** `(start_utc, end_utc)` for range queries, `(type)` for filtering.

---

## 4. Priority & urgency model

- **Importance → hue.** Linear interpolation across the visible spectrum, **violet = 0 → red = 100**. Anchors stored in `meta` so you can tune them later without a schema change.
- **Urgency → border thickness.** Thin (0) → thick (100). Same hue, no second color channel.
- **Eisenhower semantics:**
  - Q1 (important + urgent): red hue, thick border.
  - Q2 (important, not urgent): red hue, thin border.
  - Q3 (urgent, not important): violet hue, thick border.
  - Q4 (neither): violet hue, thin border.

Color and border are **independent axes**, which is the whole point.

---

## 5. UI specification

### 5.1 Frame & margins
- Design baseline: **16cm × 9cm**, 1cm margins → inner content area 14cm × 7cm.
- **Scale factor** = screen width / 16cm-equivalent.
- **< FHD** (720p class): margins suppressed, content fills screen edge-to-edge.
- **≥ FHD**: margins drawn proportionally to the 1cm baseline.
- All UI metrics expressed in **baseline units**, converted at render time.

### 5.2 Weekly view
- **Week starts Sunday.**
- Day headers: `S U · M · TU · W · TH · F · SA`.
- **No left hour ruler.** Each day is a **mini-agenda column**.
- Events = rectangles:
  - Width = column width (fixed).
  - Height ∝ duration.
  - Fill = importance hue.
  - Border thickness = urgency.
  - Start-time label above each rectangle.
- After the last event of the day: **hatched rectangle** (45° lines) = "nothing else today."
- **Sleep blocks** render like any event but can be **hidden** via the `hidden` flag → the view then shows only productive hours.
- **Week counter** (ISO-style week + year) shown top of view.

### 5.3 Left panel
- **Navigation only** for v1. Filters, organizers, mini-month come later.

### 5.4 Overlay
- Key event info drawn **over any application**, corner-anchored, minimal. Post-MVP.

### 5.5 Print
- High-quality weekly export. Post-MVP.

### 5.6 Responsive rules recap
| Resolution | Margins | Notes |
|---|---|---|
| ≤ 720p | None | Full-bleed. |
| 1080p | 1cm-equivalent | Standard. |
| 4K+ | 1cm-equivalent | Target quality. |

---

## 6. Alarms

- **Two default triggers** per event: **T−offset** and **T=0**.
- Offset configurable at creation (e.g. 10 min).
- **Sleep / wake / sleep-duration** computed and alerted.
- Linux: **libnotify** for notifications, **PipeWire** for sound.
- Android: native notification + audio APIs.
- Alarm engine runs as a **separate module** polling the calendar; it can be shipped as its own app later (per your note: "alarm app is a todo later").

---

## 7. MCP integration

- **Local MCP server**, written in C++.
- **Read/write from day one.**
- **Minimal tool surface** — start with:
  - `list_events(range)`
  - `get_event(id)`
  - `create_event(...)`
  - `update_event(id, ...)`
  - `delete_event(id)`
- **Permission tiers** (read / write / destructive) implemented as a config flag, even if all are enabled now. This avoids a refactor later.
- **OpenCode** connects via the `mcp` config block (local command type).
- Context-token discipline: keep tools few and payloads small. Do not expose 30+ tools.

---

## 8. Sync

- **Syncthing** syncs the single encrypted SQLite file.
- No server component. No cloud.
- Conflict handling: SQLite + Syncthing can conflict on simultaneous writes; the plan is **last-writer-wins with a timestamp check at open**, and a documented policy for the rare conflict case. (This is the one area I'd flag for a dedicated design pass before implementation.)

---

## 9. Roadmap

| Phase | Deliverable | Status |
|---|---|---|
| 1 | Calendar Core + SQLite schema | **Next.** |
| 2 | Encryption + Syncthing sync | |
| 3 | Weekly UI (16:9, responsive, day columns) | |
| 4 | MCP server (read/write, minimal tools, tiers) | |
| 5 | OpenCode integration & AI round-trip test | |
| 6 | Alarm engine (libnotify + PipeWire / Android) | |
| 7 | Overlay + high-res print | |
| 8 | Android client | |
| 9 | Tests over time | |

**MVP = Phases 1–3.**

---

## 10. Agent directives (from nota1, kept separate from the product)

These are instructions to the AI agents building Openseek, not features:

- **AGENTS → Sumarse**: new agents join the project.
- **Skill set**: maintain a defined skill inventory per agent.
- **New commands**: add commands as the workflow grows.
- **Custom agents**: specialized agents per subsystem.
- **Do Tests overtime**: continuous testing, not one-shot.

---

## 11. Open items (explicitly deferred, not blocking)

1. Exact **spectrum anchor values** in `meta` (tune later; default violet→red linear).
2. **Sync conflict policy** details — needs a short design pass before Phase 2 ships.
3. **Key management** for SQLite3MultipleCiphers (passphrase vs. keyfile) — decide at Phase 2.
4. **Left panel** future content (filters/organizers) — post-MVP.
5. **Alarm app** as a separate binary — post-MVP.
6. **Android MCP transport** — local vs. remote — post-MVP.

None of these block starting Phase 1.

---

## 12. What to do next

Start **Phase 1**: define the SQLite schema in a `schema.sql` file, write the C++ storage layer, and stand up a minimal CLI that can create, list, and delete events. That gives you a testable core before any UI exists.

If you want, I can draft the **actual `schema.sql`** and a **minimal C++ storage interface header** as the first concrete artifacts of the project. Say the word and I'll produce them.
