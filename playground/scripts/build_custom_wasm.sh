#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PLAYGROUND_DIR="$ROOT_DIR/playground"
TOKENIZER_DIR="$ROOT_DIR/tokenizer"
SQLITE_SRC_DIR="${1:-${SQLITE_SRC_DIR:-}}"

if [[ -z "$SQLITE_SRC_DIR" ]]; then
  echo "usage: $0 /path/to/sqlite-source-tree"
  echo "hint: pass SQLITE_SRC_DIR=/path/to/sqlite-source-tree"
  exit 1
fi

WASM_DIR="$SQLITE_SRC_DIR/ext/wasm"
if [[ ! -f "$WASM_DIR/GNUmakefile" ]]; then
  echo "error: expected SQLite wasm dir at $WASM_DIR"
  exit 1
fi

echo "[1/5] syncing tokenizer sources into SQLite wasm build dir"
rm -rf \
  "$WASM_DIR"/sqlite_tokenizer_ar.c \
  "$WASM_DIR"/sqlite_tokenizer_ar_*.inc \
  "$WASM_DIR"/sqlite_tokenizer_ar_*.h \
  "$WASM_DIR"/arabic_stopwords_99.h \
  "$WASM_DIR"/init \
  "$WASM_DIR"/query_udf \
  "$WASM_DIR"/token_utils \
  "$WASM_DIR"/tokenizer_runtime \
  "$WASM_DIR"/udf_core
(cd "$TOKENIZER_DIR/src" && tar cf - .) | (cd "$WASM_DIR" && tar xf -)

cat > "$WASM_DIR/sqlite3_wasm_extra_init.c" <<'C_EOF'
#include "sqlite3.h"
#include "sqlite3ext.h"

#include "sqlite_tokenizer_ar.c"

int sqlite3_wasm_extra_init(const char *unused) {
  (void)unused;
  return sqlite3_auto_extension((void (*)(void))sqlite3_sqlitetokenizerar_init);
}
C_EOF

echo "[2/5] prepared sqlite3_wasm_extra_init.c with sqlite_tokenizer_ar integration"

if [[ ! -f "$WASM_DIR/config.make" ]]; then
  echo "[3/6] running SQLite top-level configure to generate ext/wasm/config.make"
  CONFIGURE_ARGS=()
  if [[ -n "${EMSDK:-}" ]]; then
    CONFIGURE_ARGS+=("--with-emsdk=$EMSDK")
  fi
  (cd "$SQLITE_SRC_DIR" && ./configure "${CONFIGURE_ARGS[@]}")
else
  echo "[3/6] ext/wasm/config.make already exists"
fi

echo "[4/6] building official sqlite3 WASM bundle with custom extra init"
echo "[4a/6] ensuring sqlite amalgamation is generated (sqlite3.c/sqlite3.h)"
(cd "$SQLITE_SRC_DIR" && make -j2 sqlite3.c)
(cd "$WASM_DIR" && make -j2)

OUTPUT_DIR="$PLAYGROUND_DIR/sqlite-wasm-custom"
mkdir -p "$OUTPUT_DIR"
if [[ ! -f "$WASM_DIR/jswasm/sqlite3.wasm" || ! -f "$WASM_DIR/jswasm/sqlite3.mjs" ]]; then
  echo "error: build finished but jswasm/sqlite3.wasm or jswasm/sqlite3.mjs was not found"
  exit 1
fi

cp "$WASM_DIR/jswasm/sqlite3.wasm" "$OUTPUT_DIR/sqlite3.wasm"
cp "$WASM_DIR/jswasm/sqlite3.mjs" "$OUTPUT_DIR/sqlite3.mjs"
cp "$WASM_DIR/jswasm/sqlite3-worker1.js" "$OUTPUT_DIR/sqlite3-worker1.js"
cp "$WASM_DIR/jswasm/sqlite3-worker1.mjs" "$OUTPUT_DIR/sqlite3-worker1.mjs"
cp "$WASM_DIR/jswasm/sqlite3-opfs-async-proxy.js" "$OUTPUT_DIR/sqlite3-opfs-async-proxy.js"
(cd "$OUTPUT_DIR" && shasum -a 256 sqlite3.wasm sqlite3.mjs sqlite3-worker1.js sqlite3-worker1.mjs sqlite3-opfs-async-proxy.js > SHA256SUMS)

echo "[5/6] wrote $OUTPUT_DIR/sqlite3.wasm"
echo "[6/6] wrote matched loader files: sqlite3.mjs, sqlite3-worker1.js, sqlite3-worker1.mjs, sqlite3-opfs-async-proxy.js"
echo "      wrote integrity manifest: $OUTPUT_DIR/SHA256SUMS"
echo "done: playground now uses the local matched sqlite3.mjs + sqlite3.wasm pair with native sqlite_tokenizer_ar"
