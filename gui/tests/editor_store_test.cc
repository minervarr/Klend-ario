#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include "editor_store.hh"
#include "klend_ario/db.hh"

using namespace klend_ario;
using namespace klend_ario::gui;

static void useZone(const char *zone) {
    setenv("TZ", zone, 1);
    tzset();
}

static void testInsertEditStaleAndDelete() {
    useZone("UTC0");
    auto db = Db::openMemory();
    assert(db.ok());

    EditorState draft = editorForNew(1793865600); // 2026-11-05T08:00Z
    draft.title = "New thing";
    draft.notes = "from the form";
    std::string warning;
    auto inserted = saveEditorToDb(db.value(), draft, warning);
    assert(inserted.ok());
    assert(warning.empty());

    auto stored = db.value().getEvent(inserted.value());
    assert(stored.ok());
    assert(stored.value().title == "New thing");
    assert(stored.value().notes == "from the form");
    assert(stored.value().revision == 1);

    EditorState edit = editorForEvent(stored.value());
    edit.title = "Edited thing";
    edit.importance = "90";
    auto updated = saveEditorToDb(db.value(), edit, warning);
    assert(updated.ok());
    assert(updated.value() == inserted.value());

    auto after = db.value().getEvent(inserted.value());
    assert(after.ok());
    assert(after.value().title == "Edited thing");
    assert(after.value().importance == 90);
    assert(after.value().revision == stored.value().revision + 1);

    // The draft read BEFORE the committed edit is stale; the store must refuse
    // it rather than overwrite the newer title.
    EditorState stale = editorForEvent(stored.value());
    stale.title = "Stale overwrite";
    auto refused = saveEditorToDb(db.value(), stale, warning);
    assert(!refused.ok());
    auto still = db.value().getEvent(inserted.value());
    assert(still.ok());
    assert(still.value().title == "Edited thing");

    // Delete is the row the form was opened for, not a blind id.
    auto removed = deleteEditorFromDb(db.value(), edit);
    assert(removed.ok());
    assert(!db.value().getEvent(inserted.value()).ok());

    // A new form has nothing to delete.
    assert(!deleteEditorFromDb(db.value(), draft).ok());
}

static void testWarningIsSurfacedNotSwallowed() {
    useZone("Europe/Berlin");
    auto db = Db::openMemory();
    assert(db.ok());

    EditorState state = editorForNew(1774744200);
    state.title = "DST";
    state.start = "2026-10-25 02:30";
    state.end = "2026-10-25 04:00";
    std::string warning;
    auto saved = saveEditorToDb(db.value(), state, warning);
    assert(saved.ok());
    assert(!warning.empty());
    assert(warning.find("earlier") != std::string::npos);

    auto stored = db.value().getEvent(saved.value());
    assert(stored.ok());
    assert(stored.value().startUtc == 1792888200); // the earlier 02:30
}

static void testInvalidDraftNeverReachesTheStore() {
    useZone("UTC0");
    auto db = Db::openMemory();
    assert(db.ok());

    EditorState draft = editorForNew(1793865600);
    draft.title.clear();
    std::string warning;
    assert(!saveEditorToDb(db.value(), draft, warning).ok());
    auto all = db.value().listEvents(0, 2000000000);
    assert(all.ok());
    assert(all.value().empty());
}

int main() {
    testInsertEditStaleAndDelete();
    testWarningIsSurfacedNotSwallowed();
    testInvalidDraftNeverReachesTheStore();
    std::printf("editor_store_test: all assertions passed\n");
    return 0;
}
