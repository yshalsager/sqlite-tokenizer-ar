# sqlite-tokenizer-ar

Native SQLite FTS5 tokenizer and compatibility helpers for Arabic search.

The core extension exposes an FTS5 tokenizer named `sqlite_tokenizer_ar`. It is designed to match the Lucene Arabic analysis pipeline for tokenization, Arabic normalization, stopwords, digit folding, and light stemming, while running inside SQLite.

Reference assets are pinned to Lucene `9.9.0` for reproducibility. The ArabicAnalyzer pipeline was source-diffed against Lucene `10.4.0` on 2026-06-07 with no behavior changes found.

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

The playground uses official SQLite WASM built with this extension as an extra init module:

```bash
SQLITE_SRC_DIR=/path/to/sqlite-source-tree mise run playground:build-wasm
python3 -m http.server 8080
```

Open `http://localhost:8080/playground/`.

## Current Scope

The tokenizer is the stable core product. The query compatibility layer is useful but broader: it includes planner, scorer, parser, and snippet helpers that are intentionally outside tokenizer responsibilities.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for attribution and licensing notes.
