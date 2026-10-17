#include "klend_ario/query.hh"

namespace klend_ario {

const char *eventSortName(EventSort sort) {
    switch (sort) {
    case EventSort::Start:
        return "start";
    case EventSort::StartDesc:
        return "-start";
    case EventSort::Importance:
        return "importance";
    case EventSort::Urgency:
        return "urgency";
    case EventSort::Updated:
        return "updated";
    case EventSort::Title:
        return "title";
    }
    return "start";
}

bool eventSortFromName(std::string_view name, EventSort &out) {
    if (name == "start") {
        out = EventSort::Start;
        return true;
    }
    if (name == "-start") {
        out = EventSort::StartDesc;
        return true;
    }
    if (name == "importance") {
        out = EventSort::Importance;
        return true;
    }
    if (name == "urgency") {
        out = EventSort::Urgency;
        return true;
    }
    if (name == "updated") {
        out = EventSort::Updated;
        return true;
    }
    if (name == "title") {
        out = EventSort::Title;
        return true;
    }
    return false;
}

} // namespace klend_ario
