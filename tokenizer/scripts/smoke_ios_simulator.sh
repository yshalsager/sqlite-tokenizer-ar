#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/ios"
MIN_IOS_VERSION="${SQLITE_TOKENIZER_AR_IOS_DEPLOYMENT_TARGET:-15.0}"
FRAMEWORK_NAME="SQLiteTokenizerAr"
SMOKE_DIR="${BUILD_DIR}/smoke"
SMOKE_C="${SMOKE_DIR}/ios_smoke.c"
SMOKE_BIN="${SMOKE_DIR}/ios_smoke"

run_with_timeout() {
  local seconds="$1"
  shift
  "$@" &
  local pid="$!"
  (
    sleep "${seconds}"
    kill "${pid}" >/dev/null 2>&1 || true
  ) &
  local watchdog="$!"
  wait "${pid}"
  local rc="$?"
  kill "${watchdog}" >/dev/null 2>&1 || true
  wait "${watchdog}" >/dev/null 2>&1 || true
  return "${rc}"
}

if [[ -z "${DEVELOPER_DIR:-}" && -d /Applications/Xcode.app/Contents/Developer ]]; then
  export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
fi

if [[ ! -f "${BUILD_DIR}/iphonesimulator/lib${FRAMEWORK_NAME}.a" ]]; then
  echo "error: run tokenizer/scripts/build_ios_xcframework.sh first" >&2
  exit 1
fi

mkdir -p "${SMOKE_DIR}"
cat > "${SMOKE_C}" <<'C'
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SQLiteTokenizerAr.h"

static void exec_ok(sqlite3 *db, const char *sql) {
  char *err = 0;
  if (sqlite3_exec(db, sql, 0, 0, &err) != SQLITE_OK) {
    fprintf(stderr, "%s\n", err ? err : "sqlite error");
    sqlite3_free(err);
    _Exit(1);
  }
}

int main(void) {
  sqlite3 *db = 0;
  sqlite3_stmt *stmt = 0;
  if (sqlite3_open(":memory:", &db) != SQLITE_OK) return 1;
  if (sqlite_tokenizer_ar_register(db) != SQLITE_OK) return 6;
  if (sqlite3_prepare_v2(db, "SELECT sqlite_tokenizer_ar_normalize('ٱلْعَرَبِيَّة');", -1, &stmt, 0) != SQLITE_OK) return 2;
  if (sqlite3_step(stmt) != SQLITE_ROW) return 3;
  sqlite3_finalize(stmt);
  exec_ok(db, "CREATE VIRTUAL TABLE tokenizer_probe USING fts5(text, tokenize='sqlite_tokenizer_ar')");
  exec_ok(db, "INSERT INTO tokenizer_probe(text) VALUES ('اللغة العربية كتاب مفيد')");
  if (sqlite3_prepare_v2(db, "SELECT rowid FROM tokenizer_probe WHERE tokenizer_probe MATCH 'العربية'", -1, &stmt, 0) != SQLITE_OK) return 4;
  if (sqlite3_step(stmt) != SQLITE_ROW || sqlite3_column_int(stmt, 0) != 1) return 5;
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  puts("ok: iOS simulator sqlite_tokenizer_ar smoke passed");
  return 0;
}
C

sdk_path="$(xcrun --sdk iphonesimulator --show-sdk-path)"
cc="$(xcrun --sdk iphonesimulator --find clang)"
"${cc}" -O2 \
  -isysroot "${sdk_path}" \
  -arch arm64 \
  -target "arm64-apple-ios${MIN_IOS_VERSION}-simulator" \
  -mios-version-min="${MIN_IOS_VERSION}" \
  -I"${BUILD_DIR}/include" \
  "${SMOKE_C}" "${BUILD_DIR}/iphonesimulator/lib${FRAMEWORK_NAME}.a" \
  -lsqlite3 -o "${SMOKE_BIN}"

devices_json="$(xcrun simctl list devices available -j)" || {
  echo "error: xcrun simctl cannot list available devices" >&2
  exit 1
}
udid="$(printf '%s' "${devices_json}" | python3 -c 'import json,sys; d=json.load(sys.stdin)["devices"]; print(next(v["udid"] for vs in d.values() for v in vs if v.get("isAvailable") and "iPhone" in v.get("name", "")))')" || {
  echo "error: no available iPhone simulator found" >&2
  exit 1
}
xcrun simctl boot "${udid}" >/dev/null 2>&1 || true
run_with_timeout 180 xcrun simctl bootstatus "${udid}" -b >/dev/null
run_with_timeout 60 xcrun simctl spawn "${udid}" "${SMOKE_BIN}"
