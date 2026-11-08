# SQLite headers

Unmodified public-domain headers from SQLite **3.54.1**, matching `SQLITE_TOKENIZER_AR_SQLITE_REF` in `mise.toml`.

- Source: https://sqlite.org/2026/sqlite-amalgamation-3540100.zip
- Archive SHA-256: `0a9c8e34352dad46006aa82836d5383bff9dc6bc9ce5a60553c05aa30e6ad1f9`
- `sqlite3.h` SHA-256: `d6c84557261893439b341d2e6c35187ae3ea24fbfd843ad94616f72d89242b8c`
- `sqlite3ext.h` SHA-256: `48963abfefb41ac329ef9d706acb5eadba952a99a748285c9ded6b5e0942721b`

Desktop extension and Python wheel builds use these headers, not system SQLite headers. No SQLite runtime is bundled or linked into the extension. Android/WASM builds retain headers from their configured SQLite source; iOS uses its SDK headers.

Renovate's SQLite PRs automatically regenerate these files and rerun CI. For manual updates, change the pinned ref and run `mise run sqlite:update`. The updater verifies the amalgamation against the release's published SHA3-256 checksum before writing files. `mise run sqlite:check` checks the header version without downloading.
