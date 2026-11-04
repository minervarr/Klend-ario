#pragma once

// The bridge between the pure edit form and the storage layer.
//
// editor.cc decides what the fields MEAN; this file decides what Save and
// Delete DO. Keeping it out of WeekView means the transaction/insert/update
// path can be driven with an in-memory database and no window, and the window
// is left with focus, text input and the error banner.

#include "arc/result.hh"
#include "editor.hh"

namespace klend_ario {
class Db;
}

namespace klend_ario::gui {

// Insert when the form is new, compare-and-swap when it is an edit. On success
// returns the row id (also for an edit) and leaves `warning` as produced by
// buildEventFromEditor. A stale revision is an error, not a silent overwrite.
arc::Result<long long> saveEditorToDb(klend_ario::Db &db, const EditorState &state,
                                      std::string &warning);

// Delete the row the form was opened for. A new form has nothing to delete.
arc::Result<void> deleteEditorFromDb(klend_ario::Db &db, const EditorState &state);

} // namespace klend_ario::gui
