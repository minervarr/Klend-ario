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
struct Font;

namespace klend_ario::gui {

// Real glyph advances, so truncation in the scene matches what is drawn. A null
// font (the stroke fallback) reports zero width: every label then fits, which is
// the harmless direction to be wrong in.
class FontTextMetrics : public TextMetrics {
public:
    explicit FontTextMetrics(const Font *font) : font_(font) {}
    int width(std::string_view text, int sizePx) const override;

private:
    const Font *font_;
};

void drawScene(Canvas &canvas, const Scene &scene);

} // namespace klend_ario::gui
