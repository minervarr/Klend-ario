# GUI performance — the limited-resources plan

*Task spec for making the weekly view cheap on old hardware. Phases A–D were
implemented and measured on 2026-10-20; the numbers are in §2.1 and the
acceptance evidence in §6. Read [`GUI.md`](GUI.md) first for the layers and the
text pipeline, [`PLAN.md`](PLAN.md) for the phase, and [`AGENTS.md`](AGENTS.md)
for the rules. Loaded on demand, like `GUI.md` and `DEMO.md`.*

**Status:** Phases A–C landed and measured on 2026-10-20 (§2, §6). A1–A3 are
the repaint policy; A4 kept `Vsync` after a hidden-window check; B and C are in
the two framework children with their pins advanced here. Do not record a win
here or in `GUI.md` without the numbers.

---

## 1. The goal

Two different costs are being conflated when someone says "the GUI uses huge
resources":

1. **Steady-state work** — how often a frame is built and presented. The app
   controls this entirely.
2. **Fixed cost** — swapchain images, fill rate, the glyph atlas, the Vulkan
   driver's baseline. The app controls some of it; on a 4K fullscreen surface
   only `app_shell` can control the rest.

The target is: **no work when nothing visible changed**, a windowed surface on
compositors that honour one, and no chance of the UI silently running on a CPU
Vulkan device. The product behaviour (1 s data polling, read-only until 4b) does
not change.

## 2. Baseline (measured 2026-10-20)

Machine: Intel UHD (TGL GT1), Mesa 26.2.4, sway/Wayland, 1200×700 window. The
device was **not logged** before Phase B; `vulkaninfo` lists an NVIDIA RTX 3050
(NVK) first and the Intel iGPU second, so `devs[0]` was almost certainly the
dGPU all along. Treat the absolute RSS/VIRT as "this machine", not "this GPU".

| Metric | Debug | Release |
|---|---|---|
| Window / swapchain | 1200×700, 4 images | same |
| Present mode | MAILBOX (low-latency) | same |
| Idle CPU (steady sample) | 0.00 % of one core | 0.00 % |
| RSS | 36.7 MB | 37.3 MB |
| VIRT | 1.14 GB | 1.14 GB |
| Frames | 1 fps, slowest <1 ms | 1 fps, <1 ms |
| Glyph atlas | 4.0 MB, 1164 cells | same |
| Initial font bake | 10 ms | 4 ms |
| 4K headless capture | 97 MB RSS (49 MB at 1080p) | — |

How to reproduce (the app writes its log next to the executable when stdout is
not a TTY — `app_shell`'s `openLogFile()`):

```bash
cd build/gui
grep -E "Swapchain (extent|present)|Raster|render:|SLOW" klend_ario.log
grep -E "VmRSS|VmSize|Threads" /proc/$(pgrep -n klend_ario_gui)/status
```

`VIRT` is the Vulkan/Mesa address-space reservation, not RAM. **Quote RSS, not
VIRT**, when judging memory. The baseline is already light on a real GPU; the
plan exists for machines where a full-screen frame costs 100s of ms (software
Vulkan, old iGPU) or where the surface is 4K.

### 2.1 After Phase A (measured 2026-10-20, same machine, headless sway output)

| Metric | Debug, before | Debug, after | Release, after |
|---|---|---|---|
| Current week, 70 s / 135 s | 70 frames | 3 frames (2 startup + 1 now-line) | 4 frames over 135 s (2 + 2) |
| Browsing another week, 30 s | 30 frames | **0** | same code path |
| Idle CPU (steady sample) | 0.00 % | 0.00 % (0 ticks / 55 s) | 0.01 % (1 tick / 125 s) |
| RSS | 36.7 MB | 41.8 MB* | 35.3 MB, flat over 135 s* |
| Text bakes | 1 per frame | 1 at startup, 1 on resize | same |
| Swapchain | 4 images, MAILBOX | 4 images, FIFO | 4 images, FIFO |
| Windowed default | fullscreen | 1280x720 (`--fullscreen` opt-out) | same |

\*RSS is not comparable across the three runs: the "after" runs used a private
headless sway (its own buffers are outside the app), at 1280x720 for Debug and
a 1916x1056 tile for Release, and Debug binaries carry more. Compare the frame
counts and CPU, and treat RSS as "flat over the run" rather than as a delta on
the §2 baseline. The Release figure is the one to quote.

The two startup frames are the initial draw and the compositor's first
configure; the resize path re-baked text once at 1280x720 (2 ms) and the
`render:` line reports 3 frames, not 3 fps.

**Frame counting caveat:** `vk_canvas` logs `render: N fps` only when at least
one second has passed since the last such line, so the count in that line is
frames *since the previous line*, and a 1-frame/minute run logs once a minute.
That is fine for "0 while browsing" (no line at all) but do not read it as a
per-second rate.

## 3. What is actually expensive

Ranked by expected win on a weak machine, with the code that causes it:

1. **A full-scene repaint every second, unconditionally.**
   `WeekView::onTimer` ends with `pendingFrames_ = 1` (`gui/src/week_view.cc`).
   That rebuilds the scene, re-collects text inputs, re-uploads geometry and
   presents — even when nothing visible changed, and even when the shown week
   does not contain "now" (so the now-line is not in the scene at all).
2. **Text inputs are collected on every frame.**
   `drawFrame()` calls `ensureText()` before building the scene; it rebuilds the
   codepoint/size set from every event title even though it can only change when
   the week, the metrics or the glyph set changes.
3. **No local-midnight reload.** Only a *week* rollover reloads (`onTimer`);
   `isToday` is computed in `loadWeekModel` (`gui/src/week_store.cc:32`), so the
   "today" tint and day headers stay on yesterday after midnight.
4. **A forced fullscreen surface.** `LinuxHost::init()` calls
   `set_fullscreen(nullptr)` unconditionally (`app_shell/os/wayland_host.cc`).
   On GNOME/KDE a 4K monitor becomes a 4K swapchain: 4 images ≈ 132 MB and 4×
   the fill of 1080p. This is the largest fixed cost on a 4K machine.
5. **Blind physical-device selection.** `Renderer::pick_physical_device()` takes
   `devs[0]` and `create_logical_device()` hardcodes queue family 0
   (`vk_canvas/core/renderer.cc`). It never checks `VK_KHR_swapchain`, surface
   present support, or whether the device is a CPU rasterizer (lavapipe).
6. **Present policy and image count.** The app takes the framework defaults:
   `PresentPolicy::Latency` (MAILBOX) and 3 requested images. For a view drawn
   on a clock, FIFO is the framework's documented choice, and 2 images is
   enough where the compositor allows it.

## 4. The work

### Phase A — app-side, demand-driven repaint (do first)

- [x] **A1. Add the pure refresh decision.**
  New `gui/src/refresh.{hh,cc}` in `klend_ario_view` (no GUI includes); add
  `src/refresh.cc` to the library sources in `gui/CMakeLists.txt`:

  ```cpp
  enum class Refresh { None, Redraw, Reload };
  struct RefreshInputs {
      long long nowUtc;
      long long drawnNowUtc;    // the now the last built scene used
      long long weekStartUtc;   // week_.days[0].startUtc
      long long weekEndUtc;     // week_.days[6].endUtc
      bool shownDayIsToday;     // a loaded isToday day still contains now
      bool weekHadToday;        // any loaded day is flagged today (may be stale)
      bool dataChanged;         // Db::dataVersion() moved
      bool followingNow;
  };
  Refresh refreshDecision(const RefreshInputs &in);
  ```

  Rules, in order:

  | Condition | Result |
  |---|---|
  | `dataChanged` | `Reload` |
  | now outside the shown week while `followingNow` | `Reload` (the view moves the anchor to now) |
  | now outside the shown week, browsing, and a loaded day is still flagged `isToday` | `Reload`, anchor unchanged (the week rolled over while on screen — one frame clears the stale now-line/tint) |
  | now inside the shown week, but no loaded day is `isToday` | `Reload` (local midnight) |
  | now outside the shown week, browsing, nothing flagged | `None` |
  | `nowUtc < drawnNowUtc` (clock went backwards) | `Redraw` |
  | `nowUtc - drawnNowUtc >= 60` | `Redraw` |
  | otherwise | `None` |

  The shipping struct carries **two** today flags, because they answer different
  questions: `shownDayIsToday` (a flagged day still contains now — the midnight
  test) and `weekHadToday` (any flagged day — the rollover cleanup). A single
  flag made the "browse last week after midnight" case fall through to the
  minute rule and repaint once a minute, which §6 forbids.

  The week interval is **half-open**: `now == weekEndUtc` is not in the shown
  week. `kNowLineRefreshSeconds = 60` with a comment giving the pixel math: at
  700–2160 px of body height one now-line pixel is roughly 300–1000 s, so 60 s
  is still sub-pixel and keeps the line responsive if a clock label is ever
  drawn. The timer stays at 1 s: it is a **poll**, not a paint.

- [x] **A2. Wire it into `WeekView`.**
  - `onTimer`: compute `dataChanged` from `Db::dataVersion()`; compute the shown
    week bounds from `week_.days`; compute `shownDayIsToday`; call
    `refreshDecision`.
  - `Reload`: if now left the shown week while `followingNow_`, set
    `anchorUtc_ = nowUtc_` first, then `reload()` (which already requests a
    frame). `Redraw`: `pendingFrames_ = 1`. `None`: do nothing.
  - **Delete** the unconditional `pendingFrames_ = 1` at the end of `onTimer`.
  - `drawFrame`: record `drawnNowUtc_ = nowUtc_` when the scene is built.
  - `onTimer` still updates `nowUtc_` every second; a later reload or redraw
    uses the latest value.

  **Shipped note:** `onHostResized()` also calls `renderer_->notifyResized()`.
  Without it the frame right after a resize is built at the OLD extent, because
  `recreate_swapchain()` only runs inside `draw()` — at 1 fps that corrected
  itself within a second; at one frame per minute it would stay stretched.
  `drawFrame` keeps a second guard: if the extent changed during `draw()`,
  `run()` arms one more frame (a `pendingFrames_ = 1` set inside `drawFrame` is
  cleared by `run()` on return).

- [x] **A3. Text dirty flag.**
  Add `textDirty_ = true` on `reload()`, `onHostResized()`,
  `onHostLayoutInvalidated()` and when `font_->bakeMisses()` added cells.
  `drawFrame` calls `ensureText()` only when `textDirty_` (then clears it).
  A miss still re-arms a frame through the existing `hasMisses()` path.

  **Shipped note:** the flag is keyed to the renderer **extent**, not only to
  the callbacks (`ensureText()` records `textWidth_/textHeight_`; `drawFrame()`
  sets `textDirty_` when they differ). That covers the same resize ordering as
  A2 and the initial configure. `bakeMisses()` does not arm it: `text_inputs`
  always collects printable ASCII, so a miss is a cell at a size or codepoint
  the collector already requested.

- [x] **A4. Trim the fixed frame cost.**
  Construct the renderer with 2 images and FIFO:

  ```cpp
  Renderer(host_->surfaceProvider(), host_->assetReader(),
           /*desiredSwapchainImages=*/2, OutputTarget::SdrSrgb,
           PresentPolicy::Vsync);
  ```

  The surface's `minImageCount` may clamp this upward (sway advertises 4); that
  is fine and expected. Keep the 1 s DB poll: it costs microseconds and is how
  CLI/MCP edits arrive.

  **Measured:** requested 2, got **4** — sway's surface minimum is 4, so the
  old request of 3 also produced 4 and the image count (and RSS) did not move.
  FIFO did change the present mode (MAILBOX → FIFO) and nothing else.
  Hidden-window check: a reload frame presented while the window sat on an
  inactive workspace and the poll kept running, so the stall that FIFO can
  cause on an image the compositor never releases did not occur here. On a
  target that blocks, go back to `Latency` + 3 (MAILBOX wants ≥3 images) rather
  than keeping FIFO with 2.

- [x] **A5. Tests.**
  New `gui/tests/refresh_test.cc`, registered in `gui/CMakeLists.txt` with
  `klend_ario_add_test` (Debug-only, like every other suite). Cases: data
  change, week rollover, midnight rollover, 59 s vs 60 s, backwards clock,
  browsing, `now == weekEnd` (half-open). `scene_test` must keep passing
  unchanged; `text_inputs_test` covers the codepoint set.

### Phase B — `vk_canvas`: pick the right device and queue

- [x] **B1. Candidate selection in `pick_physical_device`.**
  For each physical device:
  - require `VK_KHR_swapchain` in its extension list;
  - find a queue family with `VK_QUEUE_GRAPHICS_BIT` and
    `vkGetPhysicalDeviceSurfaceSupportKHR(dev, family, surface_) == VK_TRUE`;
  - collect `(device, family, type)`.
  Prefer a non-CPU candidate; among those keep **enumeration order** (do not
  silently switch a laptop to its dGPU — battery). Fall back to today's
  `devs[0]` + family 0 with a warning if no candidate qualifies, so an existing
  consumer does not regress.
- [x] **B2. Use the chosen family.** Store `queue_family_` and use it in
  `create_logical_device()` and `vkGetDeviceQueue()` instead of the literal 0.
- [x] **B3. Say what was chosen.** Log
  `Device: <name> (<type>)[ [SOFTWARE]]`; expose `deviceName()`,
  `deviceType()` and `isSoftwareDevice()` on `Renderer`.
- [x] **B4. Child-repo workflow.** This is a change in
  `libs/firstparty/framework/Vk_Canvas_Lb_LAW`, shared with Matrix_Player. Make
  it there, build it through the parent's `-DKLEND_BUILD_GUI=ON` build, commit
  and push with the child's `git_wrapper`, then advance the parent pin. Write
  the reason in the child's docs, not only here.
- [x] **B5. Verify.** `vulkaninfo --summary` lists the expected device; the GUI
  log names the same one; on a machine with lavapipe installed, the real GPU is
  chosen and the log says so. No feature or extension request changes on
  Android.

  **Verified 2026-10-20.** The log now prints
  `Device: NVIDIA GeForce RTX 3050 Laptop GPU (NVK GA107) (discrete), queue
  family 0` — which is what `devs[0]` already was, so this machine's behaviour
  is unchanged and only its *visibility* is new (`vulkaninfo` lists that device
  first, the Intel UHD second). Forcing
  `VK_ICD_FILENAMES=…/intel_icd.json` selects the iGPU and says `integrated`.
  No lavapipe ICD is installed here, so the `[SOFTWARE]` suffix was not seen
  live; the candidate loop is what would produce it, and the fallback path
  (no usable device) logs its own warning.

### Phase C — `app_shell`: opt-in windowed mode

- [x] **C1. Add the option, default unchanged.**
  In `app_shell/host.hh`:

  ```cpp
  struct WindowOptions {
      int width = 1200;
      int height = 700;
      bool fullscreen = true;   // today's behaviour; opt in to change it
  };
  // Call before init(). A host that cannot honour it keeps its historical mode.
  virtual void setWindowOptions(const WindowOptions &) {}
  ```

- [x] **C2. Implement on Wayland.** `LinuxHost` stores the options, builds
  `WaylandWindow` with the requested logical size, and calls
  `set_fullscreen(nullptr)` only when `fullscreen` is true. `unset_fullscreen()`
  already exists for a runtime toggle if one is ever added.
- [x] **C3. Other backends.** Windows: honour the size/mode (not built here;
  keep it compiling and defaulted). Android: ignore it — a phone window is
  the activity's surface.
- [x] **C4. App opts in.** `WeekView::create` requests 1280×720 windowed before
  `init()`. The weekly view is designed 16:9; 1280×720 is the cheapest clean
  surface. `--fullscreen` on `klend_ario_gui` is the one-line way back.
- [x] **C5. Verify.** The log's `Swapchain extent` matches the window, not the
  monitor, on a compositor that honours windows. On a tiling compositor (sway)
  the tile size is the compositor's decision — Wayland has no client-side
  "float at this size" request. Document that; do not call it a bug.

  **Verified 2026-10-20** on a headless sway output at 1920x1080: tiled, the
  app got 1916x1056 (the compositor's tile); `swaymsg floating enable` re-sized
  it to the requested **1280x720**; `--fullscreen` gave 1920x1080. The 1280x720
  is therefore a request the compositor honoured once the window was free to
  choose, exactly as §1 describes.

### Phase D — prove it

- [x] **D1.** Build Debug and Release; `ctest --test-dir build --output-on-failure`.
- [x] **D2.** Run the headless captures and check they are visually unchanged
  (the §11 colour-crop check in `GUI.md` still shows hundreds of edge colours).
- [x] **D3.** Live run for 3+ minutes: no `render:` lines while browsing another
  week; at most one per minute on the current week; CPU and RSS sampled from
  `/proc`. Record Debug and Release, before and after, on this machine and on
  the target machine if it is reachable.
- [x] **D4.** Update `GUI.md` §9 (the timer is a poll) and §15 (decision log with
  the numbers), and tick this file. Add a line to `PLAN.md` Phase 4. Do not
  claim a win that §2 cannot reproduce.

  **D1:** Debug and Release both build; `ctest` is 34/34 (the new suite is
  `refresh_test`). **D2:** four captures written, and the §11 crop still shows
  200 edge colours (coverage text, not the old 1-bit path). **D3:** see §2.1 —
  browsing 30 s = 0 frames, current week 70 s = 3 frames, CPU 0 ticks, RSS flat
  over the run. **D4:** `GUI.md` §9/§15 and `PLAN.md` Phase 4 were updated in
  this change.

## 5. Decisions pinned by this plan

- **The 1 s timer is a poll, not a paint.** Data polling stays at 1 s because
  that is the product's "an AI edit appears live" contract; repainting does not.
- **60 s now-line cadence.** Sub-pixel at every realistic window height, and
  60× less work than today. Raise it only with a measurement.
- **`Vsync` + 2 images.** The framework's own comment says FIFO is for content
  presented on a clock; `minImageCount` may clamp the count. Measured here it
  clamped to 4, and FIFO was kept only after a hidden-window present completed
  and the poll continued. If a target ever hangs on a hidden surface, revert to
  `Latency` + 3 images instead of keeping FIFO with 2.
- **Non-CPU device preference only.** Never silently move a laptop to a dGPU.
  Enumeration order is the tie-break, which is why the device this machine
  already used (the NVIDIA dGPU) is still the one chosen — the change makes the
  choice visible, not different.
- **Windowed is opt-in at the shell.** The default stays fullscreen so existing
  consumers do not change behaviour.
- **Repaint policy is app policy.** Do not move it into `vk_canvas`; the
  library already exposes the knobs (`PresentPolicy`, image count,
  `swapchainImageCount()`).

## 6. Acceptance

- Browsing another week: **0 frames**. Current week: **≤1 frame/minute**.
  *Measured 2026-10-20: 0 in 30 s; 3 frames in 70 s including the two startup
  frames.*
- Idle CPU stays ~0 on a real GPU; on software Vulkan it drops by the frame
  ratio (60× before any other change). *Measured 0 CPU ticks over 55 s; the
  software path was not available on this machine — no lavapipe ICD.*
- RSS does not grow; it may drop by one swapchain image where the surface
  minimum allows, and by ~120 MB on a 4K machine that moves to a 720p window.
  *Here the surface clamps to 4 images, so RSS is unchanged and flat; the 4K
  saving only materialises where windowed mode is honoured.*
- No visual change at fixed scene inputs; captures and `scene_test` agree.
  *Four captures, 200 edge colours in the text crop; `scene_test` unchanged.*
- The log names the chosen GPU; a CPU device is never chosen while a capable
  one exists. *`Device: … (discrete), queue family 0` on this machine; the CPU
  preference is the candidate loop's only device-type test.*

## 7. Non-goals

- Clear-colour rendering instead of the full-window background quad — one
  full-screen blend, negligible here.
- Render-scale/upscale (helps software Vulkan, blurs text, large API).
- Pipeline cache, smaller atlas, freeing the CPU atlas — startup and ~4 MB,
  not the steady-state problem.
- Reducing `VIRT` — that is the driver's address space, not RAM.
- Android/JNI work — Phase 7.
- Editing `libs/firstparty/**` from the parent without the child-first,
  push-then-pin workflow in `AGENTS.md` §6.

## 8. Order

1. **A** (parent, `gui/`) — the 60× steady-state win; measure it alone first.
2. **B** (`vk_canvas` child) — correctness/diagnostics; pin advances in the
   parent.
3. **C** (`App_shell` child + the app's one option call) — the 4K fixed-cost
   win; pin advances in the parent.
4. **D** — numbers in `GUI.md`/`PLAN.md` and this file.

Each child change is committed and pushed **inside the child** with its
`git_wrapper`; only then does the parent pin move. The parent commits with
`./git_wrapper`, which pushes submodules before the parent.
