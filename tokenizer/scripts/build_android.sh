#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NDK_HOME="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-${ANDROID_NDK:-}}}"
ANDROID_API="${SQLITE_TOKENIZER_AR_ANDROID_API:-${ANDROID_API:-23}}"
ANDROID_ABIS="${SQLITE_TOKENIZER_AR_ANDROID_ABIS:-${ANDROID_ABIS:-arm64-v8a armeabi-v7a x86_64}}"
SQLITE_SRC_DIR="${SQLITE_SRC_DIR:-}"
SQLITE_INCLUDE_DIR="${SQLITE_INCLUDE_DIR:-}"
BUILD_DIR="${ROOT_DIR}/build/android"

if [[ -z "${NDK_HOME}" || ! -d "${NDK_HOME}" ]]; then
  echo "error: set ANDROID_NDK_HOME to an Android NDK directory" >&2
  exit 1
fi

TOOLCHAIN="${NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64"
if [[ ! -d "${TOOLCHAIN}" ]]; then
  case "$(uname -s)" in
    Darwin) TOOLCHAIN="${NDK_HOME}/toolchains/llvm/prebuilt/darwin-x86_64" ;;
  esac
fi
if [[ ! -d "${TOOLCHAIN}" ]]; then
  echo "error: NDK llvm toolchain not found under ${NDK_HOME}" >&2
  exit 1
fi

if [[ -n "${SQLITE_SRC_DIR}" ]]; then
  if [[ ! -f "${SQLITE_SRC_DIR}/sqlite3.h" ]]; then
    (cd "${SQLITE_SRC_DIR}" && { [[ -f Makefile ]] || ./configure; } && make -j2 sqlite3.c)
  fi
  SQLITE_INCLUDE_DIR="${SQLITE_SRC_DIR} ${SQLITE_SRC_DIR}/src ${SQLITE_INCLUDE_DIR}"
fi

include_flags=()
for dir in ${SQLITE_INCLUDE_DIR}; do
  include_flags+=("-I${dir}")
done
if [[ "${#include_flags[@]}" -eq 0 ]]; then
  echo "error: set SQLITE_SRC_DIR or SQLITE_INCLUDE_DIR for sqlite3.h/sqlite3ext.h" >&2
  exit 1
fi

mkdir -p "${BUILD_DIR}"
readelf_bin="${TOOLCHAIN}/bin/llvm-readelf"

for abi in ${ANDROID_ABIS}; do
  case "${abi}" in
    arm64-v8a) target="aarch64-linux-android"; machine="AArch64" ;;
    armeabi-v7a) target="armv7a-linux-androideabi"; machine="ARM" ;;
    x86_64) target="x86_64-linux-android"; machine="X86-64" ;;
    *) echo "error: unsupported Android ABI: ${abi}" >&2; exit 1 ;;
  esac

  cc="${TOOLCHAIN}/bin/${target}${ANDROID_API}-clang"
  out_dir="${BUILD_DIR}/${abi}"
  out="${out_dir}/libsqlite_tokenizer_ar.so"
  mkdir -p "${out_dir}"

  "${cc}" -O2 -fPIC -shared -Wall -Wextra \
    "${include_flags[@]}" \
    "${ROOT_DIR}/src/sqlite_tokenizer_ar.c" \
    -lm -o "${out}"

  header="$(${readelf_bin} -h "${out}")"
  symbols="$(${readelf_bin} -Ws "${out}")"
  grep -q "Machine:.*${machine}" <<<"${header}"
  grep -q 'sqlite3_sqlitetokenizerar_init' <<<"${symbols}"
  "${readelf_bin}" -d "${out}" >/dev/null
  echo "built ${out}"
done
