#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <string_view>

#include "editor_scene.hh"

using namespace klend_ario;
using namespace klend_ario::gui;

class FakeMetrics : public TextMetrics {
public:
    int width(std::string_view text, int sizePx) const override {
        return (sizePx * static_cast<int>(text.size())) / 2;
    }
};

static const HitTarget *findHit(const Scene &scene, std::string_view id) {
    for (const HitTarget &hit : scene.hits) {
        if (hit.id == id) {
            return &hit;
        }
    }
    return nullptr;
}

static const Op *findText(const Scene &scene, std::string_view text) {
    for (const Op &op : scene.ops) {
        if (op.kind == OpKind::Text && op.text == text) {
            return &op;
        }
    }
    return nullptr;
}

static bool hasStrokeOn(const Scene &scene, const Rect &rect, const Color &color,
                        int thickness) {
    for (const Op &op : scene.ops) {
        if (op.kind == OpKind::Stroke && op.rect.x == rect.x && op.rect.y == rect.y &&
            op.rect.w == rect.w && op.rect.h == rect.h && op.color.r == color.r &&
            op.color.g == color.g && op.color.b == color.b &&
            op.thickness == thickness) {
            return true;
        }
    }
    return false;
}

static Scene build(const EditorState &state) {
    const FakeMetrics measure;
    return buildEditorScene(900, 700, state, measure);
}

static Event editEvent() {
    Event event;
    event.id = 7;
    event.revision = 2;
    event.title = "Deep work";
    event.startUtc = 1793865600; // 2026-11-05T08:00Z
    event.endUtc = 1793869200;   // 09:00Z
    return event;
}

static void testNewEventChrome() {
    setenv("TZ", "UTC0", 1);
    tzset();

    const EditorState state = editorForNew(1793865600);
    const Scene scene = build(state);

    // A dim full-window fill is first; the panel and its title follow.
    assert(scene.ops.front().kind == OpKind::Fill);
    assert(scene.ops.front().rect.x == 0 && scene.ops.front().rect.y == 0);
    assert(scene.ops.front().rect.w == 900 && scene.ops.front().rect.h == 700);
    assert(scene.ops.front().color.a < 255); // translucent, not an opaque wall
    assert(findText(scene, "New event") != nullptr);

    // Every editable field is a target, including the toggle.
    const char *fields[] = {"field:title",      "field:start", "field:end",
                            "field:type",       "field:importance",
                            "field:urgency",    "field:alarm_offset",
                            "field:hidden",     "field:notes"};
    for (const char *id : fields) {
        assert(findHit(scene, id) != nullptr);
    }

    // A new event has Save and Cancel, but nothing to delete.
    assert(findHit(scene, "editor:save") != nullptr);
    assert(findHit(scene, "editor:cancel") != nullptr);
    assert(findHit(scene, "editor:delete") == nullptr);

    // The values the form opened with are drawn, not only stored.
    assert(findText(scene, "2026-11-05 08:00") != nullptr);
    assert(findText(scene, "2026-11-05 09:00") != nullptr);
    assert(findText(scene, "event") != nullptr);
    assert(findText(scene, "50") != nullptr);

    // UTC is visible under each local time field, in the dim annotation colour,
    // and below the field rather than over it.
    const HitTarget *startField = findHit(scene, "field:start");
    const Op *startHint = findText(scene, "UTC 2026-11-05 08:00Z");
    const Op *endHint = findText(scene, "UTC 2026-11-05 09:00Z");
    assert(startField != nullptr && startHint != nullptr && endHint != nullptr);
    assert(startHint->color.r == defaultTheme().dimText.r);
    assert(startHint->rect.y >= startField->rect.y + startField->rect.h);
}

static void testUtcHintTracksTheField() {
    setenv("TZ", "UTC0", 1);
    tzset();

    EditorState state = editorForNew(1793865600);
    state.start = "2026-11-05 10:15";
    Scene scene = build(state);
    assert(findText(scene, "UTC 2026-11-05 10:15Z") != nullptr);
    assert(findText(scene, "UTC 2026-11-05 09:00Z") != nullptr); // end unchanged

    state.start = "nope";
    scene = build(state);
    assert(findText(scene, "UTC --") != nullptr);
}

static void testFocusRingAndHiddenToggle() {
    EditorState state = editorForNew(1793865600);
    state.focus = EditorField::Title;
    Scene scene = build(state);

    const HitTarget *title = findHit(scene, "field:title");
    assert(title != nullptr);
    assert(hasStrokeOn(scene, title->rect, defaultTheme().selection, 2));

    state.focus = EditorField::Hidden;
    state.hidden = true;
    scene = build(state);
    const HitTarget *check = findHit(scene, "field:hidden");
    assert(check != nullptr);
    assert(hasStrokeOn(scene, check->rect, defaultTheme().selection, 2));
    // The check mark uses the background colour on a selection-filled box.
    bool filled = false;
    bool mark = false;
    for (const Op &op : scene.ops) {
        if (op.kind == OpKind::Fill && op.rect.x == check->rect.x &&
            op.rect.y == check->rect.y && op.color.r == defaultTheme().selection.r) {
            filled = true;
        }
        if (op.kind == OpKind::Text && op.text == "x") {
            mark = true;
        }
    }
    assert(filled && mark);
}

static void testEditEventAddsDeleteAndConfirm() {
    setenv("TZ", "UTC0", 1);
    tzset();

    EditorState state = editorForEvent(editEvent());
    Scene scene = build(state);
    assert(findText(scene, "Edit event 7") != nullptr);
    assert(findHit(scene, "editor:delete") != nullptr);
    assert(findText(scene, "Deep work") != nullptr);

    state.confirmDelete = true;
    scene = build(state);
    assert(findText(scene, "Confirm delete") != nullptr);
    assert(findText(scene,
                    "Delete this event? Click Delete again or press Enter.") != nullptr);
}

static void testErrorAndWarningAreVisible() {
    EditorState state = editorForNew(1793865600);
    state.error = "end must be at or after start";
    Scene scene = build(state);
    const Op *error = findText(scene, "end must be at or after start");
    assert(error != nullptr);
    assert(error->color.r == defaultTheme().errorText.r);

    state.error.clear();
    state.warning = "start is ambiguous; using the earlier occurrence";
    scene = build(state);
    const Op *warning = findText(scene, "start is ambiguous; using the earlier occurrence");
    assert(warning != nullptr);
    assert(warning->color.g == defaultTheme().holidayAccent.g);
}

int main() {
    testNewEventChrome();
    testUtcHintTracksTheField();
    testFocusRingAndHiddenToggle();
    testEditEventAddsDeleteAndConfirm();
    testErrorAndWarningAreVisible();
    std::printf("editor_scene_test: all assertions passed\n");
    return 0;
}
