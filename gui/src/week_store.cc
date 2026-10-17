#include "week_store.hh"

#include <utility>

#include "klend_ario/event.hh"
#include "klend_ario/holidays.hh"

namespace klend_ario::gui {

std::string weekLabelFor(const LocalWeek &week) {
    const LocalDay &sunday = week.days[0];
    const LocalDay &saturday = week.days[6];
    // A Sunday and Saturday in different years means the week contains 1
    // January, so the whole week is week 1 of the later year.
    const LocalDay &label = (sunday.year != saturday.year) ? saturday : sunday;
    const WeekOfYear counter = weekOfYearFromCivilDate(label.year, label.month, label.day);
    return "Week " + std::to_string(counter.week) + " \xC2\xB7 " + formatMonthShort(label) +
           " " + std::to_string(counter.year);
}

arc::Result<WeekModel> loadWeekModel(klend_ario::Db &db, long long anchorUtc,
                                     long long nowUtc, klend_ario::Visibility visibility,
                                     std::string_view holidayCountry) {
    const LocalWeek local = localWeekContaining(anchorUtc);

    WeekModel model;
    model.weekLabel = weekLabelFor(local);
    for (size_t i = 0; i < local.days.size(); ++i) {
        const LocalDay &day = local.days[i];
        DayInfo &info = model.days[i];
        info.day = day;
        info.isToday = nowUtc >= day.startUtc && nowUtc < day.endUtc;

        // A holiday is a civil date, so it comes from the LOCAL day, not from a
        // UTC instant. An unknown country (or an invalid date) is not worth
        // failing the view over: the accent is decoration.
        if (const auto date = holidays::Date::fromYmd(day.year, static_cast<unsigned>(day.month),
                                                     static_cast<unsigned>(day.day))) {
            const auto holiday = klend_ario::holidayOn(holidayCountry, *date);
            if (holiday.ok() && holiday.value().has_value()) {
                info.holiday = std::string(holiday.value()->name);
            }
        }
    }

    klend_ario::EventQuery query;
    query.fromUtc = local.days[0].startUtc;
    query.toUtc = local.days[6].endUtc;
    query.visibility = visibility;
    query.sort = klend_ario::EventSort::Start;

    auto rows = db.queryEvents(query);
    if (!rows) {
        return rows.error();
    }
    model.events = std::move(rows.value());
    return model;
}

} // namespace klend_ario::gui
