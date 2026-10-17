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
| `src/scene_render.{hh,cc}` | `Scene` → `Canvas`; `FontTextMetrics` |
| `src/week_view.{hh,cc}` | `AppView`: window, read-only DB, keys, clicks, 1 s poll |
| `src/gui_main.cc` | `app_shell_main`: `--db`, `--key-file`, `--hidden` |
| `tools/capture_main.cc` | headless PNG scenarios (dev tooling) |

## Build

The pure half (`klend_ario_view` + its four suites) always builds. The window
and the capture tool need the Vulkan SDK, the Slang compiler and the Wayland/xkb
development packages, so they are behind one option:

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
(sleep) · `Esc` quit. The header buttons do the same with the mouse.

It is **read-only** in this slice: the CLI and the MCP server are the write
paths, and a viewer cannot corrupt the calendar. It still *shows* their writes —
`Db::dataVersion()` is polled once a second, so an edit committed by another
process appears in an open window without a socket or a file watcher.

## Screenshots without a display

`klend_ario_capture` renders fixture weeks through `VK_EXT_headless_surface` and
writes PNGs — no window, no compositor, deterministic (the tool pins `TZ=UTC0`,
so the clock labels are stable):

```bash
cd build/gui
./klend_ario_capture --out /tmp/shots          # 1920x1080
./klend_ario_capture --frame 1280x720          # full-bleed layout
./klend_ario_capture --list                    # scenario paths
```

Scenarios: an empty week, a mixed week, sleep visible vs hidden, and overlap
density (two to four concurrent events plus one crossing midnight).

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
