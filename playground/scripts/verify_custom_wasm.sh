#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WASM_DIR="$ROOT_DIR/playground/sqlite-wasm-custom"

required_files=(
  "sqlite3.wasm"
  "sqlite3.mjs"
  "sqlite3-node.mjs"
  "sqlite3-worker1.js"
  "sqlite3-worker1.mjs"
  "sqlite3-opfs-async-proxy.js"
  "SHA256SUMS"
)

for file in "${required_files[@]}"; do
  if [[ ! -f "$WASM_DIR/$file" ]]; then
    echo "error: missing $WASM_DIR/$file"
    exit 1
  fi
done

(cd "$WASM_DIR" && shasum -a 256 -c SHA256SUMS)
echo "ok: verified custom sqlite wasm bundle integrity ($WASM_DIR)"
