# android/ — the Android platform layer (Phase 7)

**Status: not started, and not buildable on the machine that has written
everything before it** — an Android build needs the NDK, the SDK and Gradle,
none of which were present. Nothing here is faked: this file is the plan, kept
so the layer is not designed twice.

## The shape (already decided, see `../essentialAgent/MEMORY.md`)

Android is a **third frontend**, not a third app. Exactly what has to differ:

1. **The private directory is pushed in, not derived.** `arc::fs` cannot know
   `/data/user/0/<pkg>/files` — it carries the package name and the user id — so
   the platform layer calls `arc::fs::setPlatformDirs()` once, before any
   `arc::fs::layout()` call, with `ANativeActivity::internalDataPath` (or
   `getFilesDir()`) and `Environment.getExternalStorageDirectory()`. With
   neither supplied `arc_fs` still works; this makes the state directory land in
   real ext4 instead of the FUSE mount. That split is why the database is in
   `state` at all.

2. **Storage permission.** Reading/writing the user-visible `home` tree on
   shared storage needs the runtime permission; the app must not assume it.

3. **The same `core/`.** Not one line of core changes — that is the invariant the
   whole split exists to protect (`core/` has no JNI, no `__ANDROID__`). The
   Android layer links the same `klend_ario_core`, the GUI renders the same
   `gui/week_layout` geometry, and the CLI's and MCP server's logic is unchanged.

## What the build will look like

- `android/CMakeLists.txt` reaching **up** into `../core` and `../gui`, the way
  Matrix_Player's Android build reaches into its own tree.
- Gradle + the NDK, one `main.cc` that constructs the app and hands it an
  Android host (the `app_shell` shape), and the JNI glue for `setPlatformDirs`
  and the storage permission.
- The `SQLite3MultipleCiphers` amalgamation cross-compiles as-is (it is plain C).

Do this where an SDK exists, and keep every pure decision it makes assertable in
`core/`'s and `gui/`'s tests, which is where they already are.
