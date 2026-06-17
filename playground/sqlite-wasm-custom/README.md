# Custom SQLite WASM Bundle Output

The playground requires a matched local bundle in this directory:

- `sqlite-wasm-custom/sqlite3.wasm`
- `sqlite-wasm-custom/sqlite3.mjs`
- `sqlite-wasm-custom/sqlite3-node.mjs`
- `sqlite-wasm-custom/sqlite3-worker1.js`
- `sqlite-wasm-custom/sqlite3-worker1.mjs`
- `sqlite-wasm-custom/sqlite3-opfs-async-proxy.js`
- `sqlite-wasm-custom/SHA256SUMS`

Do not mix a wasm file from one SQLite build with an `sqlite3.mjs` file from another build, or initialization may fail with linker/import errors.

Build helper:

```bash
./playground/scripts/build_custom_wasm.sh /absolute/path/to/sqlite-source-tree
```

Verify helper:

```bash
./playground/scripts/verify_custom_wasm.sh
```
