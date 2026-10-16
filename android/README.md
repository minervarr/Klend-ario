# android/ — later

The Android platform layer (Phase 7). It will carry the JNI + Gradle build that
reaches up into `core/`, and the platform-specific pieces that `core/` must not
know about — the app's private directory via `arc::fs::setPlatformDirs()`, and
storage permissions.

Empty on purpose: nothing here ships, and no Android work starts before its
phase (see `../essentialAgent/PLAN.md`).
