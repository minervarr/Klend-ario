#pragma once

// The ONLY place a Scene meets vk_canvas.
//
// Everything a pixel needs has already been decided in scene.cc: colour, size,
// thickness, which label, where the hatch goes. This layer is deliberately
// dumb — a switch that maps an Op to a Canvas call — because it is the part
// that cannot be tested on a machine without a GPU, and the smaller it is the
// less there is to get wrong.
//
// It belongs to the window target, not to klend_ario_view.

#include <string_view>

#include "scene.hh"

class Canvas;
class TextFont;
struct Font;

namespace klend_ario::gui {

// Real glyph advances, so truncation in the scene matches what is drawn.
//
// `text` is the normal path: a RasterFont (or MsdfFont) through the shared text
// pipeline, whose textWidth() includes the same GPOS kerning the layout uses.
// `outline` is the missing-font fallback, the legacy FreeType outline path. A
// null pair reports zero width: every label then fits, which is the harmless
// direction to be wrong in.
class FontTextMetrics : public TextMetrics {
public:
    FontTextMetrics(const TextFont *text, const Font *outline = nullptr)
        : text_(text), outline_(outline) {}
    int width(std::string_view text, int sizePx) const override;

private:
    const TextFont *text_;
    const Font *outline_;
};

void drawScene(Canvas &canvas, const Scene &scene);

} // namespace klend_ario::gui
