#pragma once

// The edit modal as draw ops, built from the pure EditorState.
//
// It is a second scene builder, not a second renderer: the window concatenates
// this scene after the week scene, and the same dumb adapter maps the ops to
// Canvas calls. Every colour, label, focus ring, field box and button position
// is decided here and asserted by editor_scene_test without a GPU.
//
// Hit ids have two prefixes: "field:<name>" for the editable fields and
// "editor:save|delete|cancel" for the actions. The view uses those ids; it never
// hard-codes a rectangle.

#include "editor.hh"
#include "scene.hh"

namespace klend_ario::gui {

Scene buildEditorScene(int width, int height, const EditorState &state,
                       const TextMetrics &text, const Theme &theme = defaultTheme(),
                       int minTextPx = 0);

} // namespace klend_ario::gui
