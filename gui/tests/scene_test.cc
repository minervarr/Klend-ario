#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <string_view>

#include "scene.hh"

using namespace klend_ario;
using namespace klend_ario::gui;

// A deterministic stand-in for the font: every character is half the pixel size.
// Truncation is then a pure arithmetic question, which is the point — the scene
// decides WHERE to cut, the renderer only knows how wide the result is.
class FakeMetrics : public TextMetrics {
public:
    int width(std::string_view text, int sizePx) const override {
        return (sizePx * static_cast<int>(text.size())) / 2;
    }
};

// Sunday 2026-10-11T00:00:00Z; the fabricated week is Oct 11..17.
static constexpr long long kBase = 1791676800;
static constexpr int kWidth = 700;
static constexpr int kHeight = 500;

static long long at(int day, int hour, int minute = 0) {
    return kBase + static_cast<long long>(day) * 86400 + hour * 3600 + minute * 60;
}

static WeekModel makeWeek() {
    WeekModel week;
    week.weekLabel = "Week 42 \xC2\xB7 Oct 2026"; // "Week 42 · Oct 2026"
    for (int i = 0; i < 7; ++i) {
        DayInfo &info = week.days[static_cast<size_t>(i)];
        info.day.startUtc = kBase + static_cast<long long>(i) * 86400;
        info.day.endUtc = info.day.startUtc + 86400;
        info.day.year = 2026;
        info.day.month = 10;
        info.day.day = 11 + i;
        info.day.weekday = i;
        info.isToday = (i == 6);
    }
    return week;
}

static Event makeEvent(const char *title, long long start, long long end, int importance = 50,
                       int urgency = 50, EventType type = EventType::Event) {
    Event event;
    event.title = title;
    event.startUtc = start;
    event.endUtc = end;
    event.importance = importance;
    event.urgency = urgency;
    event.type = type;
    return event;
}

static bool sameColor(const Color &a, const Rgb &b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

static bool hasFill(const Scene &scene, const Rect &rect, const Rgb &rgb) {
    for (const Op &op : scene.ops) {
        if (op.kind != OpKind::Fill) {
            continue;
        }
        if (op.rect.x == rect.x && op.rect.y == rect.y && op.rect.w == rect.w &&
            op.rect.h == rect.h && sameColor(op.color, rgb)) {
            return true;
        }
    }
    return false;
}

static bool hasFillColor(const Scene &scene, const Rect &rect, const Color &color) {
    for (const Op &op : scene.ops) {
        if (op.kind != OpKind::Fill) {
            continue;
        }
        if (op.rect.x == rect.x && op.rect.y == rect.y && op.rect.w == rect.w &&
            op.rect.h == rect.h && op.color.r == color.r && op.color.g == color.g &&
            op.color.b == color.b) {
            return true;
        }
    }
    return false;
}

static const Op *findText(const Scene &scene, std::string_view text) {
    for (const Op &op : scene.ops) {
        if (op.kind == OpKind::Text && op.text == text) {
            return &op;
        }
    }
    return nullptr;
}

static const Op *findTextPrefix(const Scene &scene, std::string_view prefix) {
    for (const Op &op : scene.ops) {
        if (op.kind == OpKind::Text && op.text.rfind(prefix, 0) == 0) {
            return &op;
        }
    }
    return nullptr;
}

static int countKind(const Scene &scene, OpKind kind) {
    int total = 0;
    for (const Op &op : scene.ops) {
        total += op.kind == kind ? 1 : 0;
    }
    return total;
}

static const Op *findStroke(const Scene &scene, const Rect &rect) {
    for (const Op &op : scene.ops) {
        if (op.kind == OpKind::Stroke && op.rect.x == rect.x && op.rect.y == rect.y &&
            op.rect.w == rect.w && op.rect.h == rect.h) {
            return &op;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------

static Scene populatedScene() {
    WeekModel week = makeWeek();
    week.events.push_back(makeEvent("Deep work", at(0, 10), at(0, 12), 0, 0));
    week.events.push_back(makeEvent("Ship it", at(1, 10), at(1, 12), 100, 100));
    week.events.push_back(makeEvent("Standup", at(2, 9), at(2, 11)));
    week.events.push_back(makeEvent("Review", at(2, 10), at(2, 12)));
    week.events.push_back(makeEvent("Blink", at(3, 14), at(3, 14, 5)));
    week.events.push_back(
        makeEvent("A very long title that will not fit in a narrow day column at all",
                  at(3, 16), at(3, 18)));
    week.events.push_back(makeEvent("Sleep", at(4, 23), at(4, 24), 50, 50, EventType::Sleep));
    week.events.back().hidden = true; // hidden is a view flag; the scene draws what it gets
    week.events.push_back(makeEvent("Reminder", at(4, 8), at(4, 8), 50, 50, EventType::Alarm));

    const FakeMetrics measure;
    return buildWeekScene(kWidth, kHeight, week, at(6, 12), measure);
}

static void testHeaderAndChrome() {
    const Scene scene = populatedScene();
    const UiMetrics m = metricsFor(Rect{0, 0, kWidth, kHeight}); // 700x500

    // Background covers the whole window, not just the content rect.
    const Color background = defaultTheme().background;
    assert(hasFillColor(scene, Rect{0, 0, kWidth, kHeight}, background));

    // The week counter and all seven day headers, each with its date.
    assert(findText(scene, "Week 42 \xC2\xB7 Oct 2026") != nullptr);
    const char *headers[7] = {"SU 11", "MO 12", "TU 13", "WE 14", "TH 15", "FR 16", "SA 17"};
    for (int i = 0; i < 7; ++i) {
        const Op *op = findText(scene, headers[i]);
        assert(op != nullptr);
        assert(op->size == m.dayLetterSize);
        assert(op->align == Align::Center);
    }
    // Today is the red accent; the rest are dim; a holiday would be amber.
    assert(findText(scene, "SA 17")->color.r == defaultTheme().nowLine.r);
    assert(findText(scene, "SU 11")->color.r == defaultTheme().dimText.r);

    // Three navigation targets, right-aligned inside the content margin.
    assert(scene.hits.size() == 3);
    assert(scene.hits[0].id == "prev" && scene.hits[1].id == "today" &&
           scene.hits[2].id == "next");
    assert(scene.hits[2].rect.right() == kWidth - m.headerPad);
    assert(scene.hits[0].rect.right() < scene.hits[1].rect.x);
    assert(scene.hits[1].rect.right() < scene.hits[2].rect.x);
}

static void testEventBoxAndLabels() {
    const Scene scene = populatedScene();

    // Day 0, 10:00-12:00 over a 438px body that starts at y=62:
    //   10:00 -> 62 + 183 = 245, 12:00 -> 62 + 219 = 281, so 36px tall.
    const Event deep = makeEvent("", at(0, 10), at(0, 12), 0, 0);
    const Rgb violet = rgbForStyle(styleFor(deep));
    assert(hasFill(scene, Rect{0, 245, 100, 36}, violet));

    // The time label is the local clock, and the title sits under it.
    const Op *time = findText(scene, "10:00");
    assert(time != nullptr);
    assert(time->rect.x == 3 && time->rect.y == 248 && time->rect.w == 94);
    const Op *title = findText(scene, "Deep work");
    assert(title != nullptr);
    assert(title->rect.y == 248 + 9 + 2); // time size + labelPad at this frame

    // Urgency 0 -> a 1px inset; urgency 100 -> the maximum (3px at this width),
    // because the border keeps the same hue as the fill.
    const Op *thin = findStroke(scene, Rect{0, 245, 100, 36});
    assert(thin != nullptr && thin->thickness == 1);
    const Op *thick = findStroke(scene, Rect{100, 245, 100, 36});
    assert(thick != nullptr && thick->thickness == 3);

    // Importance 100 is red.
    const Event ship = makeEvent("", at(1, 10), at(1, 12), 100, 100);
    assert(hasFill(scene, Rect{100, 245, 100, 36}, rgbForStyle(styleFor(ship))));
}

static void testOverlapSplitsTheColumn() {
    const Scene scene = populatedScene();
    // Two overlapping events share the column: 49 + 1 gutter + 49 in a 100px
    // column. The earlier one is on the left.
    const Event standup = makeEvent("", at(2, 9), at(2, 11));
    const Event review = makeEvent("", at(2, 10), at(2, 12));
    assert(hasFill(scene, Rect{200, 226, 49, 37}, rgbForStyle(styleFor(standup))));
    assert(hasFill(scene, Rect{251, 245, 49, 36}, rgbForStyle(styleFor(review))));
    assert(findText(scene, "Standup") != nullptr);
    assert(findText(scene, "Review") != nullptr);
}

static void testShortEventKeepsItsPositionAndLosesItsText() {
    const Scene scene = populatedScene();
    // 14:00-14:05 is one pixel tall here. It is still drawn at its true start
    // (y=318), never moved and never grown — that is the "minimum marker": the
    // box itself is the marker.
    assert(hasFill(scene, Rect{300, 318, 100, 1}, rgbForStyle(styleFor(makeEvent("", 0, 0)))));
    assert(findText(scene, "14:00") == nullptr);
    assert(findText(scene, "Blink") == nullptr);
}

static void testAlarmIsNotABlock() {
    const Scene scene = populatedScene();
    assert(findText(scene, "Reminder") == nullptr);
    // 08:00 on day 4 would be at y = 62 + 146 = 208; nothing may be drawn there.
    for (const Op &op : scene.ops) {
        if (op.kind == OpKind::Fill && op.rect.x == 400 && op.rect.y == 208) {
            assert(false);
        }
    }
}

static void testSleepIsDrawnLikeAnyBlock() {
    const Scene scene = populatedScene();
    const Event sleep = makeEvent("", at(4, 23), at(4, 24));
    assert(hasFill(scene, Rect{400, 482, 100, 18}, rgbForStyle(styleFor(sleep))));
    assert(findText(scene, "23:00") != nullptr);
}

static void testTailHatchStopsAtTheLastEvent() {
    const Scene scene = populatedScene();
    // Six days have a tail; Thursday (day 4) is covered to midnight by sleep.
    assert(countKind(scene, OpKind::Hatch) == 6);

    // Day 0's last block ends at 12:00 -> the hatch starts there and reaches the
    // bottom of the column.
    bool found = false;
    for (const Op &op : scene.ops) {
        if (op.kind == OpKind::Hatch && op.rect.x == 0) {
            assert(op.rect.y == 281);
            assert(op.rect.bottom() == kHeight);
            assert(op.spacing == metricsFor(Rect{0, 0, kWidth, kHeight}).hatchSpacing);
            found = true;
        }
    }
    assert(found);

    // An empty day is all tail.
    for (const Op &op : scene.ops) {
        if (op.kind == OpKind::Hatch && op.rect.x == 500) {
            assert(op.rect.y == 62 && op.rect.h == 438);
        }
    }
}

static void testNowLineOnlyOnToday() {
    const Scene scene = populatedScene();
    const Color now = defaultTheme().nowLine;
    // 12:00 on today's column (day 6) -> y = 62 + 219 = 281.
    assert(hasFillColor(scene, Rect{600, 281, 100, 1}, now));
    // No other column gets one.
    int lines = 0;
    for (const Op &op : scene.ops) {
        if (op.kind == OpKind::Fill && op.color.r == now.r && op.color.g == now.g &&
            op.rect.h == 1) {
            ++lines;
        }
    }
    assert(lines == 1);
}

static void testLongTitleIsTruncated() {
    const Scene scene = populatedScene();
    const Op *title = findTextPrefix(scene, "A very");
    assert(title != nullptr);
    assert(title->text.size() >= 4);
    assert(title->text.substr(title->text.size() - 3) == "\xE2\x80\xA6"); // …
    const FakeMetrics measure;
    assert(measure.width(title->text, title->size) <= title->rect.w);
}

static void testClippedTopSkipsTheStartTime() {
    WeekModel week = makeWeek();
    // Began the previous evening; the column must not claim a 23:00 start that
    // belongs to yesterday.
    week.events.push_back(makeEvent("Overnight", kBase - 3600, kBase + 3600));
    const FakeMetrics measure;
    const Scene scene = buildWeekScene(kWidth, kHeight, week, at(6, 12), measure);
    assert(findText(scene, "Overnight") != nullptr);
    assert(findText(scene, "23:00") == nullptr);
}

static void testDstDayPlacesEventsByItsOwnLength() {
    WeekModel week = makeWeek();
    week.days[0].day.endUtc = week.days[0].day.startUtc + 23 * 3600; // a 23h day
    week.events.push_back(makeEvent("Short day", week.days[0].day.startUtc + 12 * 3600,
                                    week.days[0].day.startUtc + 13 * 3600));
    const FakeMetrics measure;
    const Scene scene = buildWeekScene(kWidth, kHeight, week, at(6, 12), measure);
    // 12:00 is 12/23 of 438px -> y = 62 + 229; one hour -> 19px.
    const Event expected = makeEvent("", 0, 0);
    assert(hasFill(scene, Rect{0, 291, 100, 19}, rgbForStyle(styleFor(expected))));
}

static void testEmptyWeekSaysSo() {
    WeekModel week = makeWeek();
    const FakeMetrics measure;
    const Scene scene = buildWeekScene(kWidth, kHeight, week, at(6, 12), measure);
    int hints = 0;
    for (const Op &op : scene.ops) {
        if (op.kind == OpKind::Text &&
            op.text.rfind("No events this week", 0) == 0) {
            ++hints;
        }
    }
    assert(hints == 1);
    // Seven tails and no event fills.
    assert(countKind(scene, OpKind::Hatch) == 7);
    assert(countKind(scene, OpKind::Stroke) == 0);
}

static void testAlarmOnlyWeekIsEmpty() {
    WeekModel week = makeWeek();
    week.events.push_back(makeEvent("Reminder", at(4, 8), at(4, 8), 50, 50, EventType::Alarm));
    const FakeMetrics measure;
    const Scene scene = buildWeekScene(kWidth, kHeight, week, at(6, 12), measure);
    assert(findTextPrefix(scene, "No events this week") != nullptr);
}

static void testHolidayAccent() {
    WeekModel week = makeWeek();
    week.days[3].holiday = "Something";
    const FakeMetrics measure;
    const Scene scene = buildWeekScene(kWidth, kHeight, week, at(6, 12), measure);
    const Op *header = findText(scene, "WE 14");
    assert(header != nullptr);
    assert(header->color.g == defaultTheme().holidayAccent.g);
}

int main() {
    // Clock labels are local; pin the zone so "10:00" means 10:00.
    setenv("TZ", "UTC0", 1);
    tzset();

    testHeaderAndChrome();
    testEventBoxAndLabels();
    testOverlapSplitsTheColumn();
    testShortEventKeepsItsPositionAndLosesItsText();
    testAlarmIsNotABlock();
    testSleepIsDrawnLikeAnyBlock();
    testTailHatchStopsAtTheLastEvent();
    testNowLineOnlyOnToday();
    testLongTitleIsTruncated();
    testClippedTopSkipsTheStartTime();
    testDstDayPlacesEventsByItsOwnLength();
    testEmptyWeekSaysSo();
    testAlarmOnlyWeekIsEmpty();
    testHolidayAccent();
    std::printf("scene_test: all assertions passed\n");
    return 0;
}
