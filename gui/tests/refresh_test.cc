#undef NDEBUG
#include <cassert>
#include <cstdio>

#include "refresh.hh"

using namespace klend_ario::gui;

// A Sunday 00:00 UTC .. next Sunday 00:00 UTC, and "now" three hours into it.
static constexpr long long kWeekStart = 1700000000;
static constexpr long long kWeekEnd = kWeekStart + 7 * 86400;
static constexpr long long kNow = kWeekStart + 3 * 3600;

// The steady state inside the shown week: today is marked, the scene was drawn
// at this same instant, the clock follows now.
static RefreshInputs inWeek() {
    RefreshInputs in;
    in.nowUtc = kNow;
    in.drawnNowUtc = kNow;
    in.weekStartUtc = kWeekStart;
    in.weekEndUtc = kWeekEnd;
    in.shownDayIsToday = true;
    in.weekHadToday = true;
    in.followingNow = true;
    return in;
}

static void testQuietSteadyState() {
    // Nothing moved, nothing to do. This is the case the old once-a-second
    // repaint paid for unconditionally.
    assert(refreshDecision(inWeek()) == Refresh::None);
}

static void testNowLineCadence() {
    // Sub-pixel movement is not a frame: 59 s is still within one pixel at
    // every realistic body height.
    RefreshInputs in = inWeek();
    in.nowUtc = kNow + 59;
    assert(refreshDecision(in) == Refresh::None);

    // At the documented cadence the line is due.
    in.nowUtc = kNow + kNowLineRefreshSeconds;
    assert(refreshDecision(in) == Refresh::Redraw);

    // Well past it (a suspended laptop) still redraws once, not repeatedly.
    in.nowUtc = kNow + 3600;
    assert(refreshDecision(in) == Refresh::Redraw);
}

static void testClockWentBackwards() {
    // NTP correction or a manual change: the elapsed test would be negative,
    // but the line has visibly moved UP and must be repainted.
    RefreshInputs in = inWeek();
    in.nowUtc = kNow - 1;
    assert(refreshDecision(in) == Refresh::Redraw);
}

static void testDataChangeWins() {
    RefreshInputs in = inWeek();
    in.dataChanged = true;
    assert(refreshDecision(in) == Refresh::Reload);

    // Even while browsing a week where nothing else would draw, and even
    // before a minute elapsed: the ROWS are wrong, not the now-line.
    in = inWeek();
    in.nowUtc = kWeekStart - 7 * 86400;
    in.shownDayIsToday = false;
    in.weekHadToday = false;
    in.followingNow = false;
    in.dataChanged = true;
    assert(refreshDecision(in) == Refresh::Reload);
}

static void testBrowsingAnotherWeek() {
    // Looking at last week: no day in the model is today, the scene has no
    // now-line, and no amount of elapsed time changes a pixel.
    RefreshInputs in = inWeek();
    in.nowUtc = kWeekStart - 7 * 86400;
    in.drawnNowUtc = in.nowUtc;
    in.shownDayIsToday = false;
    in.weekHadToday = false;
    in.followingNow = false;
    assert(refreshDecision(in) == Refresh::None);

    in.nowUtc += 3600;
    assert(refreshDecision(in) == Refresh::None);

    // Next week has no "now" either.
    in.nowUtc = kWeekEnd + 7 * 86400;
    in.drawnNowUtc = in.nowUtc;
    assert(refreshDecision(in) == Refresh::None);
}

static void testWeekRolloverFollowingNow() {
    // Sunday 00:00: now left the shown week while the view follows it. The
    // caller moves the anchor and reloads.
    RefreshInputs in = inWeek();
    in.nowUtc = kWeekEnd;
    in.shownDayIsToday = false;
    in.weekHadToday = false;
    assert(refreshDecision(in) == Refresh::Reload);
}

static void testWeekRolloverWhileBrowsing() {
    // The shown week was current, then the clock crossed out of it while the
    // user was browsing it. The stale today tint and now-line must be cleared
    // once — but the anchor must not jump (the caller decides that, and this
    // says nothing about moving it).
    RefreshInputs in = inWeek();
    in.nowUtc = kWeekEnd;
    in.drawnNowUtc = kWeekEnd - 60;
    in.shownDayIsToday = false; // the flagged day no longer contains now
    in.weekHadToday = true;     // ...but a day is still flagged
    in.followingNow = false;
    assert(refreshDecision(in) == Refresh::Reload);

    // After that reload the model has no today flag, so the next tick is quiet.
    in.weekHadToday = false;
    assert(refreshDecision(in) == Refresh::None);
}

static void testLocalMidnight() {
    // Inside the shown week, past local midnight: the loaded model still flags
    // yesterday. Reloading is what moves the tint and the line to the new day.
    RefreshInputs in = inWeek();
    in.nowUtc = kNow + 86400;
    in.shownDayIsToday = false;
    in.weekHadToday = true; // stale: the flagged day ended
    assert(refreshDecision(in) == Refresh::Reload);
}

static void testHalfOpenWeek() {
    // now == weekEnd is NEXT week, not this one.
    RefreshInputs in = inWeek();
    in.nowUtc = kWeekEnd;
    in.shownDayIsToday = false;
    in.weekHadToday = false;
    in.followingNow = false;
    assert(refreshDecision(in) == Refresh::None);
    in.followingNow = true;
    assert(refreshDecision(in) == Refresh::Reload);

    // now == weekStart is inside.
    in = inWeek();
    in.nowUtc = kWeekStart;
    in.drawnNowUtc = kWeekStart;
    assert(refreshDecision(in) == Refresh::None);
}

static void testNoTodayFlagInsideWeek() {
    // A model loaded before midnight for a week whose days are not yet marked:
    // inside the week but nothing is today. Reload, never redraw — a redraw
    // would draw the same stale week.
    RefreshInputs in = inWeek();
    in.shownDayIsToday = false;
    in.weekHadToday = false;
    assert(refreshDecision(in) == Refresh::Reload);
}

int main() {
    testQuietSteadyState();
    testNowLineCadence();
    testClockWentBackwards();
    testDataChangeWins();
    testBrowsingAnotherWeek();
    testWeekRolloverFollowingNow();
    testWeekRolloverWhileBrowsing();
    testLocalMidnight();
    testHalfOpenWeek();
    testNoTodayFlagInsideWeek();
    std::puts("refresh_test: ok");
    return 0;
}
