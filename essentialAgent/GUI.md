# GUI — the weekly view: decisions, seams, and traps

*Read this before changing `gui/`. It is the memory of why the GUI is shaped the
way it is and which framework paths it uses. [`PLAN.md`](PLAN.md) says what is
next; [`AGENTS.md`](AGENTS.md) says the rules.*

This file is loaded on demand (like `DEMO.md`), not in every session: it is
needed when you touch the GUI, not when you touch `core/` or the CLI.

---

## 1. Status

Phase 4a (2026-10-17): the read-only weekly view exists — window, scene,
headless capture. Phase 4b (next) adds editing. Until then the GUI opens the
database with `Db::openReadOnly`/`openEncryptedReadOnly`; the CLI and the MCP
server are the write paths.

The pure half (`klend_ario_view` and its suites) always builds. The window and
the capture tool need `-DKLEND_BUILD_GUI=ON`, the Vulkan SDK, `slangc` and the
Wayland/xkb development packages.

## 2. The shape: three layers, only one needs a GPU

```
week_layout   geometry      pure. Rectangles from pixels + instants.
scene         decisions     pure. Colours, labels, borders, hatch, now-line,
                            header, truncation — emitted as draw ops.
scene_render  mapping       the ONLY vk_canvas code. Maps an Op to Canvas calls.
```

Why the split exists: a renderer cannot be tested on a machine without a GPU and
a display, and everything a pixel needs is a decision. `scene_test` asserts the
whole visual contract as an op list; `scene_render` is deliberately small enough
to read in one sitting. **If a colour, a label rule or a truncation decision
appears in `scene_render`, it is untested and in the wrong layer.**

## 3. Window seam: `app_shell`

- `Host` owns the native window, the message pump, timers, input and paths;
  `AppView` is the app's callback surface. `WeekView` implements
  `onHostResized`/`onHostExposed`/`onKeyDownPortable`/`onLButtonDown`/
  `onMouseMove`/`onMouseLeave`/`onTimer`.
- `Host::exeDir()` is the read-only shipped-data directory. Fonts live at
  `<exeDir>/fonts/ui/ui.otf`.
- `Host::dataReader()` reads `fonts/` and is the one to open a face with. On
  Android the bytes come out of the APK; `exeDir()` is empty and the relative
  path is the asset name. Do not add `#ifdef` around this.
- `Host::assetReader()` is rooted at `<exe>/assets/` for vk_canvas's shaders.
  It is not the font reader.
- Linux/Wayland cannot position its own window or query a monitor's work area;
  `snapToEdge`/`adaptToCurrentMonitor` are documented no-ops there.
- Desktop safe insets are zero. A camera cutout is glass, not a system bar; a
  keyboard is transient. `SafeInsets` and `keyboardInset()` are separate on
  purpose. Do not bake either into `week_layout`.

## 4. Renderer seam: `vk_canvas`

`Renderer` owns Vulkan, the swapchain, pipelines, the text atlas and the SDF
shape pipeline. `Canvas` is immediate-mode and appends into four buffers:

- `curves`: compute-rasterized vector records (legacy outline text, and any
  shape that needs the winding path).
- `shapes`: SDF shape quads for rects/segments/triangles when `useShapes()` is
  on. One draw, no compute rasterizer, analytic 1px AA.
- `textQuads`: textured glyph quads when `useMsdf()` is on.
- images / foreground images.

`Renderer::draw` argument order is load-bearing:

```cpp
draw(overlay_curves, rotation, images, foregroundImages,
     msdfQuads, shapeVerts, frontShapeVerts);
```

Text quads are the **fifth** argument; shapes are the sixth. Passing them the
other way gives a blank text layer and a shape pipeline reading glyph vertices
as rectangles.

The renderer's `OverlayRasterizer` is a **scene** cache, not a glyph cache:
`uploadCurves()` compares the whole curve buffer with `memcmp` and
`recordDispatch()` skips the compute passes while it is byte-identical. It does
not make an individual glyph reusable at another position or size. Do not
mistake it for the text atlas.

Shaders are compiled by `VceShaders`: the font engine's
`composite/tiling/coverage/msdf/glyph_area/glyph_resolve` plus vk_canvas's
`overlay/image/shape`. They are copied beside the executable; `FileAssetReader`
looks there.

## 5. Text: `RasterFont`, not `MsdfFont`

**Decision (2026-10-18): the weekly view draws text through `RasterFont` +
`Canvas::useMsdf` + `Renderer::initMsdf`, and passes the quads to
`Renderer::draw`.**

### 5.1 Why not the legacy outline path

The original 4a wiring loaded the framework's `Font` and drew every glyph as
cubic Bézier records through the compute rasterizer. That path's winding fill in
`first_party/vulkan_font_engine/shaders_src/coverage.slang` runs at
`const uint SS = 1u` and writes full alpha for `totalWinding != 0`: a binary
inside/outside test. Glyph edges are 1-bit. The source comment says
supersampling "only matters for the binary winding-fill path" and to raise it
for static text, but `SS` is a shader constant the app cannot set.

Evidence, from the pre-change 1080p capture: a crop around an event label had
exactly **2 unique RGB colours** (ink and background); the same was true of the
header. The rounded buttons had many colours because those go through the SDF
shape path. Smooth boxes + jagged text was the signature.

The fix is not to raise `SS` in the submodule: that is 16x the full-screen
compute at `SS=4`, still has no glyph reuse or subpixel phase, and requires a
framework change from the parent. Use the framework's text pipeline.

### 5.2 Why not MTSDF

MTSDF is a *different* cache: one distance-field bake serves any draw size. It
is the right choice for continuously magnified text (pinch-zoom, animated
scale), but it needs wide margins around every glyph (large atlas cells) and
reconstructs the edge in the shader.

The weekly view has a handful of fixed integer sizes, and a resize rebakes. That
is exactly the case `text_font.hh` calls right for per-size raster and wrong for
MSDF. `RasterFont` stores true 8-bit area coverage at the exact ppem, so there
is no field to reconstruct and no capacity cliff for a multi-script library.

The names `Renderer::initMsdf()` and `Canvas::useMsdf()` are the **shared text
pipeline**, not "use MTSDF". The `TextFont` object picks the mode:

- `MsdfFont` → `TextMode::Msdf/Mtsdf` → distance-field branch.
- `RasterFont` → `TextMode::Raster` → `msdf_frag.slang` takes `mtsdf == 2`,
  samples `tex.r` as coverage, and composites in linear light. The shader says:
  "THIS IS THE BRANCH MATRIX PLAYER RUNS."

### 5.3 The integration contract

- Open with `Host::dataReader()` at `<exeDir>/fonts/ui/ui.otf`.
- Before building the scene, collect the codepoints and the exact integer sizes
  the scene will draw (`gui/src/text_inputs`). `RasterFont::textWidth()` reads
  baked cells; a missing size returns 0, which makes `fitText()` believe every
  title fits. Measure only after the bake.
- `ensureGlyphs(cps, sizes)` bakes each `(style, size, phase, codepoint)` cell
  once. If the size set changed, `reset()` first: a per-size cache otherwise
  keeps every intermediate resize size forever.
- If cells were added (or the pipeline is not ready), `initMsdf(font)` uploads
  the atlas (R8, one page is 4 MB).
- Per frame, `canvas.useMsdf(&font, &quads)` and pass `quads` as `draw`'s fifth
  argument. Clear the quad buffer per frame; `Canvas` appends.
- `hasMisses()`/`bakeMisses()` is the one-frame safety net for a glyph the eager
  collector did not know about. Bake at the top of the next draw, then re-arm
  the frame from `run()` **after** `pendingFrames_ = 0` (a re-arm inside
  `drawFrame()` is erased by the loop).
- Keep the legacy `Font` only as a missing-font fallback. The normal path never
  emits glyph outlines per frame.

### 5.4 The measurement trap

`FontTextMetrics` (`gui/src/scene_render`) wraps the same `TextFont` the draw
uses. Its `textWidth()` includes the same GPOS kerning the layout applies, so
truncation and centred/right-aligned labels agree with what is drawn. If the
fallback outline font is active, it measures through `Font::stringWidth`; a null
pair reports zero width, which is the harmless direction (everything fits,
nothing lies about being wider than it is).

`dayDateSize` is reserved: no current text op draws at it, so
`collectTextInputs()` deliberately does not bake it. Add it there with the first
text op that uses it.

## 6. Text sizing and the readable floor

`metricsFor(content, minTextPx)` derives the chrome from the content rectangle,
never from a DPI query or device pixels. The fractions are the historical
design; the floor is new:

- `app_shell_generate_min_text_size(<font>)` runs the framework's
  `min_text_size` tool over the shipped face and emits
  `kMinReadableTextSizePx` (16.0 px for `ui.otf`; the thinnest stroke is
  0.03125 em, so 0.5 px at 16 px).
- The window passes that value into `metricsFor()`/`buildWeekScene()`, and to
  `collectTextInputs()`, so `klend_ario_view` stays free of `app_shell` and the
  pure tests keep the historical 0 behaviour.
- Every text role is clamped; geometry floors (`labelPad`, `hatchSpacing`, …)
  are untouched. At 1080p this raises only the event time (14 → 16); at 720p it
  raises the event time and title (12/14 → 16). The trade-off is honest: a
  title may no longer fit in a short block, but a 12 px thin serif is not
  readable either.
- The day header "SU 11" uses `dayLetterSize`; `dayDateSize` is not drawn as a
  separate role.

## 7. Descenders and the text clip

A text op's `Rect` is the horizontal anchor and the clip. Its height is the
font size, and `Canvas::text` puts the baseline at `rect.y + size`. Descenders
(`p/g/y`) therefore live **below** the op rect.

`scene_render` extends only the **clip** downward by `max(1, 0.30 em)` when it
draws a Text op. It does not change the op rect, the baseline, the y advance or
the scene's height tiering. `ui.otf` has hhea descent 0.2 em and OS/2 win
descent 0.3 em, so 0.30 em covers the face with a little margin. The pre-change
captures showed "Ship", "Overlap" and "Deep" with the `p` tail cut; check those
and "today"/"Sep" (`y`/`p`) after any clip change.

## 8. Local days and time

- A local day is `[startUtc, endUtc)`; a DST day is 23 or 25 hours. Never assume
  86,400 s when drawing. `dayTimeRect`/`offsetForInstant` take the bounds.
- `gui/src/local_time` is the only place a timezone is read. Core stores UTC;
  labels are local display; the CLI/MCP share the same query semantics.
- `klend_ario_capture` pins `TZ=UTC0` so the fixtures are deterministic.

## 9. Database and refresh

- Read-only: `Db::openReadOnly` / `openEncryptedReadOnly`. The file must already
  exist; opening with create/migrate would turn a wrong path into a silent empty
  week. The error names the path and tells the user how to create a calendar.
- Hidden rows are a query choice (`Visibility::Any` with `--hidden`); the scene
  draws what it is given. `EventType::Alarm` is a reminder, not a block.
- `Db::dataVersion()` is polled once a second; a commit by another process (CLI
  or MCP) reloads the week. The now-line is recomputed every tick. This is how
  an AI edit reaches an open window without a socket or file watcher.
- Sync conflict copies are reported on startup, never merged (`MEMORY.md` §7).
- No write path until 4b. Do not add one without deciding the edit surface
  (CLI-parity fields, `*Given` semantics).

## 10. The scene contract

- An `Op` is a decision: `Fill`, `Stroke`, `Text`, `Hatch`. The renderer maps
  it; it does not add meaning.
- Colours for events come from core (`rgbForStyle`, `textColorOn`) so the screen
  and any future print agree. Chrome lives in `gui/theme`.
- `TextMetrics` is injected, so truncation is tested without a font.
- Hit targets are part of the scene, not the renderer.
- `scene_test` is the visual gate on a machine without a GPU; `klend_ario_capture`
  is the pixel gate. Both must agree with the window's text path.

## 11. Capture is the visual gate

`klend_ario_capture` renders fixtures through `VK_EXT_headless_surface`:
no window, no compositor, deterministic (`TZ=UTC0`).

```bash
cd build/gui
./klend_ario_capture --out /tmp/shots              # 1920x1080
./klend_ario_capture --frame 1280x720 --out /tmp/shots720
./klend_ario_capture --list
```

Scenarios: empty week, mixed week, sleep visible, overlap density. The capture
tool uses the same `RasterFont` and `useMsdf` as the window; if it did not, the
screenshots would lie.

How to check text quality without eyeballing a scaled preview:

```bash
python3 - <<'PY'
from PIL import Image
im = Image.open('/tmp/shots/20-week/00-mixed.png').convert('RGB')
c = im.crop((1085, 845, 1215, 880))          # an event label
print(len({p for p in c.getdata()}))         # 2 = 1-bit text; hundreds = coverage
PY
```

The pre-RasterFont capture returned exactly 2 colours. The raster path returns
hundreds of intermediate shades. Also crop an event label with a `p` and a
header containing "Sep" to verify the descent allowance.

## 12. Build and run

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DKLEND_BUILD_GUI=ON
ninja -C build
ctest --test-dir build --output-on-failure

./build/gui/klend_ario_gui                     # real state database
./build/gui/klend_ario_gui --db /tmp/demo.db   # throwaway
```

`slangc` is resolved from `-DVCE_SLANGC`, `$VULKAN_SDK/bin`, then `PATH`.
Shaders and `fonts/ui/` are copied beside the executable; `FileAssetReader`
looks there.

Keys: `Left`/`Right` previous/next week, `T` today, `H` toggle hidden blocks,
`Esc` quit. Header buttons do the same with the mouse.

## 13. File map

| File | Role |
|---|---|
| `src/week_layout.{hh,cc}` | frame/grid/overlap geometry, half-open day rects |
| `src/local_time.{hh,cc}` | the one timezone read; local days with UTC bounds and labels |
| `src/theme.{hh,cc}` | chrome palette + `UiMetrics` (content-derived, optional font floor) |
| `src/scene.{hh,cc}` | `WeekModel` + `TextMetrics` → draw ops and hit targets |
| `src/text_inputs.{hh,cc}` | the codepoint/size set the glyph cache must bake (pure) |
| `src/week_store.{hh,cc}` | `Db` + local week → `WeekModel`, one `EventQuery` |
| `src/scene_render.{hh,cc}` | `Scene` → `Canvas`; `FontTextMetrics`; descent clip |
| `src/week_view.{hh,cc}` | `AppView`: window, RasterFont, read-only DB, input, 1 s poll |
| `src/gui_main.cc` | `app_shell_main`: `--db`, `--key-file`, `--hidden` |
| `tools/capture_main.cc` | headless PNG scenarios (Debug-only) |
| `tests/*` | pure geometry/scene/store/input tests; no GPU |

## 14. Traps

- `coverage.slang`'s winding fill is `SS=1` → 1-bit text. Do not "fix" it by
  raising `SS` in the framework; use `RasterFont`.
- `initMsdf`/`useMsdf` are shared text-pipeline names. The font object chooses
  raster vs MTSDF; the name does not.
- `RasterFont::textWidth()` is 0 before the cells exist. Collect and bake before
  building the scene.
- `reset()` on a size-set change; otherwise every resize adds a new size set to
  the atlas forever.
- The `OverlayRasterizer` memcmp cache is the whole curve buffer; it is not a
  glyph cache and does not survive a text change cheaply.
- A re-arm inside `drawFrame()` is erased by `run()`'s `pendingFrames_ = 0`.
  Re-arm after the decrement, keyed on `hasMisses()`.
- Descenders are clipped by the op rect. Extend the text clip; do not change the
  layout's y advance without updating the scene tests.
- `dayDateSize` is reserved; add it to `text_inputs` with its first text op.
- The pure `klend_ario_view` must not include `app_shell/ui_metrics.hh`; pass the
  font floor in.
- Never edit the framework submodules from this repository. A change there is
  made, tested and pushed in the child, then the parent pin advances.

## 15. Decision log

- **2026-10-17 — Phase 4a.** Read-only weekly view, scene + window + headless
  capture. Text initially used the legacy `Font` outline path.
- **2026-10-18 — text path replaced with `RasterFont`.** The outline path's
  winding fill is `SS=1`; the 1080p capture had exactly 2 colours in text crops
  while boxes were smooth. `RasterFont` bakes true FreeType coverage once per
  size into an R8 atlas and draws quads through the shared text pipeline (the
  branch Matrix Player runs). Also added: eager codepoint/size collection
  (`text_inputs`), a 0.30 em descent allowance in the text clip, and the
  per-font readable floor (`kMinReadableTextSizePx`) passed into the scene.
  Verified with 1920x1080 and 1280x720 captures (hundreds of edge colours, no
  clipped `p` tails) and a live 1200x700 window.
