#pragma once

// The weekly view as an application: window, database, input, redraw.
//
// It owns exactly the things the pure half cannot: a Host, a Renderer, the font,
// a read-only database connection and a one-second poll. Every decision about
// what a frame LOOKS like stays in scene.cc; this class only decides WHEN to
// rebuild the scene and what a key or a click means.
//
// Read-only on purpose for this slice (PLAN.md Phase 4a): the CLI and the MCP
// server are the write paths, and a viewer that cannot corrupt the calendar is
// the smallest honest first GUI. Editing is the next slice.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "app_view.hh"
#include "editor.hh"
#include "frame_input.hh"
#include "klend_ario/db.hh"
#include "scene.hh"
#include "theme.hh"
#include "widgets.hh" // TextFieldState / ClipboardIo

class Host;
class RasterFont;
class Renderer;
struct Font;
struct ImageDraw;

namespace klend_ario::gui {

struct ViewOptions {
    std::string dbPath;   // empty = the app's state database
    std::string keyFile;  // empty = $KLEND_ARIO_KEY, then none
    bool showHidden = false;
    bool fullscreen = false; // windowed 1280x720 by default; see create()
    bool readOnly = false;   // viewer deployment: no editor, no writes
    std::string screenshotDir; // empty = the app's data directory
};

class WeekView : public AppView {
public:
    WeekView();
    ~WeekView() override;

    // Opens the window. Returns false with a reason already printed to stderr.
    bool create(const ViewOptions &options);
    void run();

    // --- AppView ---------------------------------------------------------
    void onHostResized() override;
    void onHostLayoutInvalidated() override;
    void onHostExposed() override;
    void onKeyDownPortable(int keyCode) override;
    void onKeyUpPortable(int keyCode) override;
    void onCharPortable(uint32_t codepoint) override;
    void onTextEditPortable(const std::string &text, size_t cursorByte) override;
    void onLButtonDown(int x, int y) override;
    void onMouseMove(int x, int y) override;
    void onMouseLeave() override;
    void onTimer(int timerId) override;
    void shutdown() override;

private:
    bool openDatabase(const ViewOptions &options);
    bool loadFont();
    void ensureText();
    bool reload();
    void drawFrame();
    void goToWeek(long long anchorUtc);
    void setCursorFor(int x, int y);

    // The edit modal. The window owns focus and text input; editor.cc owns the
    // meaning of every field.
    void beginEditSelected();
    void beginNewAt(long long startUtc);
    void closeEditor();
    void applyEditorInput();
    void syncTextField();
    void setEditorFocus(EditorField field);
    void saveEditor();
    void deleteEditor();
    std::string *focusedText();
    bool handleEditorHit(const HitTarget &hit);
    void captureScreenshot();

    std::unique_ptr<Host> host_;
    std::unique_ptr<Renderer> renderer_;
    // The normal text path: a per-size FreeType coverage atlas, drawn as quads
    // through the shared text pipeline. `outlineFont_` exists only so a broken
    // install still draws something; it is the legacy 1-bit outline path.
    std::unique_ptr<RasterFont> font_;
    std::unique_ptr<Font> outlineFont_;

    klend_ario::Db db_;
    bool dbOpen_ = false;

    long long anchorUtc_ = 0; // an instant inside the week being shown
    long long selectedEventId_ = 0; // 0 = nothing selected; 4b's editor target
    long long nowUtc_ = 0;
    long long drawnNowUtc_ = 0; // the `now` the last built scene used
    long long dataVersion_ = -1;
    bool followingNow_ = true; // false while browsing another week

    WeekModel week_;
    Scene scene_;
    Theme theme_;
    std::string dbPath_;
    std::string screenshotDir_;
    bool showHidden_ = false;
    bool readOnly_ = false;
    bool screenshotRequested_ = false;

    EditorState editor_;
    FrameInput input_;
    widgets::TextFieldState textField_;
    EditorField textFieldOwner_ = EditorField::Count;

    std::vector<float> curves_;
    std::vector<float> shapes_;
    std::vector<float> textQuads_;
    std::vector<int> glyphSizes_; // the size set currently baked in font_
    int minTextPx_ = 0;           // kMinReadableTextSizePx, passed to the scene
    uint32_t textWidth_ = 0;      // the renderer extent the glyphs were baked for
    uint32_t textHeight_ = 0;
    bool textDirty_ = true;       // codepoints/sizes may have changed
    bool redrawAgain_ = false;    // drawFrame() saw the extent move mid-draw
    int pendingFrames_ = 1;
    bool running_ = true;
    int lastCursor_ = -1;
};

} // namespace klend_ario::gui
