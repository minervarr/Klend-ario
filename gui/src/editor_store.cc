#include "editor_store.hh"

#include "klend_ario/db.hh"

namespace klend_ario::gui {

arc::Result<long long> saveEditorToDb(klend_ario::Db &db, const EditorState &state,
                                      std::string &warning) {
    auto built = buildEventFromEditor(state, warning);
    if (!built) {
        return built.error();
    }
    if (state.isNew) {
        return db.insertEvent(built.value());
    }
    auto updated = db.updateEvent(built.value());
    if (!updated) {
        return updated.error();
    }
    return built.value().id;
}

arc::Result<void> deleteEditorFromDb(klend_ario::Db &db, const EditorState &state) {
    if (state.isNew || state.id <= 0) {
        return arc::Error{arc::ErrorKind::Other, 0, "nothing to delete"};
    }
    return db.removeEvent(state.id);
}

} // namespace klend_ario::gui
