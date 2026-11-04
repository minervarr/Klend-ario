# gui/ — the weekly 16:9 view

The product's rendering frontend (Phase 4). It links `core`; `core` never links
it. Drawing is `vk_canvas`; the window/host seam is `app_shell`.

## The shape: three layers, only one of them needs a GPU

```
week_layout   geometry      pure. Rectangles from pixels + instants.
scene         decisions     pure. Colours, labels, borders, hatch, now-line,
                            header — emitted as a list of draw ops.
scene_render  mapping       the ONLY vk_canvas code. Maps ops to Canvas calls.
```

The split exists because a renderer cannot be tested on a machine without a GPU
and a display, and everything a pixel needs is a decision. `scene_test` asserts
the whole visual contract as an op list; the renderer is small enough to read in
one sitting.

| File | Role |
|---|---|
| `src/week_layout.{hh,cc}` | `frameFor` (16cm×9cm baseline margins), `weekGrid` (Sunday-first columns), `layoutDay` (overlap clusters), `dayTimeRect`, `tailRect`, `offsetForInstant` |
| `src/local_time.{hh,cc}` | the one place a timezone is consulted: instant → local civil day with its **half-open UTC bounds** (23/25 h on DST days), 24-hour labels |
| `src/theme.{hh,cc}` | chrome palette + `UiMetrics` derived from the content rect (no DPI query, no device pixels) |
| `src/scene.{hh,cc}` | `WeekModel` + `TextMetrics` → `Scene` (draw ops and hit targets) |
| `src/week_store.{hh,cc}` | `Db` + local week → `WeekModel`, one `EventQuery` |
| `src/editor.{hh,cc}` | the edit form as pure state: Event ↔ local-time fields, validation, DST flags |
| `src/editor_scene.{hh,cc}` | the modal's draw ops and `field:*` / `editor:*` hit targets |
| `src/editor_store.{hh,cc}` | the form → `Db::insertEvent` / CAS `updateEvent` / delete bridge |
| `src/text_inputs.{hh,cc}` | the codepoint/size set the glyph cache must bake before measuring |
| `src/scene_render.{hh,cc}` | `Scene` → `Canvas`; `FontTextMetrics`; descent clip |
| `src/week_view.{hh,cc}` | `AppView`: window, `RasterFont`, writable DB, editor input, 1 s poll, F12 capture |
| `src/gui_main.cc` | `app_shell_main`: `--db`, `--key-file`, `--hidden`, `--read-only`, `--screenshot-dir` |
| `tools/capture_main.cc` | headless fixture PNG scenarios (dev tooling) |

## Build

The pure half (`klend_ario_view` + its suites: geometry, local time, theme,
scene, week store, text inputs, refresh, editor, editor scene, editor store)
always builds. The window and the capture tool need the Vulkan SDK, the Slang
compiler and the Wayland/xkb development packages, so they are behind one
option:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DKLEND_BUILD_GUI=ON
ninja -C build
```

`slangc` is resolved from `-DVCE_SLANGC=/path/to/slangc` first, then from
`$VULKAN_SDK/bin`, then from `PATH` on Linux. Shaders are compiled to
`build/assets/shaders` and copied beside the binary together with `fonts/ui/`,
because `FileAssetReader` looks next to the executable.

## Run

```bash
./build/gui/klend_ario_gui                     # the real state database
./build/gui/klend_ario_gui --db /tmp/demo.db   # a throwaway one
./build/gui/klend_ario_gui --help
```

Keys: `Left`/`Right` previous/next week · `T` today · `H` toggle hidden blocks
(sleep) · `N` new · `Enter` edit selected · `Esc` cancel/close or quit ·
`F12` screenshot. The header buttons do the same with the mouse. Click an event
once to select it, again (or `Enter`) to open the form; Delete asks twice.

It is **writable by default** (Phase 4b): the form saves through
`editor_store` → `Db::insertEvent` / `Db::updateEvent`, the latter a revision
compare-and-swap. `--read-only` keeps the old viewer contract, including
"the database must already exist". External writes still appear within a
second through `Db::dataVersion()`; a stale save is refused, not applied.

`F12` writes a PNG of the current window, editor included, to the app's data
directory (`~/.local/share/klend_ario` by default) or `--screenshot-dir`.
It renders the frame with present disabled, reads it back, then presents on the
next pass — presenting first lets the compositor recycle the image.

## Screenshots

`F12` in the running app is the live path. The separate tool remains for
fixtures and for machines with no display: `klend_ario_capture` renders fixture
weeks through `VK_EXT_headless_surface` and writes PNGs — no window, no
compositor, deterministic (the tool pins `TZ=UTC0`, so the clock labels are
stable):

```bash
cd build/gui
./klend_ario_capture --out /tmp/shots          # 1920x1080
./klend_ario_capture --frame 1280x720          # full-bleed layout
./klend_ario_capture --list                    # scenario paths
```

Scenarios: an empty week, a mixed week, sleep visible vs hidden, overlap
density (two to four concurrent events plus one crossing midnight), and the
editor in edit/new state.

## Text

The window draws text with **`RasterFont`**, not the legacy outline path:
FreeType bakes true 8-bit coverage once per `(glyph, size, phase)` into an R8
atlas, and each frame emits textured quads through `Canvas::useMsdf` +
`Renderer::initMsdf`. That is the "Bézier outlines, cached" design; the outline
path's winding fill is `SS=1` and looks 1-bit. MTSDF is not used here — the
weekly view has fixed integer sizes, so per-size coverage is the right cache.

`gui/src/text_inputs` collects the codepoints and sizes **before** the scene is
built, because `RasterFont::textWidth()` returns 0 for an unbaked cell. Sizes
are clamped to the font's generated `kMinReadableTextSizePx` (16 px for
`ui.otf`), and the text clip gets a 0.30 em descent allowance so `p/g/y` are not
cut. The full rationale, traps and capture checks are in
[`../essentialAgent/GUI.md`](../essentialAgent/GUI.md).

## Rules worth keeping

- **A day is `[startUtc, endUtc)`, not 86,400 seconds.** The model is UTC but the
  columns are local days, and a DST day is 23 or 25 hours. `dayTimeRect` took a
  single day start and assumed a day length; that put every event after a DST
  transition in the wrong place, twice a year, in some zones.
- **Overlaps are a day-level question.** `layoutDay` clusters intervals and
  splits each cluster into side-by-side slots. Touching intervals (`end ==
  start`) do not overlap, matching the half-open rule everywhere else.
- **Short events keep their position.** Height stays proportional; labels are
  tiered (time, then title, then nothing), so a five-minute event is a thin
  coloured line at its true start rather than a box that lies about its duration.
- **Hidden is a query, not a scene filter.** `--hidden` chooses `Visibility::Any`;
  the scene draws what it is given. `EventType::Alarm` rows are reminders, not
  blocks, so the scene skips those.
- **The week label comes from the week.** A week containing 1 January is week 1
  of the new year, even though its Sunday is in December.
- **Colours live in core.** `styleFor` (hue/border) and `rgbForStyle` +
  `textColorOn` (pixels) are core functions so the screen and any future print
  agree. The GUI owns only chrome.
- **Text is a separate renderer layer.** Every text op is batched after every
  shape, regardless of issue order. When the modal is open the week's text ops
  are dropped, or day headers paint over the panel fill. The modal's own text is
  then the only text, on top of the dim/panel shapes.
- **A save is compare-and-swap.** `editor_store` sends the `Event.revision` the
  form was opened with; a stale draft gets an error banner, not an overwrite.
- **Local time is resolved, not parsed.** `localTimeToUtc` flags a
  spring-forward gap and a fall-back repetition; an unchanged ambiguous field
  keeps its original instant. `editor_test` pins all three cases.
- **A screenshot must not present first.** `F12` renders with present disabled,
  reads the image back, then re-enables present on the next frame. Reading a
  presented image can copy recycled memory.
- **Notes is single-line in the 4b form.** The storage model allows newlines and
  the CLI/MCP do; the first GUI editor does not grow the box. Multiline is later
  work, not a hidden data change.
