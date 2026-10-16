# sqlite3mc — SQLite3MultipleCiphers (vendored)

Page-level encryption for the calendar database (Phase 3). This replaces the
plain SQLite amalgamation that shipped in Phase 0.

| | |
|---|---|
| Library | [SQLite3MultipleCiphers](https://github.com/utelle/SQLite3MultipleCiphers) by Ulrich Telle |
| Version | **2.6.1** (embeds SQLite **3.54.0**) |
| Release | `v2.6.1`, asset `sqlite3mc-2.6.1-sqlite-3.54.0-amalgamation.zip` |
| SHA-256 of that zip | `3f25af603566a23c821d8e581778b5ece2a541d2435776de57b8d845c176815a` |
| License | MIT (the bundled SQLite is public domain) |

## Files

- `sqlite3mc_amalgamation.c` — the amalgamation. Compiled as the `sqlite3mc`
  CMake target; it is the one C translation unit in the build.
- `sqlite3mc_amalgamation.h` — the single header. It **embeds the whole SQLite
  API** (so `#include "sqlite3mc_amalgamation.h"` gives both `sqlite3_*` and the
  codec calls), then adds `sqlite3_key`/`sqlite3_key_v2`/`sqlite3_rekey`.

`sqlite3ext.h` and the shell are deliberately not vendored: the app is not a
SQLite extension and does not ship a shell.

## Why vendored, not found

Same reason the previous amalgamation was: Linux and Android must build the
same, known version with no package to install and no configure-time download
(see `essentialAgent/MEMORY.md`). The zip's checksum is recorded above so the
exact bytes can be re-verified.

## What we rely on

- `sqlite3_key(db, pass, len)` after `sqlite3_open_v2` — see `core/src/db.cc`.
- The on-disk format is pinned with `PRAGMA cipher = 'chacha20'` (ChaCha20).
  Also this is SQLite3MultipleCiphers' own default; stating it makes the format
  we write an explicit choice rather than a library default that could change.
