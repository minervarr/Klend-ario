#include "week_view.hh"

#include <cmath>
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
#include "raster_font.hh"
#include "refresh.hh"
#include "renderer.hh"
#include "scene_render.hh"
#include "text_inputs.hh"
#include "ui_metrics.hh"
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
    // The weekly view is designed 16:9, and 1280x720 is the cheapest clean
    // surface. Windowed also keeps a 4K desktop from handing the app a 4K
    // swapchain (four full-screen images) it does not need. `--fullscreen`
    // restores the historical mode; a tiling compositor sizes the tile itself.
    WindowOptions window;
    window.width = 1280;
    window.height = 720;
    window.fullscreen = options.fullscreen;
    host_->setWindowOptions(window);
    if (!host_->init(this)) {
        std::fprintf(stderr, "error: could not create the window\n");
        return false;
    }

    if (!openDatabase(options)) {
        return false;
    }
    minTextPx_ = static_cast<int>(std::ceil(kMinReadableTextSizePx));
    if (!loadFont()) {
        // A missing font is not fatal: Canvas falls back to stroke outlines.
        std::fprintf(stderr, "warning: fonts/ui/ui.otf not found; using outline text\n");
    }

    try {
        // A viewer drawn on a clock, not a live preview: FIFO paces it and there
        // is no "newest frame" to prefer (PresentPolicy in vk_canvas). Two
        // images is Vulkan's minimum and enough for FIFO — MAILBOX is the policy
        // that wants three or more. The surface may clamp it upward (sway
        // advertises a minimum of 4), which is fine and expected.
        renderer_ = std::make_unique<Renderer>(host_->surfaceProvider(), host_->assetReader(),
                                               /*desiredSwapchainImages=*/2, OutputTarget::SdrSrgb,
                                               PresentPolicy::Vsync);
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
    // Bake before the window is shown: the first glyph set costs a few tens of
    // milliseconds with FreeType, and paying it behind a black window is worse
    // than paying it once at startup.
    ensureText();
    textDirty_ = false; // baked for this extent; the first frame re-checks it

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
    const std::string path = host_->exeDir() + "fonts/ui/ui.otf";

    // The normal path: per-size FreeType coverage in an R8 atlas, drawn as
    // quads. It is the framework's "Bézier outlines, rasterized once and
    // cached" text renderer — not MTSDF. See essentialAgent/GUI.md.
    font_ = std::make_unique<RasterFont>();
    if (font_->open(host_->dataReader(), path.c_str())) {
        return true;
    }
    font_.reset();

    // A broken install must still show text. The legacy outline path is 1-bit,
    // but it is a fallback, not the product's text renderer.
    std::fprintf(stderr, "warning: cannot open the text atlas; using outline text\n");
    outlineFont_ = std::make_unique<Font>();
    std::vector<uint8_t> bytes;
    if (host_->dataReader().read(path.c_str(), bytes) && !bytes.empty() &&
        outlineFont_->loadFromMemory(bytes.data(), bytes.size())) {
        return true;
    }
    outlineFont_.reset();
    return false;
}

void WeekView::ensureText() {
    if (renderer_ == nullptr) {
        return;
    }

    // Record the extent this bake is for. drawFrame() re-arms a bake when the
    // renderer's extent moves — including the recreate that happens INSIDE
    // draw(), one frame after onHostResized() — because the role sizes are
    // derived from the content rect.
    textWidth_ = renderer_->width();
    textHeight_ = renderer_->height();
    if (!font_) {
        return;
    }

    const int width = static_cast<int>(textWidth_);
    const int height = static_cast<int>(textHeight_);
    const UiMetrics metrics = metricsFor(frameFor(width, height).content, minTextPx_);
    const TextInputs inputs = collectTextInputs(week_, metrics);
    if (inputs.codepoints.empty() || inputs.sizesPx.empty()) {
        return;
    }

    // A resize changes the role sizes; a per-size cache would otherwise keep
    // every intermediate set forever. Reset the sheet and bake the current set.
    if (inputs.sizesPx != glyphSizes_) {
        font_->reset();
        glyphSizes_ = inputs.sizesPx;
    }

    const int added = font_->ensureGlyphs(inputs.codepoints, inputs.sizesPx);
    if (added > 0 || !renderer_->msdfReady()) {
        renderer_->initMsdf(*font_);
    }
}

bool WeekView::reload() {
    if (!dbOpen_) {
        return false;
    }
    const auto visibility = showHidden_ ? klend_ario::Visibility::Any
                                        : klend_ario::Visibility::Visible;
    auto model = loadWeekModel(db_, anchorUtc_, nowUtc_, visibility);
    if (!model) {
        std::fprintf(stderr, "error: %s\n", model.error().message.c_str());
        return false;
    }
    week_ = std::move(model.value());
    // A new week means new titles: the baked codepoint set may not cover them.
    textDirty_ = true;
    pendingFrames_ = 1;
    return true;
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
        // A cell the scene asked for and the cache did not have was recorded
        // during drawFrame(). Arm the next pass AFTER the decrement above: a
        // pendingFrames_ = 1 set inside drawFrame() would be erased right here.
        if (font_ && font_->hasMisses()) {
            pendingFrames_ = 1;
        }
        // Same rule for a swapchain recreate that landed mid-draw: the scene it
        // rendered was measured at the old extent, so one more frame is owed.
        if (redrawAgain_) {
            redrawAgain_ = false;
            pendingFrames_ = 1;
        }
    }
}

void WeekView::drawFrame() {
    const uint32_t width = renderer_->width();
    const uint32_t height = renderer_->height();

    // Bake what the PREVIOUS frame asked for and did not have. This runs before
    // the scene is built, so the next pass measures and draws with the cells.
    if (font_ && font_->hasMisses()) {
        if (font_->bakeMisses() > 0) {
            renderer_->initMsdf(*font_);
        }
    }
    // The glyph sizes follow the content rect, which follows the extent. Compare
    // extents rather than trusting the resize callback: recreate_swapchain()
    // runs inside draw(), so the frame right after onHostResized() still reports
    // the OLD extent.
    if (width != textWidth_ || height != textHeight_) {
        textDirty_ = true;
    }
    if (textDirty_) {
        ensureText();
        textDirty_ = false;
    }

    const FontTextMetrics metrics(font_.get(), outlineFont_.get());

    curves_.clear();
    shapes_.clear();
    textQuads_.clear();
    scene_ = buildWeekScene(static_cast<int>(width), static_cast<int>(height), week_, nowUtc_,
                            metrics, theme_, minTextPx_);
    // The now-line position this scene was built for. onTimer() compares
    // against it instead of repainting every second.
    drawnNowUtc_ = nowUtc_;

    Canvas canvas(curves_, width, height, outlineFont_.get(), 0.0f, 0.0f, 0.0f, 0.0f);
    canvas.useShapes(&shapes_);
    if (font_ && font_->valid()) {
        canvas.useMsdf(font_.get(), &textQuads_);
    }
    drawScene(canvas, scene_);
    // Text quads are the FIFTH argument; shapes are the sixth. Passing them in
    // the shape slot gives a blank text layer and a shape pipeline that reads
    // glyph vertices as rectangles.
    renderer_->draw(curves_, /*overlay_rotation_deg=*/0, {}, {}, textQuads_, shapes_);

    // draw() is where a resized surface recreates its swapchain, which changes
    // the renderer's extent AFTER this scene was measured at the old one. The
    // next frame must be built at the new size; at a once-a-minute cadence
    // nothing else would correct the stretch. run() consumes the flag, because
    // pendingFrames_ set here would be cleared right after drawFrame().
    if (renderer_->width() != width || renderer_->height() != height) {
        textDirty_ = true;
        redrawAgain_ = true;
    }
}

void WeekView::onHostResized() {
    // The scene and the glyph sizes read the RENDERER's extent, and the
    // renderer only learns the surface's new size when its swapchain is
    // recreated. Do that here so the frame armed below is built at the real
    // size, not at the previous one.
    if (renderer_ != nullptr) {
        renderer_->notifyResized();
    }
    textDirty_ = true;
    pendingFrames_ = 1;
}

void WeekView::onHostLayoutInvalidated() {
    // Insets can change the content rect the type sizes are derived from.
    textDirty_ = true;
    pendingFrames_ = 1;
}

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

    // The timer is a POLL, not a paint. It runs once a second because that is
    // the product's "an AI edit appears live" contract, but a frame is owed
    // only when the clock or the database says something visible changed. This
    // used to end with an unconditional pendingFrames_ = 1, which repainted an
    // identical full-screen frame every second. See GUI_PERFORMANCE.md.
    nowUtc_ = klend_ario::utcNowSeconds();

    bool dataChanged = false;
    long long observedVersion = dataVersion_;
    if (auto version = db_.dataVersion(); version.ok()) {
        observedVersion = version.value();
        dataChanged = observedVersion != dataVersion_;
    }

    RefreshInputs in;
    in.nowUtc = nowUtc_;
    in.drawnNowUtc = drawnNowUtc_;
    in.weekStartUtc = week_.days.front().day.startUtc;
    in.weekEndUtc = week_.days.back().day.endUtc;
    in.dataChanged = dataChanged;
    in.followingNow = followingNow_;
    for (const DayInfo &day : week_.days) {
        if (!day.isToday) {
            continue;
        }
        in.weekHadToday = true;
        if (nowUtc_ >= day.day.startUtc && nowUtc_ < day.day.endUtc) {
            in.shownDayIsToday = true;
        }
    }

    const Refresh decision = refreshDecision(in);
    if (decision == Refresh::None) {
        return;
    }

    if (decision == Refresh::Redraw) {
        pendingFrames_ = 1;
        return;
    }

    // Reload. Following "now" out of the shown week moves the anchor to the
    // new current week; the same rollover while BROWSING must not jump the
    // user anywhere — it only clears the now-line and today tint that are now
    // stale.
    if (followingNow_ && (nowUtc_ < in.weekStartUtc || nowUtc_ >= in.weekEndUtc)) {
        anchorUtc_ = nowUtc_;
    }
    // Advance the version only once the query succeeded: a transient database
    // error must be retried on the next tick, not silence the poll forever.
    if (reload() && dataChanged) {
        dataVersion_ = observedVersion;
    }
}

void WeekView::shutdown() {
    if (host_) {
        host_->stopTimer(kPollTimer);
    }
    renderer_.reset();
    font_.reset();
    outlineFont_.reset();
}

} // namespace klend_ario::gui
