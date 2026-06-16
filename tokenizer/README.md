# sqlite_tokenizer_ar (C Tokenizer)

Native SQLite FTS5 tokenizer extension that reproduces Lucene ArabicAnalyzer behavior for index/query term analysis.

## Scope

This module owns analyzer/token-stream parity only:

- tokenization and term generation
- Arabic normalization and light stemming
- stopword filtering
- analyzer helper UDFs used by query compatibility layers

This module does **not** implement query-language semantics (boolean parser, wildcard expansion policy, ranking, snippets).

## What Is Exposed

After loading the extension, SQLite gets:

- FTS5 tokenizer: `sqlite_tokenizer_ar`
- UDF: `sqlite_tokenizer_ar_analyze_json(text)`
- UDF: `sqlite_tokenizer_ar_analyze_positions_json(text)`
- UDF: `sqlite_tokenizer_ar_normalize(text, ignore_diacritics=1, ignore_hamza_forms=1, ignore_letter_forms=1, ignore_digit_forms=1, lowercase=0, keep_wildcards=0)`
- UDF: `sqlite_tokenizer_ar_stem(text)`
- UDF: `sqlite_tokenizer_ar_has_sensitive_forms(text, check_diacritics, check_hamza_forms, check_letter_forms, check_digit_forms)`
- UDF: `sqlite_tokenizer_ar_wildcard_match(text, pattern)`
- UDF: `sqlite_tokenizer_ar_levenshtein(left, right, max_edits)`
- UDF: `sqlite_tokenizer_ar_int_to_byte4(value)`
- UDF: `sqlite_tokenizer_ar_byte4_to_int(value)`
- UDF: `sqlite_tokenizer_ar_norm_inverse_cache_blob(k1, b, avgdl)`
- UDF: `sqlite_tokenizer_ar_f32_mul(left, right)`
- UDF: `sqlite_tokenizer_ar_f32_add(left, right)`
- UDF: `sqlite_tokenizer_ar_lucene_idf(doc_count, doc_freq)`
- UDF: `sqlite_tokenizer_ar_lucene_term_score(weight, tf, doc_len, k1, b, avgdl)`
- UDF: `sqlite_tokenizer_ar_parse_scoped_token_json(token)`
- UDF: `sqlite_tokenizer_ar_parse_boosted_token_clause_json(token, runtime_field)`
- UDF: `sqlite_tokenizer_ar_find_all_normalized_match_spans_json(text, term, limit=8)`
- UDF: `sqlite_tokenizer_ar_snap_snippet_window_json(text, start, end, scan=12)`
- UDF: `sqlite_tokenizer_ar_select_non_overlapping_spans_csv(spans_csv)`
- UDF: `sqlite_tokenizer_ar_render_snippet_with_highlights(text, start, end, spans_csv)`
- UDF: `sqlite_tokenizer_ar_strip_boost_json(token)`
- UDF: `sqlite_tokenizer_ar_parse_fuzzy_token_json(token)`
- UDF: `sqlite_tokenizer_ar_parse_phrase_query_json(query)`
- UDF: `sqlite_tokenizer_ar_parse_single_phrase_clause_json(query)`
- UDF: `sqlite_tokenizer_ar_parse_phrase_term_boolean_json(query)`
- UDF: `sqlite_tokenizer_ar_parse_simple_boolean_json(query)`
- UDF: `sqlite_tokenizer_ar_parse_top_level_group_boost_json(query)`
- UDF: `sqlite_tokenizer_ar_parse_whole_scoped_group_json(query)`
- UDF: `sqlite_tokenizer_ar_preprocess_rank_boost_json(query, runtime_field)`
- UDF: `sqlite_tokenizer_ar_extract_boosted_group_spans_json(query, runtime_field)`
- UDF: `sqlite_tokenizer_ar_extract_boosted_phrase_spans_json(query)`
- UDF (migration stub): `sqlite_tokenizer_ar_parse_query_ast_json(query)`
- UDF (migration helper): `sqlite_tokenizer_ar_iter_query_ast_leaves_json(query)`
- UDF (migration stub): `sqlite_tokenizer_ar_execute_query_json(query, field, limit, options_json)`

Extension entry points:

- `sqlite3_sqlitetokenizerar_init(...)`
- `sqlite3_extension_init(...)` (delegates to `sqlite3_sqlitetokenizerar_init`)

## Analysis Pipeline

The tokenizer follows Lucene ArabicAnalyzer order:

1. Standard-style token segmentation (UTF-8 aware, Arabic/Latin/digits)
2. lowercase for ASCII
3. Arabic/Persian digit folding to ASCII digits
4. stopword filtering (Lucene Arabic stoplist)
5. Arabic normalization (diacritics/tatweel removal, alef/yaa/teh-marbouta normalization)
6. Arabic light stemming

Pinned Lucene references are under `assets/lucene_9_9_0/`.
Those assets are pinned to Lucene `9.9.0` for reproducibility; Lucene `10.4.0` was source-diffed on 2026-06-07 and did not change the ArabicAnalyzer behavior surface used by this tokenizer.

## Source Layout

Core source is now split into focused modules under `tokenizer/src`:

- `sqlite_tokenizer_ar.c`: thin composition unit (shared typedefs/macros + module includes)
- `sqlite_tokenizer_ar_types.h`: shared internal constants/types/macros used by all modules
- `sqlite_tokenizer_ar_token_utils.inc`: aggregator for tokenizer text/CSV/buffer utility helpers
- `token_utils/csv_options.inc`: stem exclusion + custom stopword CSV parsing helpers
- `token_utils/utf8_transform.inc`: aggregator for UTF-8 transform helpers
- `token_utils/utf8/codec.inc`: UTF-8 decode/encode helpers
- `token_utils/utf8/char_classes.inc`: Arabic normalization and token-class helpers
- `token_utils/utf8/transformers.inc`: token transform and utility helpers
- `token_utils/stopword_checks.inc`: stopword lookup helpers (default + custom)
- `token_utils/analyze_buffer.inc`: analysis buffer collectors used by analyze UDFs
- `sqlite_tokenizer_ar_udf_core.inc`: aggregator for core analyzer/ranker helper UDF internals
- `udf_core/analyze_normalize.inc`: aggregator for analyze/normalize/sensitive-form helpers
- `udf_core/analyze/helpers.inc`: aggregator for analyze helper primitives
- `udf_core/analyze/helpers/json_builder.inc`: shared JSON builder helpers
- `udf_core/analyze/helpers/normalize_core.inc`: normalize/sensitive helper primitives
- `udf_core/analyze/udf_analyze.inc`: `analyze_json` + `analyze_positions_json` UDFs
- `udf_core/analyze/udf_analyze/*.inc`: analyze UDF entrypoint implementations
- `udf_core/analyze/udf_normalize_sensitive.inc`: `normalize` + `has_sensitive_forms` UDFs
- `udf_core/wildcard_levenshtein.inc`: wildcard + Levenshtein helpers
- `udf_core/wildcard/shared.inc`: wildcard and UTF-8/codepoint utility helpers
- `udf_core/wildcard/levenshtein_core.inc`: edit-distance core routine
- `udf_core/wildcard/udf_entrypoints.inc`: wildcard/Levenshtein SQLite UDF entrypoints
- `udf_core/lucene_smallfloat.inc`: Lucene SmallFloat and norm-cache helpers
- `sqlite_tokenizer_ar_query_udf.inc`: aggregator for query-compat helper parser UDFs
- `query_udf/ast_primitives.inc`: aggregator for AST primitive/span parsers
- `query_udf/ast_primitives/core.inc`: aggregator for core AST primitive helpers
- `query_udf/ast_primitives/span_parsers.inc`: aggregator for span parser helpers
- `query_udf/ast_primitives/{common,field_group_clause,ast_types,span_low_level,span_advanced}.inc`: low-level AST/token span parsing pieces
- `query_udf/ast_primitives/common/*.inc`: shared ASCII/text + JSON-clause helper primitives
- `query_udf/ast_primitives/field_group_clause/*.inc`: field-group clause parser helpers
- `query_udf/ast_primitives/span_low_level/*.inc`: low-level numeric/quoted/scope/escape span helpers
- `query_udf/ast_builder.inc`: aggregator for AST parse/serialize walkers
- `query_udf/udf_parse_query_ast.inc`: aggregator for `sqlite_tokenizer_ar_parse_query_ast_json` path handlers
- `query_udf/parse_query_ast/*.inc`: parse-query-AST helpers (field-group path, recursive path, token fallback, entrypoint)
- `query_udf/ast/parse.inc`: aggregator for simple AST parser pieces
- `query_udf/ast/parse_parts/operand_clause.inc`: aggregator for operand parser helpers
- `query_udf/ast/parse_parts/operand_clause/*.inc`: operand parser helpers (prefix/group/phrase/token/entrypoint)
- `query_udf/ast/parse_parts/top_level_boolean.inc`: top-level boolean composition
- `query_udf/ast/parse_parts/top_level_boolean/*.inc`: top-level boolean scan/parse helpers
- `query_udf/ast/serialize.inc`: aggregator for JSON/leaf serialization helpers
- `query_udf/ast/serialize_parts/*.inc`: parsed-clause and leaf serialization helpers
- `query_udf/udf_token_helpers.inc`: aggregator for token-level query helper UDFs
- `query_udf/token_helpers/*.inc`: scoped/boost/fuzzy/group parser helper UDFs
- `query_udf/token_helpers/parse_whole_scoped_group/*.inc`: whole-query scoped-group helper parsing + JSON emission
- `query_udf/udf_ranking_helpers.inc`: aggregator for ranking-preprocess helper UDFs
- `query_udf/ranking/*.inc`: rank-boost preprocess + boosted-group-span extraction UDFs
- `query_udf/ranking/preprocess_rank_boost/*.inc`: rank-boost preprocessing helpers
- `query_udf/ranking/boosted_group_spans/*.inc`: boosted-group span extraction helpers
- `query_udf/ranking/extract_boosted_phrase_spans.inc`: boosted-phrase span extraction helper UDF
- `query_udf/udf_leaves_exec.inc`: aggregator for AST-leaves and execute-stub UDFs
- `query_udf/leaves_exec/*.inc`: leaves iterator UDF + execute stub UDF
- `sqlite_tokenizer_ar_tokenizer_runtime.inc`: aggregator for tokenizer runtime/stemming internals
- `tokenizer_runtime/stemmer_udf.inc`: aggregator for Arabic light stemmer + stem UDF
- `tokenizer_runtime/stemmer/core.inc`: Arabic light stemming core helpers
- `tokenizer_runtime/stemmer/udf_entrypoint.inc`: `sqlite_tokenizer_ar_stem` UDF entrypoint
- `tokenizer_runtime/tokenizer_lifecycle.inc`: aggregator for tokenizer create/delete lifecycle
- `tokenizer_runtime/lifecycle/free_data.inc`: tokenizer-owned resource cleanup helper
- `tokenizer_runtime/lifecycle/create.inc`: tokenizer create/options parsing
- `tokenizer_runtime/lifecycle/delete.inc`: tokenizer delete hook
- `tokenizer_runtime/tokenizer_scan.inc`: aggregator for FTS5 tokenize scan internals
- `tokenizer_runtime/scan/helpers.inc`: scanner helper routines (skip/emit helpers)
- `tokenizer_runtime/scan/boundary.inc`: aggregator for token-boundary scanner helpers
- `tokenizer_runtime/scan/boundary/scan_token_end.inc`: token-boundary scanner (`scan_token_end(...)`)
- `tokenizer_runtime/scan/main.inc`: `arabic_tokenize(...)` main scan loop
- `sqlite_tokenizer_ar_init.inc`: extension init + registration plumbing
- `init/udf_list_core.inc`, `init/udf_list_query.inc`: declarative UDF registration lists

## Build

From `sqlite-tokenizer-ar/tokenizer`:

```bash
mise run tokenizer:build
```

Output:

- `build/sqlite_tokenizer_ar.so`

Notes:

- SQLite must support FTS5.
- On macOS, `mise run tokenizer:build` prefers Homebrew SQLite headers when available.
- The extension is built as a loadable module and should not link libsqlite3 directly.

## Android Build

Set `ANDROID_NDK_HOME` and point `SQLITE_SRC_DIR` at a SQLite source checkout, then run:

```bash
SQLITE_SRC_DIR=/path/to/sqlite-source ANDROID_NDK_HOME=/path/to/android-ndk mise run tokenizer:build-android
```

Outputs:

- `build/android/arm64-v8a/libsqlite_tokenizer_ar.so`
- `build/android/armeabi-v7a/libsqlite_tokenizer_ar.so`
- `build/android/x86_64/libsqlite_tokenizer_ar.so`

The default Android build uses NDK `29.0.14206865`, API `23`, and the ABI list pinned in the root `mise.toml`.

## iOS Build

Build the static XCFramework:

```bash
mise run tokenizer:build-ios
```

Output:

```text
build/ios/sqlite-tokenizer-ar-ios.xcframework.zip
└── SQLiteTokenizerAr.xcframework
```

For iOS apps, link the XCFramework and `libsqlite3`, then call this once after every `sqlite3_open*` and before creating/querying FTS tables:

```c
sqlite_tokenizer_ar_register(db);
```

The iOS artifact does not use `sqlite3_auto_extension`; Apple deprecates that API on iOS because process-global auto extensions are not supported.

Run the simulator smoke check with:

```bash
mise run tokenizer:smoke-ios-simulator
```

## Quick Usage (SQLite)

```sql
SELECT load_extension('/absolute/path/to/build/sqlite_tokenizer_ar.so');

CREATE VIRTUAL TABLE docs USING fts5(
  page,
  title,
  tokenize='sqlite_tokenizer_ar'
);

INSERT INTO docs(page, title) VALUES
  (
    'الحمد لله رب العالمين والصلاة والسلام على نبينا محمد وآله وصحبه أجمعين. وكان يعرض دعوته في ملتقيات الناس وأسواقهم.',
    'من وسائل الدعوة'
  ),
  (
    'إن الإيمان هو أعظم المطالب وأهمها. وقد جعل الله له أسبابا تجلبه وتقويه.',
    'المقدمة'
  ),
  (
    'فهذه أضواء على المذاهب الهدامة التي استشرى خطرها على الإنسانية وعظم ضررها على جميع المجتمعات.',
    'أضواء على المذاهب الهدامة'
  );

-- Arabic normalization and stemming:
SELECT rowid, title FROM docs WHERE docs MATCH 'الدعوة';
SELECT rowid, title FROM docs WHERE docs MATCH 'الايمان';

-- Fielded search in FTS5:
SELECT rowid, title FROM docs WHERE docs MATCH 'title : المقدمة';
SELECT rowid, title FROM docs WHERE docs MATCH 'title : مذاهب';
```

These snippets are adapted from fixture Fixture fixtures in:

- `tests/fixtures/queries/docs.complex.jsonl`
- `tests/fixtures/queries/docs.snippets.jsonl`

## Tokenizer Arguments

Supported optional arguments:

- `stem_exclusion`
- `stopwords`
- `disable_stopwords`

Accepted forms:

- `tokenize='sqlite_tokenizer_ar stem_exclusion كتابها stem_exclusion مصطلح'`

Behavior:

- Excluded terms skip stemming only.
- All other pipeline stages still apply.

Example:

```sql
CREATE VIRTUAL TABLE t_excl USING fts5(
  content,
  tokenize='sqlite_tokenizer_ar stem_exclusion كتابها'
);
INSERT INTO t_excl(content) VALUES ('كتابها جديد');

SELECT count(*) FROM t_excl WHERE t_excl MATCH 'كتابها'; -- 1
SELECT count(*) FROM t_excl WHERE t_excl MATCH 'كتاب';   -- 0
```

`stopwords` controls the stopword set used at stop-filter stage:

- If omitted, tokenizer uses the built-in Lucene Arabic stoplist.
- If provided, it replaces the built-in list with the provided CSV terms.
- `disable_stopwords` disables stopword filtering entirely.

Accepted forms:

- `tokenize='sqlite_tokenizer_ar stopwords في stopwords من stopwords على'`
- `tokenize='sqlite_tokenizer_ar disable_stopwords'`

## UDF Reference

### 1) `sqlite_tokenizer_ar_analyze_json(text)`

Returns analyzer output tokens as a JSON array.

```sql
SELECT sqlite_tokenizer_ar_analyze_json('الذين ملكت أيمانكم');
-- ["ملكت","ايمانكم"]
```

### 2) `sqlite_tokenizer_ar_analyze_positions_json(text)`

Returns analyzer output as JSON objects with Lucene-style logical positions.
Positions preserve stopword gaps, so query/runtime layers can reproduce phrase-slop behavior.

```sql
SELECT sqlite_tokenizer_ar_analyze_positions_json('المنصور في اللغة');
-- [{"term":"منصور","position":0},{"term":"لغ","position":2}]
```

### 3) `sqlite_tokenizer_ar_normalize(...)`

Signature:

```text
sqlite_tokenizer_ar_normalize(
  text,
  ignore_diacritics=1,
  ignore_hamza_forms=1,
  ignore_letter_forms=1,
  ignore_digit_forms=1,
  lowercase=0,
  keep_wildcards=0
)
```

Examples:

```sql
SELECT sqlite_tokenizer_ar_normalize('قُرْآن ١٢٣ مدرسة', 1, 1, 1, 1, 1, 0);
-- قران 123 مدرسه

SELECT sqlite_tokenizer_ar_normalize('*قُر?آن*', 1, 1, 1, 1, 0, 1);
-- *قر?ان*

SELECT sqlite_tokenizer_ar_normalize('بِسْمِ اللَّهِ الرَّحْمَنِ الرَّحِيمِ', 1, 1, 1, 1, 0, 0);
SELECT sqlite_tokenizer_ar_normalize('أُحِبُّ الإِيمَان ١٤٤٥', 1, 1, 1, 1, 1, 0);
```

### 4) `sqlite_tokenizer_ar_stem(text)`

Applies the Arabic light stemmer stage only (no stopword removal or normalization).

```sql
SELECT sqlite_tokenizer_ar_stem('ساهدهات');
-- ساهد

SELECT sqlite_tokenizer_ar_stem('English');
-- English
```

### 5) `sqlite_tokenizer_ar_has_sensitive_forms(...)`

Returns `1` if text contains forms that should be treated as strict-sensitive by selected checks.

```sql
SELECT sqlite_tokenizer_ar_has_sensitive_forms('قُرْآن ١٢٣ مدرسة', 1, 1, 1, 1);
-- 1

SELECT sqlite_tokenizer_ar_has_sensitive_forms('كتاب مفيد', 1, 1, 1, 1);
-- 0
```

### 6) `sqlite_tokenizer_ar_wildcard_match(text, pattern)`

Returns `1` when `pattern` matches a substring of `text` with `*`/`?` semantics, else `0`.

```sql
SELECT sqlite_tokenizer_ar_wildcard_match('هذا قران كريم', 'قر?ن');
-- 1

SELECT sqlite_tokenizer_ar_wildcard_match('في كتابها مفيد', 'كتاب*مفيد');
-- 1
```

### 7) `sqlite_tokenizer_ar_levenshtein(left, right, max_edits)`

Returns edit distance using UTF-8 codepoint semantics with Lucene-compat early-exit (`> max_edits` returns `max_edits + 1`).

```sql
SELECT sqlite_tokenizer_ar_levenshtein('منصور', 'مصور', 1);
-- 1

SELECT sqlite_tokenizer_ar_levenshtein('abc', 'abcdef', 1);
-- 2
```

### 8) `sqlite_tokenizer_ar_int_to_byte4(value)` / `sqlite_tokenizer_ar_byte4_to_int(value)`

Lucene SmallFloat helpers used by BM25 norm encoding/decoding.

```sql
SELECT sqlite_tokenizer_ar_int_to_byte4(1000);
SELECT sqlite_tokenizer_ar_byte4_to_int(123);
```

### 9) `sqlite_tokenizer_ar_norm_inverse_cache_blob(k1, b, avgdl)`

Returns a 1024-byte blob (`256 x float32`) for Lucene-style BM25 norm inverse cache.

```sql
SELECT length(sqlite_tokenizer_ar_norm_inverse_cache_blob(1.2, 0.75, 12.0));
-- 1024
```

### 10) `sqlite_tokenizer_ar_parse_scoped_token_json(token)`

Parses Lucene-style scoped tokens and returns JSON:
- `scope`: `null | "page" | "title"`
- `raw`: token payload after scope extraction (or original token when not scoped)

```sql
SELECT sqlite_tokenizer_ar_parse_scoped_token_json('page:المنصور');
-- {"scope":"page","raw":"المنصور"}
```

#### `sqlite_tokenizer_ar_parse_boosted_token_clause_json(token, runtime_field)`

Parses boosted token clauses for ranking helpers and returns JSON:
- `scope`: `null | "page" | "title"` (resolved token scope)
- `raw`: token text with trailing boost removed
- `boost`: parsed boost factor
- `has_wildcard`: whether unescaped `*`/`?` is present in `raw`
- `is_fuzzy`: whether `raw` has Lucene fuzzy suffix (`~`, `~1`, `~2`)

`runtime_field` can be `page`, `title`, or `both`. The helper returns SQL `NULL` when the token is out of scope or not a boosted-token clause.

```sql
SELECT sqlite_tokenizer_ar_parse_boosted_token_clause_json('title:المنصور^2', 'title');
-- {"scope":"title","raw":"المنصور","boost":2.0,"has_wildcard":false,"is_fuzzy":false}

SELECT sqlite_tokenizer_ar_parse_boosted_token_clause_json('title:المنصور^2', 'both');
-- {"scope":"title","raw":"المنصور","boost":2.0,"has_wildcard":false,"is_fuzzy":false}
```

### 11) `sqlite_tokenizer_ar_strip_boost_json(token)`

Parses trailing unescaped boost suffix (`^N` / `^N.M`) and returns JSON:
- `base`: token text without boost when valid
- `boost`: `null` or parsed numeric value

```sql
SELECT sqlite_tokenizer_ar_strip_boost_json('المنصور^2');
-- {"base":"المنصور","boost":2.0}
```

### 12) `sqlite_tokenizer_ar_parse_fuzzy_token_json(token)`

Parses trailing unescaped fuzzy suffix (`~`, `~1`, `~2`) and returns JSON:
- `base`: fuzzy base text
- `edits`: clamped edit distance (`0..2`, Lucene-style)

Returns SQL `NULL` when token does not encode a fuzzy suffix.

```sql
SELECT sqlite_tokenizer_ar_parse_fuzzy_token_json('المنصور~1');
-- {"base":"المنصور","edits":1}
```

### 13) `sqlite_tokenizer_ar_parse_top_level_group_boost_json(query)`

Parses a whole-query top-level group boost form:
- input: `( ... )^N`
- output JSON: `{ "inner": "...", "boost": N }`

Returns SQL `NULL` when query is not exactly a top-level group-boost shape.

```sql
SELECT sqlite_tokenizer_ar_parse_top_level_group_boost_json('(المنصور OR المصور)^2');
-- {"inner":"المنصور OR المصور","boost":2.0}
```

### 14) `sqlite_tokenizer_ar_parse_whole_scoped_group_json(query)`

Parses whole-query scoped-group forms:
- input: `title:(...)` / `page:(...)` with optional `^N` boost
- output JSON: `{ "scope": "title|page", "inner": "...", "boost": null|N }`

Returns SQL `NULL` when query is not exactly a whole scoped-group shape.

```sql
SELECT sqlite_tokenizer_ar_parse_whole_scoped_group_json('title:(المنصور OR المصور)^2');
-- {"scope":"title","inner":"المنصور OR المصور","boost":2.0}
```

### 15) `sqlite_tokenizer_ar_preprocess_rank_boost_json(query, runtime_field)`

Preprocesses ranking boost shape for common whole-query forms and returns JSON:
- `boost_factor`: numeric multiplier for ranking scores
- `ranking_query`: query with supported whole-query boosts stripped for ranking execution

Returns SQL `NULL` when shape is unsupported by this helper (query_compat falls back to Python logic).

```sql
SELECT sqlite_tokenizer_ar_preprocess_rank_boost_json('title:(المنصور OR المصور)^2', 'title');
-- {"boost_factor":2.0,"ranking_query":"المنصور OR المصور"}
```

### 16) `sqlite_tokenizer_ar_extract_boosted_group_spans_json(query, runtime_field)`

Extracts boosted subgroup spans (non-whole-query only) as JSON array:
- each entry: `{ "inner": "<group query>", "boost": <number> }`
- respects runtime field scope (`title`/`page`) for scoped group forms
- skips whole-query group boost shapes (handled separately by rank-preprocess helper)

```sql
SELECT sqlite_tokenizer_ar_extract_boosted_group_spans_json('باب AND ("هذا كتاب" OR فصل)^2', 'page');
-- [{"inner":"\"هذا كتاب\" OR فصل","boost":2.0}]
```

### 17) C backend migration stubs

The following UDF entrypoints exist so query-compat backend gates can detect capability state deterministically:

- `sqlite_tokenizer_ar_parse_query_ast_json(query)`
- `sqlite_tokenizer_ar_parse_scoped_token_json(token)`
- `sqlite_tokenizer_ar_parse_boosted_token_clause_json(token, runtime_field)`
- `sqlite_tokenizer_ar_strip_boost_json(token)`
- `sqlite_tokenizer_ar_parse_fuzzy_token_json(token)`
- `sqlite_tokenizer_ar_parse_top_level_group_boost_json(query)`
- `sqlite_tokenizer_ar_parse_whole_scoped_group_json(query)`
- `sqlite_tokenizer_ar_preprocess_rank_boost_json(query, runtime_field)`
- `sqlite_tokenizer_ar_extract_boosted_group_spans_json(query, runtime_field)`
- `sqlite_tokenizer_ar_iter_query_ast_leaves_json(query)`
- `sqlite_tokenizer_ar_execute_query_json(query, field, limit, options_json)`

Current behavior:

- `sqlite_tokenizer_ar_parse_query_ast_json` supports:
  - fixture-covered parser shapes in `tests/fixtures/queries/inputs.smoke|complex|snippets.jsonl`, including grouped/nested clauses used by the current query-compat gates.
- `sqlite_tokenizer_ar_iter_query_ast_leaves_json` returns flattened AST leaves with composed occur semantics (`MUST/SHOULD/MUST_NOT`) for ranking/planner helpers.
- `sqlite_tokenizer_ar_execute_query_json` currently returns explicit `"not implemented yet"` errors until planner/ranker/snippet runtime is fully ported to C.

## Integration Patterns

### Python (`sqlite3`)

```python
import sqlite3

conn = sqlite3.connect(':memory:')
conn.enable_load_extension(True)
conn.load_extension('/absolute/path/to/sqlite-tokenizer-ar/tokenizer/build/sqlite_tokenizer_ar.so')

conn.execute("""
CREATE VIRTUAL TABLE docs USING fts5(
  page,
  title,
  tokenize='sqlite_tokenizer_ar'
)
""")
conn.executemany(
    "INSERT INTO docs(page, title) VALUES (?, ?)",
    [
        ("الحمد لله رب العالمين وكان يعرض دعوته في ملتقيات الناس وأسواقهم", "من وسائل الدعوة"),
        ("إن الإيمان هو أعظم المطالب وأهمها", "المقدمة"),
        ("فهذه أضواء على المذاهب الهدامة", "أضواء على المذاهب الهدامة"),
    ],
)
rows = conn.execute("SELECT rowid, title FROM docs WHERE docs MATCH 'الايمان OR الدعوة'").fetchall()
print(rows)
```

### C/C++ app startup (auto-register)

If the extension code is linked into your binary, register each SQLite connection after opening it:

```c
#include "SQLiteTokenizerAr.h"
sqlite_tokenizer_ar_register(db);
```

### SQLite WASM build

For official SQLite WASM builds, use `ext/wasm/sqlite3_wasm_extra_init.c` to auto-register `sqlite3_sqlitetokenizerar_init`.
The playground script already does this:

```bash
./playground/scripts/build_custom_wasm.sh /path/to/sqlite-source-tree
```

## Validation

From `sqlite-tokenizer-ar/tokenizer`:

```bash
mise run tokenizer:verify-assets
mise run tokenizer:test
mise run tokenizer:fuzz
mise run tokenizer:random-parity
```

`mise run tokenizer:test` runs:

- asset checksum verification
- smoke integration checks
- ported Lucene Arabic regression cases
- deterministic fuzz/stress run

`mise run tokenizer:random-parity` runs deterministic random-string analyzer parity:

- generates random mixed Arabic/Latin/digit/punctuation fixtures
- runs Lucene Java `ArabicAnalyzer` oracle
- runs SQLite `sqlite_tokenizer_ar`
- compares both analyzed term sequences and logical token positions
- fails on first parity mismatch (strict mode)
- non-strict exploratory mode: `python3 ./scripts/test_lucene_random_parity.py`
- optional position check explicitly: `python3 ./scripts/test_lucene_random_parity.py --check-positions`
- requires `javac`/`java` 11+ and Lucene jars via `LUCENE_CLASSPATH`, `.build-tools/lucene/*.jar`, or `/opt/lucene/*.jar`

## Troubleshooting

- `no such tokenizer: sqlite_tokenizer_ar`
  - extension not loaded, load failed, or SQLite lacks FTS5.
- `not authorized` on `load_extension`
  - enable extension loading in your host runtime (`enable_load_extension(True)` in Python).
- WASM `LinkError` / `_abort_js`
  - `sqlite3.mjs` and `sqlite3.wasm` are from different builds; use matched outputs from one build run.
- Query parity mismatch while tokenizer tests pass
  - likely in query layer (parser/planner/ranking), not tokenizer pipeline.
