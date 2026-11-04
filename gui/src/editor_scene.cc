#include "editor_scene.hh"

#include <algorithm>
#include <string>
#include <string_view>

#include "week_layout.hh"

namespace klend_ario::gui {
namespace {

const std::string &fieldValue(const EditorState &state, EditorField field) {
    switch (field) {
    case EditorField::Start:
        return state.start;
    case EditorField::End:
        return state.end;
    case EditorField::Type:
        return state.type;
    case EditorField::Importance:
        return state.importance;
    case EditorField::Urgency:
        return state.urgency;
    case EditorField::AlarmOffset:
        return state.alarmOffset;
    case EditorField::Notes:
        return state.notes;
    case EditorField::Title:
    default:
        return state.title;
    }
}

const char *fieldPlaceholder(EditorField field) {
    switch (field) {
    case EditorField::Start:
    case EditorField::End:
        return "YYYY-MM-DD HH:MM";
    case EditorField::Type:
        return "event";
    case EditorField::Importance:
    case EditorField::Urgency:
        return "0..100";
    case EditorField::AlarmOffset:
        return "seconds";
    case EditorField::Notes:
        return "optional";
    case EditorField::Title:
    default:
        return "Untitled";
    }
}

std::string oneLine(std::string text) {
    for (char &ch : text) {
        if (ch == '\n' || ch == '\r') {
            ch = ' ';
        }
    }
    return text;
}

void addLabel(Scene &scene, int x, int y, int width, std::string_view label,
              const UiMetrics &metrics, const Theme &theme, const TextMetrics &text) {
    const std::string shown =
        fitText(label, metrics.editorLabelSize, width, text);
    if (!shown.empty()) {
        scene.ops.push_back(textOp(Rect{x, y, width, metrics.editorLabelSize}, shown,
                                   metrics.editorLabelSize, theme.dimText));
    }
}

void addField(Scene &scene, const Rect &box, EditorField field, const EditorState &state,
              const UiMetrics &metrics, const Theme &theme, const TextMetrics &text) {
    const bool focused = state.focus == field;
    scene.ops.push_back(fillOp(box, theme.fieldFill, 3));
    scene.ops.push_back(strokeOp(box, focused ? theme.selection : theme.fieldBorder,
                                 focused ? 2 : 1, 3));

    const int pad = std::max(metrics.labelPad, 4);
    const int innerWidth = std::max(box.w - 2 * pad, 0);
    const std::string &value = fieldValue(state, field);
    const bool empty = value.empty();
    const std::string shown =
        fitText(empty ? fieldPlaceholder(field) : oneLine(value), metrics.editorFieldSize,
                innerWidth, text);
    if (!shown.empty() && box.h >= metrics.editorFieldSize) {
        // Canvas text y is the top of the em box, not the glyph box; the small
        // floor keeps an ascender off the field border on a tight field.
        const int textY =
            box.y + std::max(2, (box.h - metrics.editorFieldSize) / 2);
        scene.ops.push_back(textOp(
            Rect{box.x + pad, textY, innerWidth, metrics.editorFieldSize}, shown,
            metrics.editorFieldSize, empty ? theme.dimText : theme.headerText));
    }
    if (focused) {
        // The real caret lives in the text widget; the scene only has to make
        // the focused target unmistakable.
        scene.ops.push_back(fillOp(
            Rect{box.right() - pad - 1, box.y + 4, 1, std::max(box.h - 8, 1)},
            theme.selection));
    }
    scene.hits.push_back(
        HitTarget{box, std::string("field:") + editorFieldName(field)});
}

int addButton(Scene &scene, int x, int y, const char *label, const char *id,
              bool primary, bool danger, const UiMetrics &metrics, const Theme &theme,
              const TextMetrics &text) {
    const int labelWidth = text.width(label, metrics.editorButtonSize);
    const int width =
        std::max(labelWidth + 2 * std::max(metrics.buttonPad, 8), 64);
    const Rect box{x, y, width, std::max(metrics.buttonHeight, metrics.editorFieldHeight)};
    scene.ops.push_back(fillOp(box, theme.buttonFill, 4));
    scene.ops.push_back(
        strokeOp(box, primary ? theme.selection : theme.fieldBorder, primary ? 2 : 1, 4));
    scene.ops.push_back(textOp(
        Rect{box.x, box.y + (box.h - metrics.editorButtonSize) / 2, box.w,
             metrics.editorButtonSize},
        label, metrics.editorButtonSize,
        danger ? theme.errorText : (primary ? theme.headerText : theme.dimText),
        Align::Center));
    scene.hits.push_back(HitTarget{box, id});
    return width;
}

} // namespace

Scene buildEditorScene(int width, int height, const EditorState &state,
                       const TextMetrics &text, const Theme &theme, int minTextPx) {
    Scene scene;
    // Dim the week, then own the centre. The week scene is drawn first and this
    // fill sits over it; the modal is not a separate window.
    scene.ops.push_back(fillOp(Rect{0, 0, width, height}, Color{0, 0, 0, 170}));

    const Rect content = frameFor(width, height).content;
    const UiMetrics m = metricsFor(content, minTextPx);
    const int pad = std::max(content.w / 40, 12);
    const int panelWidth =
        std::min(std::max(content.w - 2 * pad, 320), std::max(content.w * 4 / 5, 420));
    const int innerPad = std::max(pad * 3 / 4, 10);

    // Size to the form, not to the monitor. A fixed 90 % panel leaves a huge
    // empty field under the buttons on a 4K screen; the estimate is the five
    // rows + title + status + actions + padding, clamped to the content.
    const int labelGapEstimate = std::max(1, m.labelPad / 2);
    const int rowEstimate = m.editorLabelSize + labelGapEstimate + m.editorFieldHeight;
    const int buttonEstimate = std::max(m.buttonHeight, m.editorFieldHeight);
    const int needed = 2 * innerPad + m.editorTitleSize + 2 * m.editorRowGap +
                       5 * (rowEstimate + m.editorRowGap) + m.editorErrorSize +
                       buttonEstimate;
    const int panelHeight = std::min(
        std::max(content.h - 2 * pad, 320), std::max({needed, 300, content.h / 2}));
    const Rect panel{content.x + (content.w - panelWidth) / 2,
                     content.y + (content.h - panelHeight) / 2, panelWidth, panelHeight};
    scene.ops.push_back(fillOp(panel, theme.panel, 6));
    scene.ops.push_back(strokeOp(panel, theme.panelBorder, 1, 6));

    const Rect inner{panel.x + innerPad, panel.y + innerPad, panel.w - 2 * innerPad,
                     panel.h - 2 * innerPad};

    const std::string title =
        state.isNew ? "New event" : "Edit event " + std::to_string(state.id);
    scene.ops.push_back(textOp(Rect{inner.x, inner.y, inner.w, m.editorTitleSize}, title,
                               m.editorTitleSize, theme.headerText));

    const int labelH = m.editorLabelSize;
    const int labelGap = std::max(1, m.labelPad / 2);
    const int fieldH = m.editorFieldHeight;
    const int rowH = labelH + labelGap + fieldH;
    const int gap = m.editorRowGap;
    const int colGap = gap;
    int y = inner.y + m.editorTitleSize + gap;

    auto labelRow = [&](const char *label, int x, int width) {
        addLabel(scene, x, y, width, label, m, theme, text);
    };
    auto fieldBox = [&](int x, int width) {
        return Rect{x, y + labelH + labelGap, width, fieldH};
    };

    // Title.
    labelRow("Title", inner.x, inner.w);
    addField(scene, fieldBox(inner.x, inner.w), EditorField::Title, state, m, theme, text);
    y += rowH + gap;

    // Start / End, side by side.
    const int half = (inner.w - colGap) / 2;
    const int halfRight = inner.w - half - colGap;
    labelRow("Start (local)", inner.x, half);
    labelRow("End (local)", inner.x + half + colGap, halfRight);
    addField(scene, fieldBox(inner.x, half), EditorField::Start, state, m, theme, text);
    addField(scene, fieldBox(inner.x + half + colGap, halfRight), EditorField::End, state,
             m, theme, text);
    y += rowH + gap;

    // Type / Importance / Urgency.
    const int third = (inner.w - 2 * colGap) / 3;
    const int thirdRight = inner.w - 2 * third - 2 * colGap;
    labelRow("Type", inner.x, third);
    labelRow("Importance", inner.x + third + colGap, third);
    labelRow("Urgency", inner.x + 2 * (third + colGap), thirdRight);
    addField(scene, fieldBox(inner.x, third), EditorField::Type, state, m, theme, text);
    addField(scene, fieldBox(inner.x + third + colGap, third), EditorField::Importance, state,
             m, theme, text);
    addField(scene, fieldBox(inner.x + 2 * (third + colGap), thirdRight), EditorField::Urgency,
             state, m, theme, text);
    y += rowH + gap;

    // Alarm offset / Hidden toggle.
    labelRow("Alarm offset (s)", inner.x, half);
    addField(scene, fieldBox(inner.x, half), EditorField::AlarmOffset, state, m, theme, text);
    addLabel(scene, inner.x + half + colGap, y, halfRight, "Hidden", m, theme, text);
    const Rect check{inner.x + half + colGap, y + labelH + labelGap, fieldH, fieldH};
    scene.ops.push_back(fillOp(check, state.hidden ? theme.selection : theme.fieldFill, 3));
    scene.ops.push_back(strokeOp(
        check, state.focus == EditorField::Hidden ? theme.selection : theme.fieldBorder,
        state.focus == EditorField::Hidden ? 2 : 1, 3));
    if (state.hidden) {
        scene.ops.push_back(textOp(check, "x", m.editorButtonSize, theme.background,
                                   Align::Center));
    }
    scene.hits.push_back(HitTarget{check, "field:hidden"});
    y += rowH + gap;

    // Notes (a single-line field in v1; multiline is a later editor item).
    labelRow("Notes", inner.x, inner.w);
    addField(scene, fieldBox(inner.x, inner.w), EditorField::Notes, state, m, theme, text);

    // Status and actions are anchored to the panel bottom, so a long note field
    // never pushes Save off-screen.
    const int buttonH = std::max(m.buttonHeight, fieldH);
    const int buttonY = panel.bottom() - innerPad - buttonH;
    const int statusY = buttonY - gap - m.editorErrorSize;
    const std::string status =
        !state.error.empty()
            ? state.error
            : (!state.warning.empty()
                   ? state.warning
                   : (state.confirmDelete
                          ? "Delete this event? Click Delete again or press Enter."
                          : std::string{}));
    if (!status.empty() && statusY >= 0) {
        const Color color =
            !state.error.empty()
                ? theme.errorText
                : (state.confirmDelete && state.warning.empty() ? theme.errorText
                                                                : theme.holidayAccent);
        const std::string shown = fitText(status, m.editorErrorSize, inner.w, text);
        if (!shown.empty()) {
            scene.ops.push_back(textOp(
                Rect{inner.x, statusY, inner.w, m.editorErrorSize}, shown,
                m.editorErrorSize, color));
        }
    }

    int buttonX = inner.x;
    if (!state.isNew) {
        buttonX += addButton(scene, buttonX, buttonY,
                             state.confirmDelete ? "Confirm delete" : "Delete",
                             "editor:delete", false, true, m, theme, text) +
                   gap;
    }
    buttonX += addButton(scene, buttonX, buttonY, "Save", "editor:save", true, false, m,
                         theme, text) +
               gap;
    addButton(scene, buttonX, buttonY, "Cancel", "editor:cancel", false, false, m, theme,
              text);

    return scene;
}

} // namespace klend_ario::gui
