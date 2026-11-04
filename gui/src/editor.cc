#include "editor.hh"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string_view>

#include "local_time.hh"

namespace klend_ario::gui {
namespace {

const char *kFields[] = {"title",  "start", "end",   "type",       "importance",
                         "urgency", "alarm", "hidden", "notes",     "save",
                         "delete", "cancel"};

int daysInMonth(int year, int month) {
    static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) {
        return 29;
    }
    return kDays[month - 1];
}

bool parseInteger(std::string_view text, long long minimum, long long maximum,
                  long long &out) {
    if (text.empty()) {
        return false;
    }
    const std::string copy(text);
    errno = 0;
    char *end = nullptr;
    const long long value = std::strtoll(copy.c_str(), &end, 10);
    if (errno == ERANGE || end == copy.c_str() || *end != '\0') {
        return false;
    }
    if (value < minimum || value > maximum) {
        return false;
    }
    out = value;
    return true;
}

// Parse the editor's local shape. Returns false with `why` on a malformed or
// nonexistent wall clock; `ambiguous` is an advisory (the earlier occurrence is
// used) rather than an error.
bool parseEditorInstant(std::string_view text, long long &out, bool &ambiguous,
                        std::string &why) {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int consumed = 0;
    const std::string copy(text);
    int fields = std::sscanf(copy.c_str(), "%d-%d-%d%*[ T]%d:%d:%d%n", &year, &month,
                             &day, &hour, &minute, &second, &consumed);
    if (fields != 6) {
        consumed = 0;
        second = 0;
        fields = std::sscanf(copy.c_str(), "%d-%d-%d%*[ T]%d:%d%n", &year, &month, &day,
                             &hour, &minute, &consumed);
        if (fields != 5) {
            why = "use YYYY-MM-DD HH:MM";
            return false;
        }
    }
    if (copy[static_cast<size_t>(consumed)] != '\0') {
        why = "trailing characters; use YYYY-MM-DD HH:MM";
        return false;
    }
    if (year < 1970 || year > 9999 || month < 1 || month > 12 || day < 1 ||
        day > daysInMonth(year, month) || hour < 0 || hour > 23 || minute < 0 ||
        minute > 59 || second < 0 || second > 59) {
        why = "the date or time is out of range";
        return false;
    }

    const LocalResolution resolved =
        localTimeToUtc(year, month, day, hour, minute, second);
    if (resolved.nonexistent) {
        why = "that local time does not exist (the clock jumps forward)";
        return false;
    }
    ambiguous = resolved.ambiguous;
    out = resolved.utc;
    return true;
}

} // namespace

const char *editorFieldName(EditorField field) {
    const auto index = static_cast<size_t>(field);
    if (index < sizeof(kFields) / sizeof(kFields[0])) {
        return kFields[index];
    }
    return "";
}

EditorField editorFieldNext(EditorField field, bool backward) {
    const int count = static_cast<int>(EditorField::Count);
    int index = static_cast<int>(field);
    index += backward ? -1 : 1;
    if (index < 0) {
        index = count - 1;
    }
    if (index >= count) {
        index = 0;
    }
    return static_cast<EditorField>(index);
}

std::string formatEditorInstant(long long utcSeconds) {
    return formatDateTimeLocal(utcSeconds);
}

EditorState editorForNew(long long startUtc) {
    EditorState state;
    state.open = true;
    state.isNew = true;
    state.focus = EditorField::Title;
    state.start = formatEditorInstant(startUtc);
    state.end = formatEditorInstant(startUtc + 3600);
    state.type = "event";
    state.importance = "50";
    state.urgency = "50";
    state.alarmOffset = "0";
    return state;
}

EditorState editorForEvent(const klend_ario::Event &event) {
    EditorState state;
    state.open = true;
    state.isNew = false;
    state.id = event.id;
    state.revision = event.revision;
    state.originalStartUtc = event.startUtc;
    state.originalEndUtc = event.endUtc;
    state.focus = EditorField::Title;
    state.title = event.title;
    state.start = formatEditorInstant(event.startUtc);
    state.end = formatEditorInstant(event.endUtc);
    state.type = klend_ario::eventTypeName(event.type);
    state.importance = std::to_string(event.importance);
    state.urgency = std::to_string(event.urgency);
    state.alarmOffset = std::to_string(event.alarmOffset);
    state.hidden = event.hidden;
    state.notes = event.notes;
    return state;
}

arc::Result<klend_ario::Event> buildEventFromEditor(const EditorState &state,
                                                    std::string &warning) {
    warning.clear();

    if (state.title.empty()) {
        return arc::Error{arc::ErrorKind::Other, 0, "title must not be empty"};
    }

    long long startUtc = 0;
    long long endUtc = 0;
    bool startAmbiguous = false;
    bool endAmbiguous = false;
    std::string why;

    if (!parseEditorInstant(state.start, startUtc, startAmbiguous, why)) {
        return arc::Error{arc::ErrorKind::Other, 0, "start: " + why};
    }
    if (!parseEditorInstant(state.end, endUtc, endAmbiguous, why)) {
        return arc::Error{arc::ErrorKind::Other, 0, "end: " + why};
    }
    // An unchanged ambiguous field keeps the instant it was opened at. The
    // text alone cannot distinguish the two 02:30s, and re-parsing would move
    // the event by an hour on an unrelated title edit.
    if (state.originalStartUtc != 0 &&
        state.start == formatEditorInstant(state.originalStartUtc)) {
        startUtc = state.originalStartUtc;
        startAmbiguous = false;
    }
    if (state.originalEndUtc != 0 &&
        state.end == formatEditorInstant(state.originalEndUtc)) {
        endUtc = state.originalEndUtc;
        endAmbiguous = false;
    }
    if (endUtc < startUtc) {
        return arc::Error{arc::ErrorKind::Other, 0, "end must be at or after start"};
    }

    klend_ario::EventType type = klend_ario::EventType::Event;
    if (!klend_ario::eventTypeFromName(state.type, type)) {
        return arc::Error{arc::ErrorKind::Other, 0,
                          "type must be event, sleep, wake or alarm"};
    }

    long long importance = 0;
    if (!parseInteger(state.importance, 0, 100, importance)) {
        return arc::Error{arc::ErrorKind::Other, 0, "importance must be 0..100"};
    }
    long long urgency = 0;
    if (!parseInteger(state.urgency, 0, 100, urgency)) {
        return arc::Error{arc::ErrorKind::Other, 0, "urgency must be 0..100"};
    }
    long long alarmOffset = 0;
    if (!parseInteger(state.alarmOffset, std::numeric_limits<int>::min(),
                      std::numeric_limits<int>::max(), alarmOffset)) {
        return arc::Error{arc::ErrorKind::Other, 0,
                          "alarm offset must be whole seconds"};
    }

    klend_ario::Event event;
    event.id = state.id;
    event.revision = state.revision;
    event.title = state.title;
    event.startUtc = startUtc;
    event.endUtc = endUtc;
    event.type = type;
    event.importance = static_cast<int>(importance);
    event.urgency = static_cast<int>(urgency);
    event.alarmOffset = static_cast<int>(alarmOffset);
    event.hidden = state.hidden;
    event.notes = state.notes;

    if (startAmbiguous || endAmbiguous) {
        warning = (startAmbiguous && endAmbiguous)
                      ? "start and end are ambiguous; using the earlier occurrences"
                      : (startAmbiguous ? "start is ambiguous; using the earlier occurrence"
                                        : "end is ambiguous; using the earlier occurrence");
    }
    return event;
}

} // namespace klend_ario::gui
