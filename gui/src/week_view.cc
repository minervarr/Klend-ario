#include "week_view.hh"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <utility>
#include <vector>

#include "arc/fs/paths.hh"
#include "canvas.hh"
#include "editor_scene.hh"
#include "editor_store.hh"
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
#include "stb_image_write.h"
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
constexpr int kKeyN = 0x4E;
constexpr int kKeyF12 = key::F1 + 11;

// The host clipboard behind the text field widget. Core cannot call Wayland;
// this adapter is the one place the editor touches it.
class HostClipboard : public widgets::ClipboardIo {
public:
    explicit HostClipboard(Host *host) : host_(host) {}
    void setText(const std::string &utf8) override { host_->setClipboardText(utf8); }
    std::string getText() override { return host_->getClipboardText(); }

private:
    Host *host_;
};

// Every string the modal can draw. Static labels are cheap; the field values
// are what may add a codepoint the week never used (an accented title, a
// pasted name). Keeping the list here means a new label cannot be added to the
// scene and forgotten by the glyph bake.
std::vector<std::string> editorTextStrings(const EditorState &editor) {
    std::vector<std::string> texts = {
        "New event",
        "Edit event",
        "Title",
        "Start (local)",
        "End (local)",
        "Type",
        "Importance",
        "Urgency",
        "Alarm offset (s)",
        "Hidden",
        "Notes",
        "Save",
        "Delete",
        "Confirm delete",
        "Cancel",
        "YYYY-MM-DD HH:MM",
        "Untitled",
        "seconds",
        "optional",
        "0..100",
        "event",
        "sleep",
        "wake",
        "alarm",
        "Delete this event? Click Delete again or press Enter.",
        "UTC --",
    };
    // The UTC hints are resolved from the live field text; an invalid value
    // draws the placeholder above. ASCII-only, but listing them keeps the
    // "every string the modal can draw" contract true by construction.
    texts.push_back("UTC " + editorUtcHint(editor, EditorField::Start));
    texts.push_back("UTC " + editorUtcHint(editor, EditorField::End));
    texts.push_back(editor.title);
    texts.push_back(editor.start);
    texts.push_back(editor.end);
    texts.push_back(editor.type);
    texts.push_back(editor.importance);
    texts.push_back(editor.urgency);
    texts.push_back(editor.alarmOffset);
    texts.push_back(editor.notes);
    texts.push_back(editor.error);
    texts.push_back(editor.warning);
    return texts;
}

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
    if (options.screenshotDir.empty()) {
        // The app's own data directory. Never the user's Pictures: ensureLayout
        // is scoped to App, and a screenshot is an app artifact, not something
        // we are entitled to scatter into a user-owned folder.
        const arc::fs::Layout layout = arc::fs::layout("klend_ario");
        (void)arc::fs::ensureLayout(layout, arc::fs::LayoutScope::App);
        screenshotDir_ = layout.data.empty() ? layout.state : layout.data;
    } else {
        screenshotDir_ = options.screenshotDir;
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
    readOnly_ = options.readOnly;
    dbPath_ = options.dbPath;
    if (dbPath_.empty()) {
        const arc::fs::Layout layout = arc::fs::layout("klend_ario");
        if (!readOnly_) {
            // The app owns state/cache/config/data; it never creates the user's
            // Music/Downloads/Pictures/Documents (AGENTS.md §2.3).
            if (!arc::fs::ensureLayout(layout, arc::fs::LayoutScope::App)) {
                std::fprintf(stderr, "error: cannot create the app's state directory\n");
                return false;
            }
        }
        dbPath_ = arc::fs::join(layout.state, "klend_ario.db");
    }

    std::string error;
    const std::string passphrase = resolvePassphrase(options, error);
    if (!error.empty()) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return false;
    }

    // Writable by default: the weekly view is the editor now. `--read-only`
    // keeps the old viewer contract, where a missing path must not become a
    // silent empty week. The writable path may create and migrate — that is
    // what makes the first run work without a CLI.
    auto opened =
        readOnly_
            ? (passphrase.empty() ? klend_ario::Db::openReadOnly(dbPath_)
                                  : klend_ario::Db::openEncryptedReadOnly(dbPath_, passphrase))
            : (passphrase.empty() ? klend_ario::Db::open(dbPath_)
                                  : klend_ario::Db::openEncrypted(dbPath_, passphrase));
    if (!opened) {
        std::fprintf(stderr, "error: %s\n", opened.error().message.c_str());
        std::fprintf(stderr, "       database: %s\n", dbPath_.c_str());
        if (passphrase.empty()) {
            std::fprintf(stderr, "       (if it is encrypted, pass --key-file <path> or set "
                                 "$KLEND_ARIO_KEY)\n");
        }
        if (readOnly_) {
            std::fprintf(stderr,
                         "       --read-only needs an existing calendar; run without it to "
                         "create one\n");
        }
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
    TextInputs inputs = collectTextInputs(week_, metrics);
    if (editor_.open) {
        const std::vector<int> editorSizes = {
            metrics.editorTitleSize, metrics.editorLabelSize, metrics.editorHintSize,
            metrics.editorFieldSize, metrics.editorButtonSize, metrics.editorErrorSize};
        inputs = mergeTextInputs(
            inputs, collectEditorTextInputs(editorTextStrings(editor_), editorSizes));
    }
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
    // A selection belongs to the week it was made in. Navigating, toggling
    // hidden rows or an external delete can all remove it; never leave an id
    // highlighted that is not on screen.
    if (selectedEventId_ != 0) {
        bool present = false;
        for (const Event &event : week_.events) {
            if (event.id == selectedEventId_) {
                present = true;
                break;
            }
        }
        if (!present) {
            selectedEventId_ = 0;
        }
    }
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
        // Edges are per frame: clear before pump, then consume what arrived.
        // This is the contract FrameInput documents; clearing after pump would
        // silently drop the events the frame was about to handle.
        input_.beginFrame();
        const bool editorWasOpen = editor_.open;
        const bool haveWork = renderer_ != nullptr && pendingFrames_ > 0;
        host_->pump(haveWork);
        if (host_->quitRequested()) {
            running_ = false;
            break;
        }
        // Do not consume the key that OPENED the modal in the same frame:
        // Enter is both "edit this event" and "save", and applying it twice
        // would open and immediately save an empty form.
        if (editor_.open && editorWasOpen) {
            applyEditorInput();
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
                            metrics, theme_, minTextPx_, selectedEventId_);
    if (editor_.open) {
        // The renderer batches every text op after every shape, so the week's
        // day headers would paint OVER the modal's panel fill even though they
        // were issued first. Drop the week's text; its event rectangles still
        // give date context, and the modal owns the text layer for this frame.
        scene_.ops.erase(
            std::remove_if(scene_.ops.begin(), scene_.ops.end(),
                           [](const Op &op) { return op.kind == OpKind::Text; }),
            scene_.ops.end());
        Scene modal = buildEditorScene(static_cast<int>(width), static_cast<int>(height),
                                       editor_, metrics, theme_, minTextPx_);
        scene_.ops.insert(scene_.ops.end(), modal.ops.begin(), modal.ops.end());
        scene_.hits.insert(scene_.hits.end(), modal.hits.begin(), modal.hits.end());
    }
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
    const bool screenshotFrame = screenshotRequested_;
    if (screenshotFrame) {
        // Do not present this frame: presenting goes through the compositor and
        // can recycle the image before readback. The capture path is the same
        // one the headless tool uses, where present is disabled for exactly
        // this reason. The next frame presents normally.
        renderer_->setPresentEnabled(false);
    }
    renderer_->draw(curves_, /*overlay_rotation_deg=*/0, {}, {}, textQuads_, shapes_);

    if (screenshotFrame) {
        screenshotRequested_ = false;
        captureScreenshot();
        renderer_->setPresentEnabled(true);
        // drawFrame() set pendingFrames_ would be cleared by run(); use the
        // existing "one more pass" flag so the captured frame is shown.
        redrawAgain_ = true;
    }

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
    input_.onKey(KeyEvent{keyCode, /*down=*/true});
    if (keyCode == kKeyF12) {
        // The window's own capture, not the dev fixture tool. It works in the
        // Release binary and includes whatever is on screen (the editor too).
        screenshotRequested_ = true;
        pendingFrames_ = 1;
        return;
    }
    if (editor_.open) {
        // The modal consumes keys after pump(), where textFieldHandleInput can
        // see the same edge. Handling Escape/Enter here as well would apply
        // them twice.
        return;
    }
    switch (keyCode) {
    case key::Escape:
        // First Escape drops the selection; a second one quits. That keeps the
        // old muscle memory while giving the editor a cancel key.
        if (selectedEventId_ != 0) {
            selectedEventId_ = 0;
            pendingFrames_ = 1;
        } else {
            running_ = false;
        }
        break;
    case key::Left:
        followingNow_ = false;
        goToWeek(anchorUtc_ - kWeekSeconds);
        break;
    case key::Right:
        followingNow_ = false;
        goToWeek(anchorUtc_ + kWeekSeconds);
        break;
    case key::Enter:
        if (selectedEventId_ != 0) {
            beginEditSelected();
        }
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
    case kKeyN:
        beginNewAt(nowUtc_);
        break;
    default:
        break;
    }
}

void WeekView::onKeyUpPortable(int keyCode) {
    input_.onKey(KeyEvent{keyCode, /*down=*/false});
}

void WeekView::onCharPortable(uint32_t codepoint) {
    input_.onChar(CharEvent{codepoint});
}

void WeekView::onTextEditPortable(const std::string &text, size_t cursorByte) {
    input_.onTextEdit(TextEditEvent{text, cursorByte});
}

void WeekView::onLButtonDown(int x, int y) {
    // Reverse order: the modal is appended after the week, so it is on top.
    // Walking forwards would let a day column steal a click from a field that
    // visually covers it.
    for (auto it = scene_.hits.rbegin(); it != scene_.hits.rend(); ++it) {
        const HitTarget &hit = *it;
        if (!hit.rect.contains(x, y)) {
            continue;
        }
        if (editor_.open) {
            if (handleEditorHit(hit)) {
                pendingFrames_ = 1;
            }
            return;
        }
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
        } else if (hit.id.rfind("event:", 0) == 0) {
            const long long id = std::strtoll(hit.id.c_str() + 6, nullptr, 10);
            if (selectedEventId_ == id) {
                // Second click on the same box: open the form. This is the
                // double-click gesture without a separate event type.
                beginEditSelected();
            } else {
                selectedEventId_ = id;
                pendingFrames_ = 1;
            }
        }
        return;
    }
    if (editor_.open) {
        return; // a click outside the panel must not dismiss unsaved work
    }
    // A click on empty chrome clears the selection. Creating an event from a
    // click is the next slice; clearing is already the right "deselect" verb.
    if (selectedEventId_ != 0) {
        selectedEventId_ = 0;
        pendingFrames_ = 1;
    }
}

std::string *WeekView::focusedText() {
    switch (editor_.focus) {
    case EditorField::Title:
        return &editor_.title;
    case EditorField::Start:
        return &editor_.start;
    case EditorField::End:
        return &editor_.end;
    case EditorField::Type:
        return &editor_.type;
    case EditorField::Importance:
        return &editor_.importance;
    case EditorField::Urgency:
        return &editor_.urgency;
    case EditorField::AlarmOffset:
        return &editor_.alarmOffset;
    case EditorField::Notes:
        return &editor_.notes;
    case EditorField::Hidden:
    case EditorField::Save:
    case EditorField::Delete:
    case EditorField::Cancel:
    case EditorField::Count:
        return nullptr;
    }
    return nullptr;
}

void WeekView::syncTextField() {
    if (!editorFieldIsText(editor_.focus)) {
        textFieldOwner_ = editor_.focus;
        return;
    }
    if (textFieldOwner_ == editor_.focus) {
        return;
    }
    textFieldOwner_ = editor_.focus;
    const std::string *value = focusedText();
    textField_.text = value != nullptr ? *value : std::string{};
    textField_.cursorByte = textField_.text.size();
    textField_.selectionAnchor = textField_.cursorByte;
}

void WeekView::setEditorFocus(EditorField field) {
    if (editor_.focus == field) {
        return;
    }
    editor_.focus = field;
    syncTextField();
    pendingFrames_ = 1;
}

void WeekView::beginEditSelected() {
    if (editor_.open || readOnly_ || selectedEventId_ == 0) {
        return;
    }
    for (const Event &event : week_.events) {
        if (event.id != selectedEventId_) {
            continue;
        }
        editor_ = editorForEvent(event);
        textFieldOwner_ = EditorField::Count;
        syncTextField();
        textDirty_ = true;
        pendingFrames_ = 1;
        return;
    }
}

void WeekView::beginNewAt(long long startUtc) {
    if (editor_.open || readOnly_) {
        return;
    }
    editor_ = editorForNew(startUtc);
    selectedEventId_ = 0;
    textFieldOwner_ = EditorField::Count;
    syncTextField();
    textDirty_ = true;
    pendingFrames_ = 1;
}

void WeekView::closeEditor() {
    editor_ = EditorState{};
    textFieldOwner_ = EditorField::Count;
    textField_ = widgets::TextFieldState{};
    textDirty_ = true;
    pendingFrames_ = 1;
}

bool WeekView::handleEditorHit(const HitTarget &hit) {
    if (hit.id == "editor:save") {
        saveEditor();
        return true;
    }
    if (hit.id == "editor:delete") {
        deleteEditor();
        return true;
    }
    if (hit.id == "editor:cancel") {
        closeEditor();
        return true;
    }
    if (hit.id.rfind("field:", 0) != 0) {
        return false;
    }
    EditorField field = EditorField::Title;
    if (!editorFieldFromName(hit.id.substr(6), field)) {
        return false;
    }
    if (field == EditorField::Hidden) {
        editor_.hidden = !editor_.hidden;
        editor_.acceptAmbiguous = false;
    }
    setEditorFocus(field);
    return true;
}

void WeekView::applyEditorInput() {
    if (!editor_.open) {
        return;
    }
    syncTextField();

    if (editorFieldIsText(editor_.focus)) {
        HostClipboard clipboard(host_.get());
        if (widgets::textFieldHandleInput(textField_, input_, &clipboard)) {
            std::string *value = focusedText();
            if (value != nullptr) {
                *value = textField_.text;
            }
            editor_.error.clear();
            editor_.warning.clear();
            editor_.acceptAmbiguous = false;
            textDirty_ = true; // a pasted/typed codepoint may be new to the atlas
            pendingFrames_ = 1;
        }
    }

    if (input_.keyWentDown(key::Escape)) {
        if (editor_.confirmDelete) {
            editor_.confirmDelete = false;
            pendingFrames_ = 1;
        } else {
            closeEditor();
        }
        return;
    }
    if (input_.keyWentDown(key::Tab)) {
        setEditorFocus(editorFieldNext(editor_.focus, input_.shiftDown));
        return;
    }
    if (input_.keyWentDown(key::Enter)) {
        if (editor_.focus == EditorField::Delete) {
            deleteEditor();
        } else if (editor_.focus == EditorField::Cancel) {
            closeEditor();
        } else {
            saveEditor();
        }
        return;
    }
    if (editor_.focus == EditorField::Hidden && input_.keyWentDown(key::Space)) {
        editor_.hidden = !editor_.hidden;
        editor_.acceptAmbiguous = false;
        pendingFrames_ = 1;
    }
}

void WeekView::saveEditor() {
    if (!editor_.open) {
        return;
    }
    std::string warning;
    auto built = buildEventFromEditor(editor_, warning);
    if (!built) {
        editor_.error = built.error().message;
        editor_.warning.clear();
        pendingFrames_ = 1;
        return;
    }
    if (!warning.empty() && !editor_.acceptAmbiguous) {
        // A fall-back wall clock has two instants. Show which one was chosen
        // and require a second Save; editing again resets the acknowledgement.
        editor_.warning = warning + "; press Save again to accept";
        editor_.error.clear();
        editor_.acceptAmbiguous = true;
        pendingFrames_ = 1;
        return;
    }

    std::string storedWarning;
    auto saved = saveEditorToDb(db_, editor_, storedWarning);
    if (!saved) {
        editor_.error = saved.error().message;
        pendingFrames_ = 1;
        return;
    }
    selectedEventId_ = saved.value();
    closeEditor();
    reload();
}

void WeekView::deleteEditor() {
    if (!editor_.open || editor_.isNew) {
        return;
    }
    if (!editor_.confirmDelete) {
        editor_.confirmDelete = true;
        editor_.error.clear();
        pendingFrames_ = 1;
        return;
    }
    auto removed = deleteEditorFromDb(db_, editor_);
    if (!removed) {
        editor_.error = removed.error().message;
        pendingFrames_ = 1;
        return;
    }
    closeEditor();
    selectedEventId_ = 0;
    reload();
}

void WeekView::captureScreenshot() {
    if (renderer_ == nullptr) {
        return;
    }
    std::vector<uint8_t> rgba;
    uint32_t width = 0;
    uint32_t height = 0;
    if (!renderer_->readbackLastFrame(rgba, width, height)) {
        std::fprintf(stderr, "error: screenshot readback failed\n");
        return;
    }

    // UTC filename, because the calendar stores UTC and a screenshot of a
    // week is evidence about the same instants. `formatIso8601` gives
    // 2026-11-04T23:15:00Z; keep the digits only.
    const std::string iso = klend_ario::formatIso8601(klend_ario::utcNowSeconds());
    std::string stamp;
    for (const char ch : iso) {
        if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
            stamp.push_back(ch);
        }
    }
    if (stamp.size() >= 14) {
        stamp = stamp.substr(0, 8) + "-" + stamp.substr(8, 6);
    }
    const std::string name = "klend_ario-" + stamp + ".png";
    const std::string path = arc::fs::join(screenshotDir_, name);

    if (stbi_write_png(path.c_str(), static_cast<int>(width), static_cast<int>(height), 4,
                       rgba.data(), static_cast<int>(width * 4)) == 0) {
        std::fprintf(stderr, "error: cannot write screenshot: %s\n", path.c_str());
        return;
    }
    std::fprintf(stderr, "screenshot: %s (%ux%u)\n", path.c_str(), width, height);
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

    if (editor_.open && dataChanged && editor_.warning.empty()) {
        // The draft is not clobbered; the revision token will decide at Save.
        // Say so now instead of failing with an unexplained stale-save error.
        editor_.warning = "calendar changed externally; Save will be checked";
        pendingFrames_ = 1;
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
