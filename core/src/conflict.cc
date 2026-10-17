#include "klend_ario/conflict.hh"

#include <algorithm>
#include <utility>

namespace klend_ario {
namespace {

bool byStart(const Event *a, const Event *b) {
    if (a->startUtc != b->startUtc) {
        return a->startUtc < b->startUtc;
    }
    return a->id < b->id;
}

bool byConflict(const Conflict &a, const Conflict &b) {
    if (a.startUtc != b.startUtc) {
        return a.startUtc < b.startUtc;
    }
    if (a.firstId != b.firstId) {
        return a.firstId < b.firstId;
    }
    return a.secondId < b.secondId;
}

} // namespace

std::vector<Conflict> findConflicts(const std::vector<Event> &events) {
    // Sweep by start, keeping the events that are still running. Every pair is
    // examined exactly once, and a pair whose overlap is empty (touching, or a
    // zero-length event) is dropped by the `start < end` test.
    std::vector<const Event *> ordered;
    ordered.reserve(events.size());
    for (const Event &event : events) {
        if (event.type != EventType::Alarm) {
            ordered.push_back(&event);
        }
    }
    std::sort(ordered.begin(), ordered.end(), byStart);

    std::vector<const Event *> active;
    std::vector<Conflict> conflicts;

    for (const Event *current : ordered) {
        active.erase(std::remove_if(active.begin(), active.end(),
                                    [current](const Event *other) {
                                        return other->endUtc <= current->startUtc;
                                    }),
                     active.end());

        for (const Event *other : active) {
            const long long startUtc = std::max(other->startUtc, current->startUtc);
            const long long endUtc = std::min(other->endUtc, current->endUtc);
            if (startUtc >= endUtc) {
                continue;
            }

            const Event *first = other;
            const Event *second = current;
            if (first->startUtc > second->startUtc ||
                (first->startUtc == second->startUtc && first->id > second->id)) {
                std::swap(first, second);
            }
            conflicts.push_back(Conflict{first->id, first->title, second->id, second->title,
                                         startUtc, endUtc});
        }
        active.push_back(current);
    }

    std::sort(conflicts.begin(), conflicts.end(), byConflict);
    return conflicts;
}

} // namespace klend_ario
