#!/usr/bin/env bash
set -euo pipefail

expected="${1:?Usage: check_macos_deployment_target.sh VERSION [LIBRARY]}"
library="${2:-tokenizer/build/sqlite_tokenizer_ar.so}"
actual="$(xcrun vtool -show-build "${library}" | awk '$1 == "minos" { print $2 }')"

if [[ "${actual}" != "${expected}" ]]; then
  echo "error: ${library} targets macOS ${actual:-unknown}; expected ${expected}" >&2
  exit 1
fi
echo "ok: ${library} targets macOS ${actual}"
