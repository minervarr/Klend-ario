#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include "editor.hh"

using namespace klend_ario;
using namespace klend_ario::gui;

static void useZone(const char *zone) {
    setenv("TZ", zone, 1);
    tzset();
}

// 2026-11-05T08:00:00Z = 09:00 in Berlin (CET, UTC+1).
static constexpr long long kNineLocal = 1793865600;

static Event makeEvent() {
    Event event;
    event.id = 7;
    event.revision = 3;
    event.title = "Standup";
    event.startUtc = kNineLocal;
    event.endUtc = kNineLocal + 3600;
    event.type = EventType::Sleep;
    event.importance = 80;
    event.urgency = 20;
    event.alarmOffset = 600;
    event.hidden = true;
    event.notes = "bring notes";
    return event;
}

static void testEventRoundTripsThroughTheForm() {
    useZone("Europe/Berlin");
    const EditorState state = editorForEvent(makeEvent());
    assert(state.open && !state.isNew);
    assert(state.id == 7 && state.revision == 3);
    assert(state.title == "Standup");
    assert(state.start == "2026-11-05 09:00");
    assert(state.end == "2026-11-05 10:00");
    assert(state.type == "sleep");
    assert(state.importance == "80");
    assert(state.urgency == "20");
    assert(state.alarmOffset == "600");
    assert(state.hidden);
    assert(state.notes == "bring notes");

    // The stored instant stays visible: 09:00 Berlin is 08:00Z.
    assert(editorUtcHint(state, EditorField::Start) == "2026-11-05 08:00Z");
    assert(editorUtcHint(state, EditorField::End) == "2026-11-05 09:00Z");
    assert(editorUtcHint(state, EditorField::Title).empty());

    std::string warning;
    auto built = buildEventFromEditor(state, warning);
    assert(built.ok());
    assert(warning.empty());
    const Event &event = built.value();
    assert(event.id == 7 && event.revision == 3);
    assert(event.title == "Standup");
    assert(event.startUtc == kNineLocal);
    assert(event.endUtc == kNineLocal + 3600);
    assert(event.type == EventType::Sleep);
    assert(event.importance == 80 && event.urgency == 20);
    assert(event.alarmOffset == 600);
    assert(event.hidden);
    assert(event.notes == "bring notes");
    // The storage layer stamps these; the form must not invent them.
    assert(event.createdUtc == 0 && event.updatedUtc == 0);
}

static void testNewEventDefaultsToOneHour() {
    useZone("Europe/Berlin");
    const EditorState state = editorForNew(kNineLocal);
    assert(state.open && state.isNew);
    assert(state.id == 0 && state.revision == 0);
    assert(state.start == "2026-11-05 09:00");
    assert(state.end == "2026-11-05 10:00");
    assert(state.type == "event");
    assert(state.importance == "50" && state.urgency == "50");
    assert(state.alarmOffset == "0");
    assert(!state.hidden);

    std::string warning;
    auto built = buildEventFromEditor(state, warning);
    assert(!built.ok()); // no title yet
    assert(built.error().message == "title must not be empty");
}

static void testEditsAreParsedIntoTheEvent() {
    useZone("Europe/Berlin");
    EditorState state = editorForNew(kNineLocal);
    state.title = "Deep work";
    state.start = "2026-11-05 14:30";
    state.end = "2026-11-05 16:00";
    state.type = "wake";
    state.importance = "100";
    state.urgency = "0";
    state.alarmOffset = "-300";
    state.hidden = true;
    state.notes = "two lines\nallowed";

    std::string warning;
    auto built = buildEventFromEditor(state, warning);
    assert(built.ok());
    const Event &event = built.value();
    assert(event.title == "Deep work");
    assert(event.startUtc == 1793885400); // 2026-11-05T13:30:00Z
    assert(event.endUtc == 1793890800);   // 2026-11-05T15:00:00Z
    assert(event.type == EventType::Wake);
    assert(event.importance == 100 && event.urgency == 0);
    assert(event.alarmOffset == -300);
    assert(event.hidden);
    assert(event.notes == "two lines\nallowed");
}

static void testValidationRejectsBadFields() {
    useZone("Europe/Berlin");
    EditorState base = editorForNew(kNineLocal);
    base.title = "X";
    std::string warning;

    EditorState bad = base;
    bad.title.clear();
    assert(!buildEventFromEditor(bad, warning).ok());

    bad = base;
    bad.start = "not a time";
    auto result = buildEventFromEditor(bad, warning);
    assert(!result.ok());
    assert(result.error().message.rfind("start: ", 0) == 0);

    bad = base;
    bad.end = "2026-11-05 08:00"; // before the 09:00 start
    result = buildEventFromEditor(bad, warning);
    assert(!result.ok());
    assert(result.error().message == "end must be at or after start");

    bad = base;
    bad.importance = "101";
    assert(!buildEventFromEditor(bad, warning).ok());

    bad = base;
    bad.urgency = "-1";
    assert(!buildEventFromEditor(bad, warning).ok());

    bad = base;
    bad.type = "meeting";
    assert(!buildEventFromEditor(bad, warning).ok());

    bad = base;
    bad.alarmOffset = "10m"; // whole seconds, matching the CLI/MCP field
    assert(!buildEventFromEditor(bad, warning).ok());

    bad = base;
    bad.start = "2026-02-30 09:00"; // February has no 30th
    assert(!buildEventFromEditor(bad, warning).ok());
}

static void testDstTimesAreResolvedNotGuessed() {
    useZone("Europe/Berlin");
    EditorState base = editorForNew(1774744200); // 2026-03-29T00:30Z
    base.title = "DST";

    // Spring forward: 02:30 does not exist.
    EditorState spring = base;
    spring.start = "2026-03-29 02:30";
    spring.end = "2026-03-29 04:00";
    std::string warning;
    auto result = buildEventFromEditor(spring, warning);
    assert(!result.ok());
    assert(result.error().message.find("does not exist") != std::string::npos);

    // Fall back: 02:30 happens twice; the earlier occurrence is chosen and the
    // ambiguity is reported, not hidden.
    EditorState fall = base;
    fall.start = "2026-10-25 02:30";
    fall.end = "2026-10-25 04:00";
    result = buildEventFromEditor(fall, warning);
    assert(result.ok());
    assert(result.value().startUtc == 1792888200); // 02:30 CEST = 00:30Z
    assert(!warning.empty());
    assert(warning.find("earlier") != std::string::npos);
}

static void testUnchangedAmbiguousFieldKeepsItsInstant() {
    useZone("Europe/Berlin");
    Event event = makeEvent();
    event.startUtc = 1792891800; // the SECOND 02:30 on 2026-10-25 (CET)
    event.endUtc = 1792897200;   // 04:00 CET
    const EditorState state = editorForEvent(event);
    assert(state.start == "2026-10-25 02:30");

    std::string warning;
    auto built = buildEventFromEditor(state, warning);
    assert(built.ok());
    // The text is unchanged and ambiguous. Re-parsing would choose the earlier
    // 02:30 and move the event an hour; the original instant must survive an
    // unrelated edit.
    assert(built.value().startUtc == 1792891800);
    assert(built.value().endUtc == 1792897200);
    assert(warning.empty());
    // The hint must agree with Save, not with a fresh parse of the text.
    assert(editorUtcHint(state, EditorField::Start) == "2026-10-25 01:30Z");
}

static void testUtcHintFollowsTheField() {
    useZone("UTC0");
    EditorState state = editorForNew(1793865600);
    assert(editorUtcHint(state, EditorField::Start) == "2026-11-05 08:00Z");

    state.start = "2026-11-05 09:30";
    assert(editorUtcHint(state, EditorField::Start) == "2026-11-05 09:30Z");

    // A malformed or nonexistent field has no UTC answer; the scene draws
    // "UTC --" rather than a stale value.
    state.start = "not a time";
    assert(editorUtcHint(state, EditorField::Start).empty());

    useZone("Europe/Berlin");
    state.start = "2026-03-29 02:30"; // skipped by spring forward
    assert(editorUtcHint(state, EditorField::Start).empty());
}

static void testFieldOrder() {
    assert(std::string(editorFieldName(EditorField::Title)) == "title");
    assert(editorFieldNext(EditorField::Title, false) == EditorField::Start);
    assert(editorFieldNext(EditorField::Start, true) == EditorField::Title);
    assert(editorFieldNext(EditorField::Cancel, false) == EditorField::Title);
    assert(editorFieldNext(EditorField::Title, true) == EditorField::Cancel);

    EditorField field = EditorField::Cancel;
    assert(editorFieldFromName("title", field) && field == EditorField::Title);
    assert(editorFieldFromName("alarm_offset", field) &&
           field == EditorField::AlarmOffset);
    assert(!editorFieldFromName("nope", field));
    assert(editorFieldIsText(EditorField::Title));
    assert(editorFieldIsText(EditorField::Notes));
    assert(!editorFieldIsText(EditorField::Hidden));
    assert(!editorFieldIsText(EditorField::Save));
}

int main() {
    testEventRoundTripsThroughTheForm();
    testNewEventDefaultsToOneHour();
    testEditsAreParsedIntoTheEvent();
    testValidationRejectsBadFields();
    testDstTimesAreResolvedNotGuessed();
    testUnchangedAmbiguousFieldKeepsItsInstant();
    testUtcHintFollowsTheField();
    testFieldOrder();
    std::printf("editor_test: all assertions passed\n");
    return 0;
}
