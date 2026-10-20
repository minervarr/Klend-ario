#include "refresh.hh"

namespace klend_ario::gui {

Refresh refreshDecision(const RefreshInputs &in) {
    // A commit by another process (CLI or MCP) beats every other reason: the
    // rows themselves are stale, so a redraw would only repaint old data.
    if (in.dataChanged) {
        return Refresh::Reload;
    }

    // Half-open: now == weekEndUtc belongs to NEXT week, and a day ends where
    // the next begins.
    const bool nowInWeek = in.nowUtc >= in.weekStartUtc && in.nowUtc < in.weekEndUtc;

    if (!nowInWeek) {
        if (in.followingNow) {
            // The current week changed under a view that follows it. Reloading
            // is not enough on its own — the caller also moves the anchor to
            // now, or it would reload the OLD week.
            return Refresh::Reload;
        }
        if (in.weekHadToday) {
            // Browsing, and the shown week was the current one until the clock
            // crossed out of it. Its "today" tint and now-line are now wrong;
            // one reload clears them and the next tick is quiet again.
            return Refresh::Reload;
        }
        // Browsing a week that contains no "now": nothing on screen can change,
        // so no frame. This is the acceptance case — not even the minute tick
        // applies, because there is no now-line to move.
        return Refresh::None;
    }

    // Inside the shown week, but no loaded day still contains now: local
    // midnight moved it into a day the model has not marked yet. (A DST day is
    // still one local day; the bounds come from the model, not from 86,400.)
    if (!in.shownDayIsToday) {
        return Refresh::Reload;
    }

    // A backwards jump (NTP correction, a manual clock change) must repaint
    // even though the elapsed check below would be negative. The line has
    // visibly moved up.
    if (in.nowUtc < in.drawnNowUtc) {
        return Refresh::Redraw;
    }

    if (in.nowUtc - in.drawnNowUtc >= kNowLineRefreshSeconds) {
        return Refresh::Redraw;
    }

    return Refresh::None;
}

} // namespace klend_ario::gui
