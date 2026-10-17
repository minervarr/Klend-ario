#include "scene_render.hh"

#include <algorithm>
#include <cmath>

#include "canvas.hh"
#include "font.hh"

namespace klend_ario::gui {
namespace {

::Color toCanvasColor(const Color &color) {
    return ::Color{color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f};
}

} // namespace

int FontTextMetrics::width(std::string_view text, int sizePx) const {
    if (font_ == nullptr || text.empty() || sizePx <= 0) {
        return 0;
    }
    return static_cast<int>(std::lround(font_->stringWidth(text, static_cast<float>(sizePx))));
}

void drawScene(Canvas &canvas, const Scene &scene) {
    for (const Op &op : scene.ops) {
        const Rect &r = op.rect;
        const ::Color color = toCanvasColor(op.color);
        switch (op.kind) {
        case OpKind::Fill:
            canvas.rect(static_cast<float>(r.x), static_cast<float>(r.y),
                        static_cast<float>(r.w), static_cast<float>(r.h), color,
                        static_cast<float>(op.radius));
            break;

        case OpKind::Stroke: {
            // An INSET stroke, drawn as four bands, so the box keeps the rect the
            // layout gave it and urgency never grows the event.
            const int t = std::clamp(op.thickness, 1, std::max(1, std::min(r.w, r.h)));
            const float x = static_cast<float>(r.x);
            const float y = static_cast<float>(r.y);
            const float w = static_cast<float>(r.w);
            const float h = static_cast<float>(r.h);
            const float ft = static_cast<float>(t);
            canvas.rect(x, y, w, ft, color);                 // top
            canvas.rect(x, y + h - ft, w, ft, color);        // bottom
            canvas.rect(x, y, ft, h, color);                 // left
            canvas.rect(x + w - ft, y, ft, h, color);        // right
            break;
        }

        case OpKind::Text: {
            // The op's rect is the anchor box: horizontally aligned inside it,
            // and CLIPPED to it so a long title can never bleed into the
            // neighbouring day. Canvas text y is the top of the box.
            canvas.setClip(static_cast<float>(r.x), static_cast<float>(r.y),
                           static_cast<float>(r.w), static_cast<float>(r.h));
            const float size = static_cast<float>(op.size);
            const float top = static_cast<float>(r.y);
            switch (op.align) {
            case Align::Left:
                canvas.text(op.text, static_cast<float>(r.x), top, size, color);
                break;
            case Align::Center:
                canvas.textCentered(op.text, static_cast<float>(r.x) + static_cast<float>(r.w) / 2.0f,
                                    top, size, color);
                break;
            case Align::Right:
                canvas.textRight(op.text, static_cast<float>(r.right()), top, size, color);
                break;
            }
            canvas.clearClip();
            break;
        }

        case OpKind::Hatch: {
            // 45-degree lines inside the clip: the "nothing else today" region.
            // The clip is what makes the lines stop at the column and at the
            // tail's own top edge.
            if (r.empty() || op.spacing <= 0) {
                break;
            }
            canvas.setClip(static_cast<float>(r.x), static_cast<float>(r.y),
                           static_cast<float>(r.w), static_cast<float>(r.h));
            const float thickness = static_cast<float>(std::max(op.thickness, 1));
            const float spacing = static_cast<float>(op.spacing);
            const float x0 = static_cast<float>(r.x);
            const float y0 = static_cast<float>(r.y);
            const float x1 = static_cast<float>(r.right());
            const float y1 = static_cast<float>(r.bottom());
            for (float x = x0 - static_cast<float>(r.h); x < x1; x += spacing) {
                canvas.segment(x, y0, x + static_cast<float>(r.h), y1, thickness, color);
            }
            canvas.clearClip();
            break;
        }
        }
    }
}

} // namespace klend_ario::gui
