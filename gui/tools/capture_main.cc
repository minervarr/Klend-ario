// klend_ario_capture — headless screenshots of the weekly view.
//
// Dev tooling (Debug-only). It does not need a window, a compositor or a
// display: vk_canvas renders through VK_EXT_headless_surface and writes the
// pixels back as PNG. That is what lets the view be reviewed on a machine like
// the one this was written on, where DISPLAY is empty.
//
// The scenarios are FIXTURES, not the user's database: deterministic pixels are
// what makes a review meaningful, and a screenshot of someone's real calendar
// is not something a test should need.
//
//   ./klend_ario_capture                        # -> ./gui-shots/
//   ./klend_ario_capture --frame 1280x720       # the full-bleed layout
//   ./klend_ario_capture --out /tmp/shots --list

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

#include "app_paths.hh"
#include "canvas.hh"
#include "capture.hh"
#include "font.hh"
#include "headless.hh"
#include "renderer.hh"
#include "scene.hh"
#include "scene_render.hh"

namespace {

using namespace klend_ario;
using namespace klend_ario::gui;

// Sunday 2026-10-11T00:00:00Z. The tool pins TZ=UTC0, so these are also the
// local wall clocks the labels show.
constexpr long long kBase = 1791676800;

long long at(int day, int hour, int minute = 0) {
    return kBase + static_cast<long long>(day) * 86400 + hour * 3600 + minute * 60;
}

Event event(const char *title, long long start, long long end, int importance = 50,
            int urgency = 50, EventType type = EventType::Event, bool hidden = false) {
    Event e;
    e.title = title;
    e.startUtc = start;
    e.endUtc = end;
    e.importance = importance;
    e.urgency = urgency;
    e.type = type;
    e.hidden = hidden;
    return e;
}

WeekModel makeWeek() {
    WeekModel week;
    week.weekLabel = "Week 42 \xC2\xB7 Oct 2026";
    for (int i = 0; i < 7; ++i) {
        DayInfo &day = week.days[static_cast<size_t>(i)];
        day.day.startUtc = kBase + static_cast<long long>(i) * 86400;
        day.day.endUtc = day.day.startUtc + 86400;
        day.day.year = 2026;
        day.day.month = 10;
        day.day.day = 11 + i;
        day.day.weekday = i;
        day.isToday = (i == 6);
    }
    // A holiday accent on Thursday.
    week.days[4].holiday = "Día no laborable";
    return week;
}

struct Fixture {
    std::string path;
    WeekModel week;
    long long nowUtc = 0;
};

std::vector<Fixture> fixtures() {
    std::vector<Fixture> out;

    // 1. An empty week: seven hatched tails and the hint. What a new calendar
    //    looks like, and the state where a broken layout is hardest to hide.
    out.push_back({"10-empty/00-week", makeWeek(), at(6, 12)});

    // 2. A realistic week: a full day, an overlap, urgent work, a short event,
    //    hidden sleep, an alarm row (which must NOT draw a block), and today.
    {
        WeekModel week = makeWeek();
        week.events = {
            event("Deep work", at(0, 9), at(0, 12), 80, 20),
            event("Lunch", at(0, 12), at(0, 13), 10, 10),
            event("Review", at(0, 14), at(0, 15, 30), 60, 60),
            event("Standup", at(1, 9), at(1, 9, 15), 40, 90),
            event("Ship the GUI", at(1, 10), at(1, 12), 100, 100),
            event("Overlap A", at(2, 10), at(2, 12), 50, 50),
            event("Overlap B", at(2, 11), at(2, 13), 70, 30),
            event("Coffee", at(2, 15), at(2, 15, 5), 5, 5),
            event("Gym", at(3, 18), at(3, 19, 30), 30, 70),
            event("Reminder", at(4, 8), at(4, 8), 50, 50, EventType::Alarm),
            event("Dinner with Ana", at(4, 20), at(4, 22), 90, 40),
            event("Write", at(5, 10), at(5, 13), 60, 10),
            event("Call", at(6, 15), at(6, 15, 30), 100, 80),
        };
        out.push_back({"20-week/00-mixed", week, at(6, 16)});
    }

    // 3. The same week with the sleep block VISIBLE: the flag is a query choice,
    //    and the block draws like any other event when it is allowed through.
    {
        WeekModel week = makeWeek();
        week.events = {
            event("Sleep", at(0, 23), at(1, 9), 20, 10, EventType::Sleep),
            event("Work", at(1, 9), at(1, 18), 50, 30),
        };
        out.push_back({"30-hidden/00-sleep-visible", week, at(1, 12)});
    }

    // 4. Overlaps at every density: two, three and four at once, plus an event
    //    crossing midnight (clipped in both columns).
    {
        WeekModel week = makeWeek();
        week.events = {
            event("A", at(2, 9), at(2, 12), 90, 20),
            event("B", at(2, 9, 30), at(2, 11), 60, 40),
            event("C", at(2, 10), at(2, 13), 30, 60),
            event("D", at(2, 10, 30), at(2, 11, 30), 100, 100),
            event("Night shift", at(3, 22), at(4, 6), 70, 50),
            event("Day off", at(5, 0), at(5, 23, 59), 20, 20),
        };
        out.push_back({"40-density/00-overlaps", week, at(3, 12)});
    }

    return out;
}

std::vector<vkc::Scenario> makeScenarios(const Font &font) {
    std::vector<vkc::Scenario> scenarios;
    for (const Fixture &fixture : fixtures()) {
        vkc::Scenario scenario;
        scenario.path = fixture.path;
        scenario.render = [&font, fixture](Renderer &renderer) {
            const FontTextMetrics measure(&font);
            const Scene scene = buildWeekScene(static_cast<int>(renderer.width()),
                                               static_cast<int>(renderer.height()),
                                               fixture.week, fixture.nowUtc, measure);

            std::vector<float> curves;
            std::vector<float> shapes;
            Canvas canvas(curves, renderer.width(), renderer.height(), &font, 0.0f, 0.0f, 0.0f,
                          0.0f);
            canvas.useShapes(&shapes);
            drawScene(canvas, scene);
            renderer.draw(curves, /*overlay_rotation_deg=*/0, {}, {}, {}, shapes);
        };
        scenarios.push_back(std::move(scenario));
    }
    return scenarios;
}

} // namespace

int main(int argc, char **argv) {
    // Deterministic pixels: the clock labels are local, so pin the zone.
    setenv("TZ", "UTC0", 1);
    tzset();

    if (!headless_surface_supported()) {
        std::fprintf(stderr,
                     "error: this Vulkan driver does not offer VK_EXT_headless_surface,\n"
                     "       so the capture cannot render without a window.\n");
        return 1;
    }

    Font font;
    const std::string fontPath = app_paths::exeDir() + "fonts/ui/ui.otf";
    if (!font.load(fontPath.c_str())) {
        std::fprintf(stderr, "error: cannot load %s\n", fontPath.c_str());
        return 1;
    }

    vkc::CaptureConfig config;
    config.default_out = "gui-shots";

    const std::vector<vkc::Scenario> scenarios = makeScenarios(font);
    return vkc::capture_main(argc, argv, scenarios, config);
}
