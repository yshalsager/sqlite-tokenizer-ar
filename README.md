# sqlite-tokenizer-ar

Native SQLite FTS5 tokenizer and compatibility helpers for Arabic search.

The core extension exposes an FTS5 tokenizer named `sqlite_tokenizer_ar`. It is designed to match the Lucene Arabic analysis pipeline for tokenization, Arabic normalization, stopwords, digit folding, and light stemming, while running inside SQLite.

Reference assets are pinned to Lucene `9.9.0` for reproducibility. The ArabicAnalyzer pipeline was source-diffed against Lucene `10.4.0` on 2026-06-07 with no behavior changes found.

> [!CAUTION]
> **Disclaimer:** This project was developed with heavy use of AI assistance. It's more like an AI experiment that a stable product. 

## Repository Layout

- `tokenizer/`: loadable SQLite extension written in C.
- `query_compat/`: optional Python compatibility layer that compiles Lucene-style query behavior to SQLite FTS plans and delegates shared analysis/parser helpers to C UDFs.
- `playground/`: browser demo source using official SQLite WASM built with the tokenizer extension.
- `tests/fixtures/`: small public fixtures for smoke tests. Large/private corpus fixtures are intentionally excluded.
- `ingester/sql/001_canonical_schema.sql`: minimal schema fixture used by query compatibility tests.

## Build

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
- Helper UDFs for analysis, normalization, stemming, sensitive-form checks, wildcard matching, Levenshtein distance, and query helper parsing.

## Query Compatibility Layer

`query_compat/` is separate from the tokenizer. It implements query planning and execution behavior that SQLite FTS5 does not provide by itself:

- Boolean query parsing.
- Prefix, suffix, wildcard, and fuzzy expansion.
- Strict versus relaxed Arabic-form matching.
- Field routing for page/title style schemas.
- Lucene-style scoring helpers and deterministic result ordering.
- Snippet/highlight helper paths.

Use it when you need Lucene-style search semantics. Use the tokenizer directly when you only need Arabic FTS tokenization inside SQLite.

## Test

```bash
mise run test
```

The public test lane uses small synthetic fixtures. Full corpus parity runs are kept outside this repository because they depend on large third-party/private datasets.

## WASM Playground

Public demo:

- `https://yshalsager.github.io/sqlite-tokenizer-ar/playground/`

The playground uses official SQLite WASM built with this extension as an extra init module:

```bash
SQLITE_SRC_DIR=/path/to/sqlite-source-tree mise run playground:build-wasm
python3 -m http.server 8080
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

Install from GitHub Packages with an authenticated npm client:

```bash
npm install @yshalsager/sqlite-tokenizer-ar-wasm --registry=https://npm.pkg.github.com
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
