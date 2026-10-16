# gui/ — the weekly 16:9 view

The product's rendering frontend (Phase 4). It links `core`; `core` never links
it. The drawing engine is `vk_canvas` and the app/window/host seam is
`app_shell` — both already mounted under
`../libs/firstparty/framework/`.

## What exists now

`src/week_layout.{hh,cc}` (target `klend_ario_view`) — the view's **geometry**,
as pure functions:

- `frameFor()` — the 16cm×9cm baseline frame; 1cm-equivalent margins at FHD and
  up, full-bleed below (`merged.md` §5.1).
- `weekGrid()` — the seven Sunday-first day columns plus the header band,
  tiling the content width exactly.
- `dayTimeRect()` — an event's box on a 00:00–24:00 axis, with clip flags.
- `tailRect()` — the hatched "nothing else today" region.

It touches no Canvas, no window, no OS, so `tests/week_layout_test.cc` asserts
the whole view with no GPU.

## What does not exist yet

The **renderer** (the `vk_canvas` half): mapping those rectangles to draw calls,
with `core`'s colours (`klend_ario::styleFor`) and `app_shell`'s window/host.
It is deliberately absent because it needs the Vulkan SDK **and** the `slangc`
shader compiler, plus a real display, none of which the machine that wrote this
had. Build it where it can run, and keep every plan it makes assertable in
`week_layout_test` wherever possible.
