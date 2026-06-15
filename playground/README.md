# SQLite WASM Playground

Browser playground to showcase Arabic query-compat progress on a small sample corpus using a required local custom SQLite WASM bundle (`sqlite3.mjs` + `sqlite3.wasm`) that includes native `sqlite_tokenizer_ar`.

Use [`WASM_VALIDATION_MATRIX.md`](./WASM_VALIDATION_MATRIX.md) to track validated browser/runtime combinations.
Use [`NATIVE_VALIDATION_RUNBOOK.md`](./NATIVE_VALIDATION_RUNBOOK.md) for native desktop/mobile validation steps (Safari/iOS/Android).

## What It Demonstrates

- Boolean query handling (`AND`, `OR`, `NOT`, `+`, `-`) with compile-time guards for malformed operators.
- Phrase and field-phrase clauses (`"..."`, `"..."~N`, `page:"..."`, `title:"..."`) including inline required/prohibited modifiers.
- Prefix/suffix/wildcard (`*`, `?`) expansion using the runtime lexicon table (`fts5vocab` or `fts4aux` fallback).
- Policy controls for disabling prefix/suffix/wildcard/fuzzy behavior, plus separate suffix and wildcard expansion caps.
- Fuzzy expansion (`term~`, `term~1`, `term~2`) with bounded edit distance.
- Clause boosts (`^N`) for term/phrase/field-group clauses.
- Strict-mode filters for Arabic forms:
  - diacritics
  - hamza/alef forms
  - letter forms (`ة/ه`, `ى/ي`)
  - digit forms
- Pre-added editable corpus text areas for page/title docs with one-click in-browser reindex.
- Corpus presets with public fixture snippets sourced from `tests/fixtures/queries/docs.smoke.jsonl`, `docs.complex.jsonl`, and `docs.snippets.jsonl`.
- Dynamic fixture presets generated in-browser from the included public fixture rows.
- Option presets (`lucene default`, `strict orthography`, `no wildcard/fuzzy`, `recall heavy`, `precision heavy`).
- Scenario presets for one-click reproducible demos (including real-corpus broad/strict/hadith scenarios plus the original mini-fixture scenarios).
- Native tokenizer args input (`Tokenizer Args`) for runtime `tokenize='sqlite_tokenizer_ar ...'` experiments (e.g. `stem_exclusion كتابها,مصطلح`).
- Tokenizer arg presets (`default`, `stem_exclusion_*`, `custom`) with one-click apply.
- Tokenizer probe panel showing token stream plus native UDF normalization/sensitive-form checks from the active tokenizer extension.
- Side-by-side search comparison (`Relaxed` vs `Strict`) with independent compiled-query and hit tables.
- Scenario assertions (`Relaxed`, `Strict`, `Cross`) with pass/fail badges.
- Explain panel showing runtime, tokenizer directive, clause-level counts, and expansion events.
  - runtime metadata includes SQLite version/source-id and FTS5 compile-option probe.
- Shareable URL state via `Copy Share Link` (captures query/options/corpus/scenario/tokenizer/probe toggles).
- Runtime mode status:
  - `custom sqlite3.wasm · native sqlite_tokenizer_ar (+UDF analyze/positions/normalize/stem)` (fixture extension demo)
  - compiled output includes `_runtime` metadata with tokenizer directive.

## Scope Note

This is a demo surface, not the authoritative parity harness. Source-of-truth parity remains the Python/C conformance and query-compat tests in this repository.

The app attempts `fts5` first and automatically falls back to `fts4` if needed. The status badge shows the active mode.

Corpus text format:

- One row per line.
- `book_id|item_id|text`

## Run Locally

### 1) Build custom official SQLite WASM with `sqlite_tokenizer_ar` (required)

The playground loads a local matched bundle from `playground/sqlite-wasm-custom/` and fails if files are missing or do not include `sqlite_tokenizer_ar`.
The SQLite source tree must already be set up for `ext/wasm` builds (`emcc`/Emscripten available).

```bash
SQLITE_SRC_DIR=/absolute/path/to/sqlite-source-tree mise run build-wasm
```

This script:

- copies `tokenizer/src/sqlite_tokenizer_ar.c` + stopwords header into `ext/wasm/`
- writes `ext/wasm/sqlite3_wasm_extra_init.c` that auto-registers `sqlite3_sqlitetokenizerar_init`
- builds SQLite WASM from official source
- outputs matched files in `playground/sqlite-wasm-custom/`:
  - `sqlite3.wasm`
  - `sqlite3.mjs`
  - `sqlite3-worker1.js`
  - `sqlite3-worker1.mjs`
  - `sqlite3-opfs-async-proxy.js`
  - `SHA256SUMS`

Validate bundle integrity after build:

```bash
./playground/scripts/verify_custom_wasm.sh
```

Tokenizer args usage in the UI:

- Leave empty for default `tokenize='sqlite_tokenizer_ar'`.
- Set text like `stem_exclusion كتابها,مصطلح`, then press Enter in the field (or change focus) to rebuild index with that directive.

Scenario preset usage in the UI:

- Pick a scenario and click `Apply Scenario`.
- This sets corpus preset, options, tokenizer args, query, and probe text together.
- Fixture corpus presets are built from `doc_id` slices across `docs.smoke.jsonl`, `docs.complex.jsonl`, and `docs.snippets.jsonl`.
- Scenario assertions are only evaluated when the active query still matches the scenario query; if you change the query manually they display `N/A`.

### 2) Serve playground

From the repository root:

```bash
python3 -m http.server 8080
```

Open:

- `http://localhost:8080/playground/`

## Automated Validation (Playwright)

Run a repeatable headless validation (preflight + strict scenario assertions + suffix expansion check):

```bash
node ./playground/scripts/validate_playground_playwright.cjs
```

From the repository root, you can also use:

```bash
mise run playground:validate
PLAYGROUND_BROWSER=firefox mise run playground:validate
mise run playground:validate-batch
PLAYGROUND_BATCH_APPEND=1 mise run playground:validate-batch
```

If Playwright is installed in a non-repo location, pass:

```bash
PLAYGROUND_NODE_PATH=/tmp/pw-runner/node_modules mise run playground:validate
```

Optional env overrides:

- `PLAYGROUND_URL` (default `http://127.0.0.1:8090/playground/`)
- `PLAYGROUND_BROWSER` (default `chromium`, options: `chromium`, `firefox`, `webkit`)
- `PLAYGROUND_SCENARIO` (default `strict_hamza_letter`)
- `PLAYGROUND_SUFFIX_QUERY` (default `*صور`)
- `PLAYGROUND_TIMEOUT_MS` (default `180000`)
- `PLAYGROUND_OUT_DIR` (default `test-results/playground`)

The script prints `PLAYGROUND_VALIDATION_JSON=...` and writes `validation.json` + `validation.png` in `PLAYGROUND_OUT_DIR`.

Batch mode runs multiple browsers and writes `summary.json` plus per-browser `validation.json` files:

```bash
mise run playground:validate-batch \
  PLAYGROUND_NODE_PATH=/tmp/pw-runner/node_modules \
  PLAYGROUND_BROWSERS=chromium,firefox,webkit \
  PLAYGROUND_BATCH_OUT_DIR=test-results/playground/batch \
  PLAYGROUND_BATCH_APPEND=1
```

Shortcut: `PLAYGROUND_BATCH_APPEND=1 mise run playground:validate-batch` appends results to the matrix.

Batch env knobs:

- `PLAYGROUND_BROWSERS` (default `chromium,firefox,webkit`)
- `PLAYGROUND_BATCH_OUT_DIR` (default `test-results/playground/batch`)
- `PLAYGROUND_BATCH_SUMMARY` (default `test-results/playground/batch/summary.json`)
- `PLAYGROUND_BATCH_APPEND` (`1` appends rows to matrix, default `0`)

Batch behavior:

- successful browsers emit `pass` rows from runtime metadata
- failed browsers emit structured `blocked` rows with captured error reason (for example missing host dependencies)

Render a ready Markdown table row from a validation JSON:

```bash
node ./playground/scripts/render_validation_matrix_row.cjs test-results/playground/validation.json
# or
node ./playground/scripts/render_validation_matrix_row.cjs test-results/playground/validation.json
```

Append that row into the matrix file (idempotent, skips duplicates):

```bash
node ./playground/scripts/append_validation_matrix_row.cjs \
  test-results/playground/validation.json \
  playground/WASM_VALIDATION_MATRIX.md
```

For native/manual runs (no Playwright JSON), append a row directly:

```bash
node ./playground/scripts/append_manual_matrix_row.cjs \
  MATRIX_OS="macOS 14" \
  MATRIX_BROWSER="Safari 17.4" \
  MATRIX_SQLITE_VERSION="3.54.0" \
  MATRIX_SOURCE_PREFIX="2026-04-14 aa3432af90b2" \
  MATRIX_ENABLE_FTS5=yes \
  MATRIX_PREFLIGHT=pass \
  MATRIX_SCENARIO_PACK="strict_hamza_letter + suffix *صور" \
  MATRIX_RESULT=pass \
  MATRIX_NOTES="Native Safari desktop validation passed"
```

## Troubleshooting

- Error `Aborted(both async and sync fetching of the wasm failed)` means `playground/sqlite-wasm-custom/sqlite3.wasm` is missing or unreachable from the browser.
- Error `WebAssembly.instantiate(): ... _abort_js ... requires a callable` usually means your `sqlite3.mjs` and `sqlite3.wasm` are from different SQLite builds.
- Error about missing `sqlite_tokenizer_ar` helper UDFs means the wasm bundle was built without the latest tokenizer registration code.
- Playwright `webkit` may require host libs (for example `libavif16`) on Linux runners; if launch fails, run only `chromium`/`firefox` until deps are installed.
- Rebuild custom wasm, then reload the page:

```bash
./playground/scripts/build_custom_wasm.sh /absolute/path/to/sqlite-source-tree
./playground/scripts/verify_custom_wasm.sh
python3 -m http.server 8080
```
