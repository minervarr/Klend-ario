#pragma once

// When the weekly view owes the screen a frame, and how much of a rebuild.
//
// The window polls the database once a second (that is the product's "an AI
// edit appears live" contract) and the clock moves continuously, but neither is
// a reason to REPAINT. Drawing a full-screen frame on a software rasterizer
// costs hundreds of milliseconds; doing it once a second over content that did
// not change is the single most expensive thing the GUI did (see
// GUI_PERFORMANCE.md §3). This is that decision, kept pure so it is tested
// without a window, a clock or a GPU — the same reason the scene and the
// layout live in `klend_ario_view`.
//
// Three answers, because they cost very different things:
//   None    nothing visible changed; do not touch the GPU
//   Redraw  same week and rows, the now-line moved; rebuild the scene
//   Reload  the week model itself is stale (rows, midnight, week rollover);
//           query the database again, which arms a frame

namespace klend_ario::gui {

enum class Refresh { None, Redraw, Reload };

struct RefreshInputs {
    long long nowUtc = 0;
    long long drawnNowUtc = 0; // the `now` the last built scene used
    long long weekStartUtc = 0; // week_.days[0].startUtc
    long long weekEndUtc = 0;   // week_.days[6].endUtc (exclusive)
    // A day the loaded model marked `isToday` still contains nowUtc. After
    // local midnight the flag survives but the containment does not, which is
    // exactly how "today" is detected as stale.
    bool shownDayIsToday = false;
    // Any loaded day is flagged `isToday`, whether or not it still contains
    // now. Used only for a week that rolled over while it was on screen: its
    // now-line and today tint are now wrong and one frame must clear them.
    bool weekHadToday = false;
    bool dataChanged = false;  // Db::dataVersion() moved since the last reload
    bool followingNow = false; // false while browsing another week
};

// 60 s is 60x less work than the old once-a-second repaint and still
// sub-pixel: the body is roughly 700-2160 px tall for a local day of 86,400 s,
// so one now-line pixel is about 300-1,000 seconds. Raise it only with a
// measurement — and note the figure is why a per-second repaint was never
// visible in the first place.
constexpr long long kNowLineRefreshSeconds = 60;

Refresh refreshDecision(const RefreshInputs &in);

} // namespace klend_ario::gui
