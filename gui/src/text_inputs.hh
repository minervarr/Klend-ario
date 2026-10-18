#pragma once

// The codepoints and pixel sizes the weekly view can draw.
//
// The text renderer caches rasterized glyphs by (style, sizePx, phase,
// codepoint). It cannot bake from what it does not know, and its textWidth()
// returns 0 for a cell that has not been baked — which would make fitText()
// believe every title fits. So the window collects the drawable text BEFORE
// building the scene, bakes exactly that set, and only then measures.
//
// Pure: it sees a WeekModel and UiMetrics, never a font, a Canvas or a GPU.

#include <cstdint>
#include <vector>

#include "scene.hh"
#include "theme.hh"

namespace klend_ario::gui {

struct TextInputs {
    std::vector<uint32_t> codepoints; // sorted, unique
    std::vector<int> sizesPx;         // sorted, unique
};

TextInputs collectTextInputs(const WeekModel &week, const UiMetrics &metrics);

} // namespace klend_ario::gui
