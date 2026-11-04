// klend_ario_edit_bench — the edit path, measured without a window.
//
// ROADMAP §4.4 asks for numbers, not an impression: how long a Save takes
// (insert and compare-and-swap update over a real file), what the reload costs,
// what rebuilding the scene costs, and whether the glyph atlas is baked again.
// A live window cannot answer that here — the verification rule forbids taking
// focus for a check — and a wall-clock threshold in CTest would be flaky, so
// this is dev tooling: it asserts that Save still DOES its job and prints
// timings for the docs.
//
// It times the shipping units, not a copy of them: saveEditorToDb (the only
// write bridge), loadWeekModel (the one query), buildWeekScene (the scene the
// window draws) and RasterFont::ensureGlyphs (the atlas cache).
//
//   ./klend_ario_edit_bench                       # throwaway DB under $TMPDIR
//   ./klend_ario_edit_bench --dir /tmp/bench --keep
//   ./klend_ario_edit_bench --iterations 500
//
// The numbers are microseconds unless a row says otherwise. Compare runs on the
// same machine, in the same build type; quote the file size with them, because
// open time is a function of it.

#include <unistd.h> // getpid(): the default temp directory is per process

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "app_paths.hh"
#include "asset_reader.hh"
#include "editor.hh"
#include "editor_store.hh"
#include "klend_ario/db.hh"
#include "klend_ario/event.hh"
#include "klend_ario/query.hh"
#include "raster_font.hh"
#include "scene.hh"
#include "text_inputs.hh"
#include "theme.hh"
#include "ui_metrics.hh"
#include "week_layout.hh"
#include "week_store.hh"

namespace {

using namespace klend_ario;
using namespace klend_ario::gui;
using Clock = std::chrono::steady_clock;

// Sunday 2026-10-11T00:00:00Z. TZ is pinned to UTC0 below, so the local day
// columns and the stored instants agree and the fixture stays deterministic.
constexpr long long kBase = 1791676800;
constexpr int kWeeks = 52;
constexpr int kTargetWeek = 26;      // the "busy week" the measurements reload
constexpr int kDenseEvents = 120;    // a heavily booked week
constexpr int kNormalEvents = 40;    // an ordinary week
constexpr int kWidth = 1280;
constexpr int kHeight = 720;
const char *kPassphrase = "edit-bench";

// The scene measures text through a seam so it needs no font. A fixed
// half-em-per-character width is enough here: this tool measures the scene's
// own work, and the real metrics do no better than a linear approximation.
struct FixedMetrics final : TextMetrics {
    int width(std::string_view text, int sizePx) const override {
        return sizePx * static_cast<int>(text.size()) / 2;
    }
};

const FixedMetrics kFixedMetrics;

[[noreturn]] void fail(const char *what) {
    std::fprintf(stderr, "edit_bench: FAILED: %s\n", what);
    std::exit(1);
}

void check(bool ok, const char *what) {
    if (!ok) {
        fail(what);
    }
}

long long weekStartUtc(int week) {
    return kBase + static_cast<long long>(week) * 7 * 86400;
}

// Deterministic rows: `normal` events in every week, `dense` in the target one,
// spread across the seven days so the layout has overlaps to cluster.
arc::Result<void> seed(Db &db) {
    for (int week = 0; week < kWeeks; ++week) {
        const int count = (week == kTargetWeek) ? kDenseEvents : kNormalEvents;
        const long long start = weekStartUtc(week);
        for (int i = 0; i < count; ++i) {
            Event event;
            event.title = "Bench W" + std::to_string(week) + " E" + std::to_string(i);
            event.startUtc = start + static_cast<long long>(i % 7) * 86400 +
                             (i / 7) * 1800;
            event.endUtc = event.startUtc + 1500;
            event.importance = i % 101;
            event.urgency = (i * 7) % 101;
            auto inserted = db.insertEvent(event);
            if (!inserted) {
                return inserted.error();
            }
        }
    }
    return {};
}

Event denseRow(Db &db, long long anchorUtc, long long nowUtc) {
    auto model = loadWeekModel(db, anchorUtc, nowUtc, Visibility::Any);
    check(model.ok(), "loadWeekModel for the dense week");
    check(!model.value().events.empty(), "the dense week has events");
    return model.value().events.front();
}

template <typename Fn>
std::vector<double> measure(int iterations, Fn fn) {
    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(iterations));
    for (int i = 0; i < iterations; ++i) {
        const auto started = Clock::now();
        fn(i);
        const auto finished = Clock::now();
        samples.push_back(
            std::chrono::duration<double, std::micro>(finished - started).count());
    }
    return samples;
}

double percentile(std::vector<double> samples, int percent) {
    check(!samples.empty(), "percentile of an empty sample set");
    std::sort(samples.begin(), samples.end());
    size_t index = static_cast<size_t>(samples.size()) * static_cast<size_t>(percent) / 100;
    if (index >= samples.size()) {
        index = samples.size() - 1;
    }
    return samples[index];
}

void report(const char *name, const std::vector<double> &samples) {
    const double p50 = percentile(samples, 50);
    const double p95 = percentile(samples, 95);
    const double worst = percentile(samples, 100);
    std::printf("%-30s %5zu %10.1f %10.1f %10.1f\n", name, samples.size(), p50, p95, worst);
}

struct Options {
    std::string dir;
    int iterations = 200;
    bool keep = false;
};

Options parseArgs(int argc, char **argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (std::strcmp(arg, "--dir") == 0 && i + 1 < argc) {
            options.dir = argv[++i];
        } else if (std::strcmp(arg, "--iterations") == 0 && i + 1 < argc) {
            options.iterations = std::atoi(argv[++i]);
            check(options.iterations > 0, "--iterations must be positive");
        } else if (std::strcmp(arg, "--keep") == 0) {
            options.keep = true;
        } else {
            std::fprintf(stderr,
                         "usage: %s [--dir <path>] [--iterations N] [--keep]\n", argv[0]);
            std::exit(2);
        }
    }
    return options;
}

std::string humanSize(uintmax_t bytes) {
    char buffer[32];
    if (bytes >= 1024u * 1024u) {
        std::snprintf(buffer, sizeof(buffer), "%.1f MiB",
                      static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024u) {
        std::snprintf(buffer, sizeof(buffer), "%.1f KiB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
    }
    return buffer;
}

} // namespace

int main(int argc, char **argv) {
    // The form is local wall-clock text; pin the zone so a run is reproducible
    // and the printed fixture is the one the docs describe.
    setenv("TZ", "UTC0", 1);
    tzset();

    const Options options = parseArgs(argc, argv);

    std::filesystem::path dir;
    if (!options.dir.empty()) {
        dir = options.dir;
    } else {
        dir = std::filesystem::temp_directory_path() /
              ("klend_edit_bench_" + std::to_string(::getpid()));
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    check(!ec, "create the bench directory");

    const std::string plainPath = (dir / "bench.db").string();
    const std::string encryptedPath = (dir / "bench_enc.db").string();
    std::filesystem::remove(plainPath, ec);
    std::filesystem::remove(encryptedPath, ec);

    // --- the fixture ------------------------------------------------------
    {
        auto db = Db::open(plainPath);
        check(db.ok(), "open the plaintext bench database");
        check(seed(db.value()).ok(), "seed the bench database");
    }
    {
        auto db = Db::openEncrypted(encryptedPath, kPassphrase);
        check(db.ok(), "open the encrypted bench database");
        check(seed(db.value()).ok(), "seed the encrypted bench database");
    }

    const long long anchorUtc = weekStartUtc(kTargetWeek) + 3 * 86400; // Wednesday
    const long long nowUtc = anchorUtc + 12 * 3600;

    auto dbResult = Db::open(plainPath);
    check(dbResult.ok(), "reopen the bench database");
    Db db = std::move(dbResult.value());

    const auto weekResult = loadWeekModel(db, anchorUtc, nowUtc, Visibility::Any);
    check(weekResult.ok(), "load the dense week");
    const WeekModel week = weekResult.value();
    check(week.events.size() == static_cast<size_t>(kDenseEvents), "dense week row count");

    const klend_ario::gui::UiMetrics metrics =
        metricsFor(frameFor(kWidth, kHeight).content,
                   static_cast<int>(std::ceil(kMinReadableTextSizePx)));
    const TextInputs text = collectTextInputs(week, metrics);
    check(!text.codepoints.empty() && !text.sizesPx.empty(), "the week has text to bake");

    uintmax_t plainBytes = std::filesystem::file_size(plainPath, ec);
    uintmax_t encryptedBytes = std::filesystem::file_size(encryptedPath, ec);

    std::printf("edit_bench: %d weeks x %d events + %d in week %d\n", kWeeks, kNormalEvents,
                kDenseEvents, kTargetWeek);
    std::printf("edit_bench: plaintext %s, encrypted %s, %zu glyphs x %zu sizes\n",
                humanSize(plainBytes).c_str(), humanSize(encryptedBytes).c_str(),
                text.codepoints.size(), text.sizesPx.size());
    std::printf("%-30s %5s %10s %10s %10s\n", "scenario", "n", "p50", "p95",
                "max (us)");

    // --- open (plaintext) -------------------------------------------------
    report("open plaintext",
           measure(20, [&](int) {
               auto opened = Db::open(plainPath);
               check(opened.ok(), "open plaintext inside the loop");
           }));

    // --- open (encrypted) -------------------------------------------------
    report("open encrypted",
           measure(20, [&](int) {
               auto opened = Db::openEncrypted(encryptedPath, kPassphrase);
               check(opened.ok(), "open encrypted inside the loop");
           }));

    // --- reload alone -----------------------------------------------------
    report("reload (one query, 120 rows)",
           measure(options.iterations, [&](int) {
               auto model = loadWeekModel(db, anchorUtc, nowUtc, Visibility::Any);
               check(model.ok(), "loadWeekModel inside the loop");
               check(model.value().events.size() == static_cast<size_t>(kDenseEvents),
                     "reload sees the dense week");
           }));

    // --- insert + reload (the Save path for a new event) -------------------
    //
    // The row is removed OUTSIDE the timed region so every iteration edits a
    // week of the same size; otherwise the reload would grow with the loop.
    report("insert + reload",
           measure(options.iterations, [&](int i) {
               EditorState state = editorForNew(anchorUtc + 9 * 3600 + i * 60);
               state.title = "Inserted " + std::to_string(i);
               std::string warning;
               auto saved = saveEditorToDb(db, state, warning);
               check(saved.ok(), "insert through saveEditorToDb");
               check(warning.empty(), "an ASCII save needs no warning");
               auto reloaded = loadWeekModel(db, anchorUtc, nowUtc, Visibility::Any);
               check(reloaded.ok(), "reload after insert");
               check(db.removeEvent(saved.value()).ok(), "remove the inserted row");
           }));

    // --- compare-and-swap update + reload --------------------------------
    //
    // The re-read (to learn the new revision) is untimed: the GUI got the row
    // with the week, so only the save and the reload are the user-visible cost.
    {
        Event row = denseRow(db, anchorUtc, nowUtc);
        report("cas update + reload",
               measure(options.iterations, [&](int i) {
                   EditorState state = editorForEvent(row);
                   state.title = "Edited " + std::to_string(i);
                   std::string warning;
                   auto saved = saveEditorToDb(db, state, warning);
                   check(saved.ok(), "update through saveEditorToDb");
                   auto reloaded = loadWeekModel(db, anchorUtc, nowUtc, Visibility::Any);
                   check(reloaded.ok(), "reload after update");
                   auto fresh = db.getEvent(row.id);
                   check(fresh.ok(), "re-read the edited row");
                   check(fresh.value().revision == row.revision + 1,
                         "the CAS token moved");
                   row = std::move(fresh.value());
               }));
    }

    // --- the scene the frame builds ---------------------------------------
    report("scene 1280x720 (120 rows)",
           measure(options.iterations, [&](int) {
               const Scene scene =
                   buildWeekScene(kWidth, kHeight, week, nowUtc, kFixedMetrics,
                                  defaultTheme(),
                                  static_cast<int>(std::ceil(kMinReadableTextSizePx)));
               check(!scene.ops.empty(), "the scene has draw ops");
           }));

    // --- text: collect, bake once, then ask again -------------------------
    report("collect text inputs",
           measure(options.iterations, [&](int) {
               const TextInputs again = collectTextInputs(week, metrics);
               check(again.codepoints.size() == text.codepoints.size(),
                     "the collector is stable");
           }));

    RasterFont font;
    FileByteReader reader;
    const std::string fontPath = app_paths::exeDir() + "fonts/ui/ui.otf";
    check(font.open(reader, fontPath.c_str()), "load fonts/ui/ui.otf");

    report("atlas bake (reset + all)",
           measure(3, [&](int) {
               font.reset();
               const int added = font.ensureGlyphs(text.codepoints, text.sizesPx);
               check(added > 0, "a reset atlas bakes cells");
               check(!font.hasMisses(), "the full set bakes without misses");
           }));

    report("atlas ensure (same set)",
           measure(options.iterations, [&](int) {
               const int added = font.ensureGlyphs(text.codepoints, text.sizesPx);
               check(added == 0, "an already-baked set adds no cells");
               check(!font.hasMisses(), "no miss for a baked set");
           }));

    // The question the docs must answer: does an edit re-bake the atlas? The
    // window re-collects after every reload, but ensureGlyphs only adds cells
    // for a (codepoint, size) the cache has never seen.
    {
        std::vector<uint32_t> withNew = text.codepoints;
        withNew.push_back(0x00F1); // ñ, a codepoint the fixture lacks
        std::sort(withNew.begin(), withNew.end());
        withNew.erase(std::unique(withNew.begin(), withNew.end()), withNew.end());
        const int added = font.ensureGlyphs(withNew, text.sizesPx);
        check(added > 0, "a new codepoint bakes a cell");
        std::printf("edit_bench: new codepoint added %d cell(s) at %zu size(s); "
                    "same-set saves add none\n", added, text.sizesPx.size());
    }

    if (!options.keep) {
        std::filesystem::remove_all(dir, ec);
        std::printf("edit_bench: removed %s (use --keep to preserve)\n", dir.string().c_str());
    } else {
        std::printf("edit_bench: kept %s\n", dir.string().c_str());
    }
    return 0;
}
