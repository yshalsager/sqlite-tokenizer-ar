# SQLite headers

Unmodified public-domain headers from SQLite **3.53.2**, pinned independently of the runtime/WASM ref in `mise.toml`.

- Source: https://sqlite.org/2026/sqlite-amalgamation-3530200.zip
- Archive SHA-256: `8a310d0a16c7a90cacd4c884e70faa51c902afed2a89f63aaa0126ab83558a32`
- `sqlite3.h` SHA-256: `9e69a1353a4288450b0d5239ede11fc7f1f4c8e5eb07491fc8317eacb5b7de7e`
- `sqlite3ext.h` SHA-256: `ac9645e5c9ff0cf176efdd6e75cb5e98f46295d38e02db5c4d208826a39ab4be`

Desktop extension and Python wheel builds use these headers, not system SQLite headers. No SQLite runtime is bundled or linked into the extension. Android/WASM builds retain headers from their configured SQLite source; iOS uses its SDK headers.

Runtime updates do not require matching header updates. Refresh these headers only when a newer API or header fix is needed: extract both files from the official amalgamation archive and update this file's version, source URL, and hashes together. Run the public tests, runtime compatibility matrix, and native wheel builds before merging.
