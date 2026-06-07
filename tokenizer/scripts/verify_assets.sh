#!/usr/bin/env sh
set -eu

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
tokenizer_dir="$(CDPATH= cd -- "${script_dir}/.." && pwd)"
assets_dir="${tokenizer_dir}/assets/lucene_9_9_0"
checksums_file="${assets_dir}/SHA256SUMS"

if [ ! -f "$checksums_file" ]; then
  echo "error: missing checksum file: $checksums_file" >&2
  exit 1
fi

if command -v sha256sum >/dev/null 2>&1; then
  (cd "$assets_dir" && sha256sum -c SHA256SUMS)
elif command -v shasum >/dev/null 2>&1; then
  while read -r expected file; do
    actual="$(shasum -a 256 "${assets_dir}/${file}" | awk '{print $1}')"
    if [ "$expected" != "$actual" ]; then
      echo "error: checksum mismatch for ${file}" >&2
      echo "expected=${expected}" >&2
      echo "actual=${actual}" >&2
      exit 1
    fi
    echo "${file}: OK"
  done < "$checksums_file"
else
  echo 'error: need sha256sum or shasum to verify assets' >&2
  exit 1
fi
