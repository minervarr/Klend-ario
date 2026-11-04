#pragma once

// The weekly view's edit form, as pure state.
//
// The window owns focus, text input and the click loop; this file owns the
// MEANING of the fields: how an Event becomes editable text, how that text is
// parsed back, which local wall clocks are invalid, and what a valid draft
// becomes. Keeping it here means the whole form can be asserted with a pinned
// TZ and no GPU — the same reason week_layout and scene are separate from the
// renderer.
//
// Times are LOCAL because that is what a human types. The model stores UTC;
// localTimeToUtc is the one bridge, and it reports DST ambiguity/nonexistence
// instead of silently choosing. The UTC instant is still drawn under each time
// field, resolved by the same function the save uses, so the stored value is
// never hidden behind a timezone conversion.

#include <string>
#include <string_view>

#include "arc/result.hh"
#include "klend_ario/event.hh"

namespace klend_ario::gui {

enum class EditorField {
    Title,
    Start,
    End,
    Type,
    Importance,
    Urgency,
    AlarmOffset,
    Hidden,
    Notes,
    Save,
    Delete,
    Cancel,
    Count,
};

const char *editorFieldName(EditorField field);
bool editorFieldFromName(std::string_view name, EditorField &out);
EditorField editorFieldNext(EditorField field, bool backward);
bool editorFieldIsText(EditorField field);

struct EditorState {
    bool open = false;
    bool isNew = false;
    long long id = 0;
    long long revision = 0;
    // The instants the form was opened with. If the visible text is unchanged,
    // saving keeps these exact values: a fall-back wall clock is ambiguous, and
    // re-parsing it would silently move the event to the other occurrence.
    long long originalStartUtc = 0;
    long long originalEndUtc = 0;
    EditorField focus = EditorField::Title;

    std::string title;
    std::string start; // local "YYYY-MM-DD HH:MM"
    std::string end;   // local "YYYY-MM-DD HH:MM"
    std::string type;  // event | sleep | wake | alarm
    std::string importance;  // 0..100
    std::string urgency;     // 0..100
    std::string alarmOffset; // seconds, may be negative
    bool hidden = false;
    std::string notes;

    std::string error;   // last parse/validation failure; cleared on success
    std::string warning; // e.g. a DST-ambiguous time; saving is still allowed
    bool confirmDelete = false; // Delete pressed once: ask before destroying
    // A DST-ambiguous time needs an explicit second Save; editing a field
    // clears this so a changed time is warned about again.
    bool acceptAmbiguous = false;
};

// A new event starting at `startUtc`, one hour long. The caller has already
// chosen the slot (clicked a day column, pressed N, ...).
EditorState editorForNew(long long startUtc);

// The editable form for an existing row, including its revision token.
EditorState editorForEvent(const klend_ario::Event &event);

// Parses and validates the form. `warning` is cleared first and may receive a
// DST-ambiguity note. On success the returned Event carries id/revision from
// the state; created/updated stay 0 for the storage layer to stamp.
arc::Result<klend_ario::Event> buildEventFromEditor(const EditorState &state,
                                                    std::string &warning);

// "YYYY-MM-DD HH:MM" in local time. Exposed here so the test and the future
// scene use exactly the same shape.
std::string formatEditorInstant(long long utcSeconds);

// The UTC instant a local Start/End field currently resolves to, as
// "YYYY-MM-DD HH:MMZ". Empty for another field, a malformed value or a
// nonexistent DST wall clock. It reuses buildEventFromEditor's resolution,
// including "an unchanged ambiguous field keeps the instant it was opened at",
// so the hint can never disagree with what Save would store.
std::string editorUtcHint(const EditorState &state, EditorField field);

} // namespace klend_ario::gui
