#include "week_view.hh"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <utility>
#include <vector>

#include "arc/fs/paths.hh"
#include "canvas.hh"
#include "font.hh"
#include "host.hh"
#include "keys.hh"
#include "klend_ario/event.hh"
#include "klend_ario/query.hh"
#include "klend_ario/sync.hh"
#include "renderer.hh"
#include "scene_render.hh"
#include "week_store.hh"

namespace klend_ario::gui {
namespace {

constexpr int kPollTimer = 1;
constexpr long long kWeekSeconds = 7 * 86400;

// Letters arrive in the Win32 VK space (ASCII uppercase); keys.hh names the
// handful the engine itself uses, so the app's own keys live here.
constexpr int kKeyT = 0x54;
constexpr int kKeyH = 0x48;

std::string resolvePassphrase(const ViewOptions &options, std::string &error) {
    if (!options.keyFile.empty()) {
        std::ifstream in(options.keyFile, std::ios::binary);
        std::string line;
        if (!std::getline(in, line)) {
            error = "cannot read key file: " + options.keyFile;
            return {};
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        return line;
    }
    if (const char *env = std::getenv("KLEND_ARIO_KEY"); env != nullptr) {
        return env;
    }
    return {};
}

} // namespace

WeekView::WeekView() = default;
WeekView::~WeekView() = default;

bool WeekView::create(const ViewOptions &options) {
    host_ = make_host();
    if (!host_) {
        std::fprintf(stderr, "error: no window host available on this platform\n");
        return false;
    }
    if (!host_->init(this)) {
        std::fprintf(stderr, "error: could not create the window\n");
        return false;
    }

    if (!openDatabase(options)) {
        return false;
    }
    if (!loadFont()) {
        // A missing font is not fatal: Canvas falls back to stroke outlines.
        std::fprintf(stderr, "warning: fonts/ui/ui.otf not found; using outline text\n");
    }

    try {
        renderer_ = std::make_unique<Renderer>(host_->surfaceProvider(), host_->assetReader(),
                                               /*desiredSwapchainImages=*/3);
    } catch (const std::exception &e) {
        host_->showErrorMessage("Vulkan initialization failed", e.what());
        std::fprintf(stderr, "error: Vulkan initialization failed: %s\n", e.what());
        return false;
    }

    nowUtc_ = klend_ario::utcNowSeconds();
    anchorUtc_ = nowUtc_;
    if (auto version = db_.dataVersion(); version.ok()) {
        dataVersion_ = version.value();
    }
    reload();

    host_->startTimer(kPollTimer, 1000);
    host_->showWindow();
    pendingFrames_ = 1;
    return true;
}

bool WeekView::openDatabase(const ViewOptions &options) {
    dbPath_ = options.dbPath;
    if (dbPath_.empty()) {
        const arc::fs::Layout layout = arc::fs::layout("klend_ario");
        dbPath_ = arc::fs::join(layout.state, "klend_ario.db");
    }

    std::string error;
    const std::string passphrase = resolvePassphrase(options, error);
    if (!error.empty()) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return false;
    }

    // READ-ONLY, and it must already exist: opening a missing calendar with
    // create/migrate would turn a wrong path into a silent empty week.
    auto opened = passphrase.empty() ? klend_ario::Db::openReadOnly(dbPath_)
                                     : klend_ario::Db::openEncryptedReadOnly(dbPath_, passphrase);
    if (!opened) {
        std::fprintf(stderr, "error: %s\n", opened.error().message.c_str());
        std::fprintf(stderr, "       database: %s\n", dbPath_.c_str());
        if (passphrase.empty()) {
            std::fprintf(stderr, "       (if it is encrypted, pass --key-file <path> or set "
                                 "$KLEND_ARIO_KEY)\n");
        }
        std::fprintf(stderr, "       create a calendar with: klend_ario_cli add --title ...\n");
        return false;
    }
    db_ = opened.take();
    dbOpen_ = true;

    // Hidden rows are a view choice; the query is what filters them.
    showHidden_ = options.showHidden;

    // Detect, never merge (MEMORY.md §7). Say it on startup so a conflict copy
    // cannot sit there unnoticed behind a window.
    for (const std::string &copy : klend_ario::syncConflictCopies(dbPath_)) {
        std::fprintf(stderr, "warning: sync conflict copy present: %s\n", copy.c_str());
    }
    return true;
}

bool WeekView::loadFont() {
    font_ = std::make_unique<Font>();
    const std::string path = host_->exeDir() + "fonts/ui/ui.otf";
    std::vector<uint8_t> bytes;
    if (host_->dataReader().read(path.c_str(), bytes) && !bytes.empty() &&
        font_->loadFromMemory(bytes.data(), bytes.size())) {
        return true;
    }
    font_.reset();
    return false;
}

void WeekView::reload() {
    if (!dbOpen_) {
        return;
    }
    const auto visibility = showHidden_ ? klend_ario::Visibility::Any
                                        : klend_ario::Visibility::Visible;
    auto model = loadWeekModel(db_, anchorUtc_, nowUtc_, visibility);
    if (!model) {
        std::fprintf(stderr, "error: %s\n", model.error().message.c_str());
        return;
    }
    week_ = std::move(model.value());
    pendingFrames_ = 1;
}

void WeekView::goToWeek(long long anchorUtc) {
    anchorUtc_ = anchorUtc;
    reload();
}

void WeekView::run() {
    while (running_) {
        const bool haveWork = renderer_ != nullptr && pendingFrames_ > 0;
        host_->pump(haveWork);
        if (host_->quitRequested()) {
            running_ = false;
            break;
        }
        if (renderer_ == nullptr || pendingFrames_ <= 0) {
            continue;
        }
        drawFrame();
        pendingFrames_ = 0;
    }
}

void WeekView::drawFrame() {
    const uint32_t width = renderer_->width();
    const uint32_t height = renderer_->height();
    const FontTextMetrics metrics(font_.get());

    curves_.clear();
    shapes_.clear();
    scene_ = buildWeekScene(static_cast<int>(width), static_cast<int>(height), week_, nowUtc_,
                            metrics, theme_);

    Canvas canvas(curves_, width, height, font_.get(), 0.0f, 0.0f, 0.0f, 0.0f);
    canvas.useShapes(&shapes_);
    drawScene(canvas, scene_);
    renderer_->draw(curves_, /*overlay_rotation_deg=*/0, {}, {}, {}, shapes_);
}

void WeekView::onHostResized() { pendingFrames_ = 1; }

void WeekView::onHostLayoutInvalidated() { pendingFrames_ = 1; }

void WeekView::onHostExposed() { pendingFrames_ = 1; }

void WeekView::onKeyDownPortable(int keyCode) {
    switch (keyCode) {
    case key::Escape:
        running_ = false;
        break;
    case key::Left:
        followingNow_ = false;
        goToWeek(anchorUtc_ - kWeekSeconds);
        break;
    case key::Right:
        followingNow_ = false;
        goToWeek(anchorUtc_ + kWeekSeconds);
        break;
    case kKeyT: {
        const long long now = klend_ario::utcNowSeconds();
        nowUtc_ = now;
        followingNow_ = true;
        goToWeek(now);
        break;
    }
    case kKeyH:
        showHidden_ = !showHidden_;
        reload();
        break;
    default:
        break;
    }
}

void WeekView::onLButtonDown(int x, int y) {
    for (const HitTarget &hit : scene_.hits) {
        if (hit.rect.contains(x, y)) {
            if (hit.id == "prev") {
                followingNow_ = false;
                goToWeek(anchorUtc_ - kWeekSeconds);
            } else if (hit.id == "next") {
                followingNow_ = false;
                goToWeek(anchorUtc_ + kWeekSeconds);
            } else if (hit.id == "today") {
                const long long now = klend_ario::utcNowSeconds();
                nowUtc_ = now;
                followingNow_ = true;
                goToWeek(now);
            }
            return;
        }
    }
}

void WeekView::setCursorFor(int x, int y) {
    int want = 0; // arrow
    for (const HitTarget &hit : scene_.hits) {
        if (hit.rect.contains(x, y)) {
            want = 1; // hand
            break;
        }
    }
    if (want != lastCursor_) {
        lastCursor_ = want;
        host_->setCursor(want == 1 ? CursorShape::Hand : CursorShape::Arrow);
    }
}

void WeekView::onMouseMove(int x, int y) { setCursorFor(x, y); }

void WeekView::onMouseLeave() {
    if (lastCursor_ != 0) {
        lastCursor_ = 0;
        host_->setCursor(CursorShape::Arrow);
    }
}

void WeekView::onTimer(int timerId) {
    if (timerId != kPollTimer || !dbOpen_) {
        return;
    }

    // The now-line moves, so a frame is always due; the WEEK and the ROWS are
    // only reloaded when they actually changed.
    nowUtc_ = klend_ario::utcNowSeconds();

    bool changed = false;
    if (auto version = db_.dataVersion(); version.ok() && version.value() != dataVersion_) {
        // Another process (CLI or MCP server) committed. This is how an AI edit
        // reaches an open window without a socket or a file watcher.
        dataVersion_ = version.value();
        changed = true;
    }
    // Following "now" across a week boundary: if the user has not browsed away,
    // the view should be showing the new current week, not last week's.
    if (followingNow_) {
        const long long previousSunday = localWeekContaining(anchorUtc_).days[0].startUtc;
        const long long currentSunday = localWeekContaining(nowUtc_).days[0].startUtc;
        if (previousSunday != currentSunday) {
            anchorUtc_ = nowUtc_;
            changed = true;
        }
    }
    if (changed) {
        reload();
    }
    pendingFrames_ = 1;
}

void WeekView::shutdown() {
    if (host_) {
        host_->stopTimer(kPollTimer);
    }
    renderer_.reset();
    font_.reset();
}

} // namespace klend_ario::gui
