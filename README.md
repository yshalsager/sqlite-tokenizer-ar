# sqlite-tokenizer-ar

Arabic search for SQLite FTS5, shipped as a native tokenizer extension, official SQLite WASM bundle, and optional Lucene-style query helpers.

The core extension exposes an FTS5 tokenizer named `sqlite_tokenizer_ar`. It is designed to match the [Apache Lucene](https://lucene.apache.org/) Arabic analysis pipeline for tokenization, Arabic normalization, stopwords, digit folding, and light stemming, while running inside SQLite.

Reference assets are pinned to Lucene `9.9.0` for reproducibility. The ArabicAnalyzer pipeline was source-diffed against Lucene `10.4.0` on 2026-06-07 with no behavior changes found.

> [!CAUTION]
> **Disclaimer:** This project was developed with heavy use of AI assistance. Treat it as an experiment rather than a stable product.

## Repository Layout

- `tokenizer/`: loadable SQLite extension written in C.
- `query_compat/`: optional Python CLI/reference layer for Lucene-style query behavior; runtime analysis, parsing, planning, ranking, and highlighting primitives live in C UDFs.
- `playground/`: browser demo source using official SQLite WASM built with the tokenizer extension.
- `tests/fixtures/`: small public fixtures for smoke tests. Large/private corpus fixtures are intentionally excluded.
- `ingester/sql/001_canonical_schema.sql`: minimal schema fixture used by query compatibility tests.

## Install

Release archives provide the loadable extension for Linux x64/arm64, macOS arm64, and Windows x64:

| Platform | Release archive | Installed library |
| --- | --- | --- |
| Linux x64 | `sqlite-tokenizer-ar-linux-x64.tar.gz` | `sqlite_tokenizer_ar.so` |
| Linux arm64 | `sqlite-tokenizer-ar-linux-arm64.tar.gz` | `sqlite_tokenizer_ar.so` |
| macOS arm64 | `sqlite-tokenizer-ar-macos-arm64.tar.gz` | `sqlite_tokenizer_ar.so` |
| Windows x64 | `sqlite-tokenizer-ar-windows-x64.tar.gz` | `sqlite_tokenizer_ar.dll` |

Install the correct archive automatically with [Mise](https://mise.jdx.dev/):

```toml
[tools."github:yshalsager/sqlite-tokenizer-ar"]
version = "0.1.13"
asset_pattern = 'sqlite-tokenizer-ar-{{ os() }}-{{ arch() }}.tar.gz'
```

```bash
mise install
export SQLITE_TOKENIZER_AR_EXTENSION="$(mise where github:yshalsager/sqlite-tokenizer-ar)/sqlite_tokenizer_ar.so"
```

On Windows PowerShell:

```powershell
mise install
$env:SQLITE_TOKENIZER_AR_EXTENSION = "$(mise where github:yshalsager/sqlite-tokenizer-ar)\sqlite_tokenizer_ar.dll"
```

SQLite must have FTS5 and loadable-extension support enabled.

## Build From Source

```bash
mise run build
```

This produces:

```text
tokenizer/build/sqlite_tokenizer_ar.so
```

Load it in SQLite:

```sql
.load ./tokenizer/build/sqlite_tokenizer_ar
CREATE VIRTUAL TABLE docs USING fts5(body, tokenize='sqlite_tokenizer_ar');
INSERT INTO docs(body) VALUES('قُرْآن كريم'),('هذه كتابها مفيد');
SELECT rowid, body FROM docs WHERE docs MATCH 'قران';
```

## Tokenizer Features

- Standard-style UTF-8 token segmentation for Arabic, Latin, and digits.
- ASCII lowercase for mixed Arabic/Latin text.
- Arabic and Persian digit folding to ASCII digits.
- Lucene Arabic stopword filtering.
- Arabic normalization: diacritics and tatweel stripping, alef/hamza normalization, dotless yeh to yeh, and teh marbuta to heh.
- Lucene-style Arabic light stemming.
- Tokenizer options for custom stopwords, stopword disabling, and stem exclusions.
- Optional Unicode Arabic honorific expansion, including `ﷺ`, `ﷻ`, `﵀`, and related ligatures.
- Analyzer-aware source spans and highlighting, including full stemmed words and one-glyph honorific sources.
- Helper UDFs for analysis, normalization, strict-form checks, wildcard/fuzzy matching, query planning, ranking, and snippets.

See the [tokenizer reference](tokenizer/README.md) for tokenizer arguments, UDF signatures, static registration, and integration examples.

## Query Compatibility Layer

SQLite FTS5 provides indexing, `MATCH`, native prefix queries, BM25, `highlight()`, and `snippet()`. The compatibility layer adds behavior FTS5 does not provide by itself:

- Boolean query parsing.
- Prefix, suffix, wildcard, and fuzzy expansion.
- Strict versus relaxed Arabic-form matching.
- Field routing for page/title style schemas.
- Lucene-style scoring helpers and deterministic result ordering.
- Snippet/highlight helper paths.

The schema-independent `sqlite_tokenizer_ar_plan_query_json()` C UDF returns `MATCH` templates plus unresolved corpus-dependent expansion descriptors. `sqlite_tokenizer_ar_execute_query_json()` is only for the repository's canonical query-compat schema. Keep application filtering, pagination, and custom result SQL outside it.

Use the compatibility layer when you need Lucene-style query syntax. Use the tokenizer directly when Arabic analysis plus native SQLite FTS behavior is enough. See the [query compatibility reference](query_compat/README.md).

## Test

```bash
mise run test
```

The public test lane uses small synthetic fixtures. Full corpus parity runs are kept outside this repository because they depend on large third-party/private datasets.

## WASM Playground

[Public demo](https://yshalsager.github.io/sqlite-tokenizer-ar/playground/)

The playground uses official SQLite WASM built with this extension as an extra init module:

```bash
SQLITE_SRC_DIR=/path/to/sqlite-source-tree mise run playground:build-wasm
mise x -- python -m http.server 8080
```

Open `http://localhost:8080/playground/`.

GitHub Pages publishes the playground from `main` using the latest release WASM assets plus the public fixture JSONL files.

## WASM npm Package

Release builds also pack the generated WASM bundle as:

```text
@yshalsager/sqlite-tokenizer-ar-wasm
```

The current package is built from SQLite `version-3.53.2`. The pinned SQLite ref lives in `mise.toml` as `SQLITE_TOKENIZER_AR_SQLITE_REF`.

It exports stable asset URLs:

```js
import {sqliteWasmUrls} from '@yshalsager/sqlite-tokenizer-ar-wasm'
```

For Node/Vitest:

```js
import sqlite3InitModule from '@yshalsager/sqlite-tokenizer-ar-wasm/node'
```

Install the public package tarball directly from the release without npm authentication:

```bash
npm install https://github.com/yshalsager/sqlite-tokenizer-ar/releases/download/v0.1.13/yshalsager-sqlite-tokenizer-ar-wasm-0.1.13.tgz
```

For apps that need fixed public paths, copy package `dist/*` to `public/sqlite-wasm/`.

## Android Native Artifacts

Release builds also attach `sqlite-tokenizer-ar-android.zip`, built with Android NDK `29.0.14206865` for:

- `arm64-v8a/libsqlite_tokenizer_ar.so`
- `armeabi-v7a/libsqlite_tokenizer_ar.so`
- `x86_64/libsqlite_tokenizer_ar.so`

The Android API level and ABI list are pinned in `mise.toml` as `SQLITE_TOKENIZER_AR_ANDROID_API` and `SQLITE_TOKENIZER_AR_ANDROID_ABIS`.

These are loadable SQLite extensions. The Android app still needs a SQLite runtime with FTS5 and extension loading enabled, or a custom SQLite build that registers the tokenizer directly.

## iOS Native Artifact

Release builds attach a static XCFramework:

```text
sqlite-tokenizer-ar-ios.xcframework.zip
└── SQLiteTokenizerAr.xcframework
```

It includes `ios-arm64` and `ios-arm64_x86_64-simulator` slices. Link it with Apple system `libsqlite3`, then register the tokenizer once per SQLite connection before creating/querying FTS tables:

```c
sqlite_tokenizer_ar_register(db);
```

Minimal local podspec shape:

```ruby
s.vendored_frameworks = 'SQLiteTokenizerAr.xcframework'
s.libraries = 'sqlite3'
s.ios.deployment_target = '15.0'
```

## Current Scope

The tokenizer is the stable core product. The query compatibility layer is useful but broader: it includes planner, scorer, parser, and snippet helpers that are intentionally outside tokenizer responsibilities.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for attribution and licensing notes.
