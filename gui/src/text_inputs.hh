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
#include <string>
#include <vector>

#include "scene.hh"
#include "theme.hh"

namespace klend_ario::gui {

struct TextInputs {
    std::vector<uint32_t> codepoints; // sorted, unique
    std::vector<int> sizesPx;         // sorted, unique
};

TextInputs collectTextInputs(const WeekModel &week, const UiMetrics &metrics);

// The edit modal draws labels, field values and an error line at sizes the
// week does not use. The caller supplies the strings and sizes; this stays
// free of editor.hh so the pure collector has no core dependency.
TextInputs collectEditorTextInputs(const std::vector<std::string> &texts,
                                   const std::vector<int> &sizesPx);

// Union of two collectors, still sorted and unique. Used when the modal is
// open: the week behind it is still drawn and its title glyphs may be new.
TextInputs mergeTextInputs(const TextInputs &first, const TextInputs &second);

} // namespace klend_ario::gui
