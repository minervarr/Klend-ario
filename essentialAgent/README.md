# essentialAgent — everything an agent needs

> Point an agent at this folder and it has the whole picture: what the project
> is, why it exists, the rules, the plan, and how it is proven to work.
> **Start here.**

This folder is the single source of truth for anyone — human or AI — writing
code in **Klend-ario**. None of it ships to a user. It is the operating manual.

## Read in this order

| # | File | What it answers |
|---|------|-----------------|
| 1 | [`MEMORY.md`](MEMORY.md) | What is this? Why does it exist? What is already decided? |
| 2 | [`REQUIREMENTS.md`](REQUIREMENTS.md) | The 2026-10-16 source note, the normalized requirements, and what is still open. |
| 3 | [`AGENTS.md`](AGENTS.md) | The rules: conventions, invariants, commands, definition of done. |
| 4 | [`PLAN.md`](PLAN.md) | Phases, the exact scaffold manifest, and current status. |
| 5 | [`TESTING.md`](TESTING.md) | How tests are written, registered, and run. |
| 6 | [`GUI.md`](GUI.md) | The weekly view: layers, the RasterFont text pipeline, sizing, capture, traps. |
| 7 | [`GUI_PERFORMANCE.md`](GUI_PERFORMANCE.md) | The limited-resources plan: measured baseline, demand-driven repaint, device picking, windowed mode. |
| 8 | [`DEMO.md`](DEMO.md) | The permanent showcase: clean-run contract, section list, verification. |
| 9 | [`skills/klend-ario/SKILL.md`](skills/klend-ario/SKILL.md) | The repeatable build/test/feature workflow. |

`GUI.md`, `GUI_PERFORMANCE.md` and `DEMO.md` are task specs, not daily rules:
they are linked from `AGENTS.md` and loaded on demand rather than in every
session.

Product background (not agent rules) is the repo's [`../merged.md`](../merged.md):
*"Openseek — Final Plan (v3)"*, the calendar design this repository implements.

## Reference material (sibling repos)

This project is built to match conventions already proven in three sibling
repositories on this machine. Read them freely; **do not modify them** — they
are shared, and changes belong in their own repos first (`MEMORY.md` §5).

| Path | What to take from it |
|---|---|
| `/home/nerio/code/minervarr/Klend-ario/libs/firstparty/archive_engine` | The storage library we consume (`arc_fs`, `arc_util`). Its `CLAUDE.md`, and `core/fs/include/arc/fs/*.hh`, are the model for how we document *why*. |
| `/home/nerio/code/minervarr/Matrix_Player` | The cross-platform file layout (`core/` + `gui/` + `android/`), `manifest.json`, and the assert+test conventions. Its `CLAUDE.md` and `core/CMakeLists.txt` are the templates. |
| `/home/nerio/code/minervarr/git_Wrapper` | The stdlib-only Python **zipapp** build tool (`scripts/`) we port for `scripts/` here. Its `docs/USAGE.md` explains the shape. |
| `libs/firstparty/framework/App_shell` | The Host/AppView seam + per-app paths/metrics. Phase 4 (GUI) only. |
| `libs/firstparty/framework/Vk_Canvas_Lb_LAW` | The Vulkan 2D canvas + text renderer. Phase 4 (GUI) only. |
| `libs/firstparty/pe-holiday-calc` | First-party C++17 Peru holiday library, pinned as a submodule (Codeberg). It is the completed port of the original Rust generator (the first item in `REQUIREMENTS.md`). Core links `pe_holiday_calc` and wraps it in `core/src/holidays.cc` behind `core/include/klend_ario/holidays.hh`; `core/tests/holidays_test.cc` covers that boundary. Its own `essentialAgent/` governs changes inside it — make library changes there first, then advance the parent pin. |

## How this folder is wired into OpenCode

- [`../AGENTS.md`](../AGENTS.md) (repo root) is loaded automatically and points
  here. It carries the top non-negotiables so they apply even if nothing else
  loads.
- [`../opencode.jsonc`](../opencode.jsonc) lists this folder's files under
  `instructions` (`AGENTS.md`, `MEMORY.md`, `REQUIREMENTS.md`, `TESTING.md`), so
  they load every session, and registers `essentialAgent/skills` as a skill
  source.
- The workflow skill is also reachable by typing `@klend-ario`.

## Rule for maintainers

When a decision is made, or an invariant is discovered the hard way, **write it
here in the same change**. These files *are* the memory. A decision that is not
written down will be made again — differently.
