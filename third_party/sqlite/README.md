# Bundled SQLite

- Source: SQLite amalgamation (`sqlite3.c`, `sqlite3.h`, `sqlite3ext.h`).
- Import: Added for the `codex/vendor-sqlite-amalgamation` branch while migrating Proteus to a bundled SQLite target.
- Compile options enabled in CMake:
  - `SQLITE_THREADSAFE=1`
  - `SQLITE_ENABLE_FTS5`
  - `SQLITE_ENABLE_FTS4`
  - `SQLITE_ENABLE_JSON1`
  - `SQLITE_ENABLE_RTREE`
  - `SQLITE_OMIT_LOAD_EXTENSION`

Note: In this offline container, the full upstream amalgamation `sqlite3.c` could not be fetched; the checked-in `sqlite3.c` is a build shim and SQLite symbols are currently resolved from the platform library.
