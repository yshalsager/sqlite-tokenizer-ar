#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_DIR="${SQLITE_WASM_BUNDLE_DIR:-$ROOT_DIR/playground/sqlite-wasm-custom}"
DIST_DIR="$ROOT_DIR/dist"

required=(
  sqlite3.wasm
  sqlite3.mjs
  sqlite3-worker1.js
  sqlite3-worker1.mjs
  sqlite3-opfs-async-proxy.js
  SHA256SUMS
)

for file in "${required[@]}"; do
  if [[ ! -f "$SRC_DIR/$file" ]]; then
    echo "error: missing $SRC_DIR/$file; run mise run playground:build-wasm first" >&2
    exit 1
  fi
done

rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"
for file in "${required[@]}"; do
  cp "$SRC_DIR/$file" "$DIST_DIR/$file"
done

cat > "$DIST_DIR/index.js" <<'JS'
export const sqliteWasmUrls = {
  module: new URL('./sqlite3.mjs', import.meta.url).href,
  wasm: new URL('./sqlite3.wasm', import.meta.url).href,
  worker: new URL('./sqlite3-worker1.mjs', import.meta.url).href,
  workerClassic: new URL('./sqlite3-worker1.js', import.meta.url).href,
  opfsProxy: new URL('./sqlite3-opfs-async-proxy.js', import.meta.url).href,
  checksums: new URL('./SHA256SUMS', import.meta.url).href,
}
JS

cat > "$DIST_DIR/index.d.ts" <<'TS'
export declare const sqliteWasmUrls: {
  module: string
  wasm: string
  worker: string
  workerClassic: string
  opfsProxy: string
  checksums: string
}
TS
