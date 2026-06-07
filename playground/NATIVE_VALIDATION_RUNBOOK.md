# Native Browser Validation Runbook

Use this runbook to fill pending rows in [`WASM_VALIDATION_MATRIX.md`](./WASM_VALIDATION_MATRIX.md) for native desktop/mobile browsers.

## Scope

- Native desktop: Chrome, Firefox, Safari.
- Native mobile: Safari iOS, Chrome Android.
- Goal: confirm the same custom WASM bundle and tokenizer/UDF preflight behavior outside headless Linux runs.

## Preflight

1. Build and verify custom wasm bundle:

```bash
./playground/scripts/build_custom_wasm.sh /absolute/path/to/sqlite-source-tree
./playground/scripts/verify_custom_wasm.sh
```

2. Serve `sqlite-tokenizer-ar` directory:

```bash
python3 -m http.server 8080
```

3. Open `http://<host>:8080/playground/` from target browser/device.

## Required Validation Steps

Run these in order for each target browser/device.

1. Confirm startup and runtime metadata:
- `#status` reaches `ok: ...`.
- Runtime shows SQLite version + source-id.

2. Confirm tokenizer/UDF preflight:
- No startup errors about missing `sqlite_tokenizer_ar` or helper UDFs.
- `Tokenizer Probe` returns non-empty tokens.

3. Run strict scenario:
- Choose scenario `strict_hamza_letter`.
- Click `Apply Scenario`.
- Verify `Relaxed`, `Strict`, and `Cross` badges are `PASS`.

4. Run suffix expansion check:
- Set query to `*صور`.
- Click `Run Search`.
- Verify hits > 0.
- In explain output, confirm `relaxed.expansions` contains `kind: "suffix"` with non-empty `expansions`.

5. Record matrix fields:
- Date (UTC)
- OS
- Browser/version
- SQLite version
- SQLite source-id prefix
- ENABLE_FTS5
- Tokenizer/UDF preflight result
- Scenario pack
- Result + notes

## Suggested Evidence Capture

- Screenshot of full page after strict scenario pass.
- Copy-paste of `runtimeInfo` from explain output.
- Optional short screen recording for mobile runs.

## Per-Platform Notes

- Safari (desktop/iOS): serve from reachable host/IP, not localhost from device.
- Android Chrome: ensure phone and host are on same network; use host LAN IP.
- If browser caches stale wasm loader files, hard-reload and clear site data.

## Matrix Row Template

```text
| YYYY-MM-DD | <OS> | <Browser> | <sqlite_version> | `<source_prefix>` | yes/no | pass/fail | `strict_hamza_letter + suffix *صور` | pass/fail/blocked | <notes> |
```

Or append directly via make:

```bash
node ./playground/scripts/append_manual_matrix_row.cjs \
  MATRIX_OS="<OS>" \
  MATRIX_BROWSER="<Browser>" \
  MATRIX_SQLITE_VERSION="<sqlite_version>" \
  MATRIX_SOURCE_PREFIX="<source_prefix>" \
  MATRIX_ENABLE_FTS5=yes \
  MATRIX_PREFLIGHT=pass \
  MATRIX_SCENARIO_PACK="strict_hamza_letter + suffix *صور" \
  MATRIX_RESULT=pass \
  MATRIX_NOTES="<notes>"
```
