#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/ios"
MIN_IOS_VERSION="${SQLITE_TOKENIZER_AR_IOS_DEPLOYMENT_TARGET:-15.0}"
FRAMEWORK_NAME="SQLiteTokenizerAr"
XCFRAMEWORK_DIR="${BUILD_DIR}/${FRAMEWORK_NAME}.xcframework"
ZIP_PATH="${BUILD_DIR}/sqlite-tokenizer-ar-ios.xcframework.zip"
SRC="${ROOT_DIR}/src/sqlite_tokenizer_ar.c"
HEADER="${ROOT_DIR}/src/SQLiteTokenizerAr.h"

if [[ -z "${DEVELOPER_DIR:-}" && -d /Applications/Xcode.app/Contents/Developer ]]; then
  export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
fi

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}/include" "${BUILD_DIR}/iphoneos" "${BUILD_DIR}/iphonesimulator"
cp "${HEADER}" "${BUILD_DIR}/include/"
cat > "${BUILD_DIR}/include/module.modulemap" <<'MODULEMAP'
module SQLiteTokenizerAr {
  header "SQLiteTokenizerAr.h"
  export *
  link "sqlite3"
}
MODULEMAP

build_object() {
  local sdk="$1"
  local arch="$2"
  local target="$3"
  local out="$4"
  local sdk_path cc
  sdk_path="$(xcrun --sdk "${sdk}" --show-sdk-path)"
  cc="$(xcrun --sdk "${sdk}" --find clang)"
  "${cc}" -O2 -fPIC -Wall -Wextra \
    -isysroot "${sdk_path}" \
    -arch "${arch}" \
    -target "${target}" \
    -mios-version-min="${MIN_IOS_VERSION}" \
    -DSQLITE_CORE -DSQLITE_ENABLE_FTS5 \
    -I"${ROOT_DIR}/src" \
    -c "${SRC}" -o "${out}"
}

build_object iphoneos arm64 "arm64-apple-ios${MIN_IOS_VERSION}" "${BUILD_DIR}/iphoneos/sqlite_tokenizer_ar.o"
xcrun libtool -static -o "${BUILD_DIR}/iphoneos/lib${FRAMEWORK_NAME}.a" "${BUILD_DIR}/iphoneos/sqlite_tokenizer_ar.o"

build_object iphonesimulator arm64 "arm64-apple-ios${MIN_IOS_VERSION}-simulator" "${BUILD_DIR}/iphonesimulator/sqlite_tokenizer_ar_arm64.o"
build_object iphonesimulator x86_64 "x86_64-apple-ios${MIN_IOS_VERSION}-simulator" "${BUILD_DIR}/iphonesimulator/sqlite_tokenizer_ar_x86_64.o"
xcrun libtool -static -o "${BUILD_DIR}/iphonesimulator/lib${FRAMEWORK_NAME}.a" \
  "${BUILD_DIR}/iphonesimulator/sqlite_tokenizer_ar_arm64.o" \
  "${BUILD_DIR}/iphonesimulator/sqlite_tokenizer_ar_x86_64.o"

xcodebuild -create-xcframework \
  -library "${BUILD_DIR}/iphoneos/lib${FRAMEWORK_NAME}.a" -headers "${BUILD_DIR}/include" \
  -library "${BUILD_DIR}/iphonesimulator/lib${FRAMEWORK_NAME}.a" -headers "${BUILD_DIR}/include" \
  -output "${XCFRAMEWORK_DIR}"

nm -g "${BUILD_DIR}/iphoneos/lib${FRAMEWORK_NAME}.a" | grep -q 'sqlite3_sqlitetokenizerar_init'
nm -g "${BUILD_DIR}/iphoneos/lib${FRAMEWORK_NAME}.a" | grep -q 'sqlite3_extension_init'
nm -g "${BUILD_DIR}/iphoneos/lib${FRAMEWORK_NAME}.a" | grep -q 'sqlite_tokenizer_ar_register'

(cd "${BUILD_DIR}" && /usr/bin/zip -qry "${ZIP_PATH}" "${FRAMEWORK_NAME}.xcframework")
echo "built ${ZIP_PATH}"
