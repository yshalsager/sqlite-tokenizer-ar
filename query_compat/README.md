# Query Compatibility Module

Lucene query parser, AST, and planner that compiles to SQLite FTS5 execution plans.

## Entrypoints

- `make build`: validate query-compat CLI scaffold availability
- `make test`: run parser roundtrip + query-compat smoke/regression tests
- `make test` also enforces runtime-wrapper hygiene:
  - `scripts/test_runtime_reference_wrapper_routing.py`
  - `scripts/test_runtime_reference_cleanup_guard.py`
- `make backend_parity_gate BACKEND_PARITY_DB=/path/to/db.sqlite3`: compare `python` vs `c` backends on query fixtures
- `make ast_baseline_refresh`: regenerate AST baseline fixture (`tests/fixtures/queries/ast.baseline.core.jsonl`)
- `make ast_backend_parity_gate AST_PARITY_DB=/path/to/db.sqlite3`: compare AST output across `python` and `c` parser backends
- `make compile_baseline_refresh`: regenerate compile baseline fixture (`tests/fixtures/queries/compile.baseline.core.jsonl`)
- `make compile_backend_parity_gate COMPILE_PARITY_DB=/path/to/db.sqlite3`: compare compile outcomes (`compiled`/`error`) across `python` and `c` backends
- `make expansion_strict_baseline_refresh`: regenerate expansion/strict helper baseline (`tests/fixtures/queries/expansion.strict.baseline.jsonl`)
- `make ranking_baseline_refresh`: regenerate ranking baseline fixture (`tests/fixtures/queries/ranking.baseline.core.jsonl`)
- `make ranking_backend_parity_gate RANKING_PARITY_DB=/path/to/db.sqlite3`: compare ranking outputs (compiled + ordered hits + score tolerance) across `python` and `c` backends
- Planner APIs (from `scripts/sqlite_query_compat.py`):
  - `parse_query_ast(query) -> QueryAst`
  - `build_execution_plan(conn, fts_table, runtime_field, query, options) -> SqliteExecutionPlan`
  - `compile_match_expression(...) -> str` (compat wrapper over `build_execution_plan`)

## Status Snapshot (2026-04-19)

- Query parity gates on `a remote Linux host` are clean:
  - `make test_query_parity`: `500/500`, `mismatch_rows=0`
  - `make test_query_full`: `990/990`, `mismatch_rows=0`
- Runtime module split:
  - `scripts/sqlite_query_compat.py` is now primarily thin wrapper APIs and backend dispatch.
  - `scripts/python_reference_helpers.py` holds reference implementations used by tests/migration guards.
- Wrapper hygiene guard status:
  - `runtime_reference_wrapper_routing checked=59`
  - `runtime_reference_cleanup_guard wrappers_checked=59`

## Current Scope

- CLI runner: `scripts/sqlite_query_compat.py`
- backend selection: `--backend python|c` (default `c`; keep `python` for transition/debug fallback)
- runtime/reference split:
  - runtime APIs in `sqlite_query_compat.py` are expected to remain thin wrappers for moved reference logic
  - reference helpers live in `python_reference_helpers.py` and are guarded by wrapper-routing + static AST checks
- Supports:
  - boolean operators (`AND`, `OR`, `NOT`) with Lucene-style implicit `OR` between adjacent operands
  - escaped parentheses (`\(`, `\)`) are parsed as literal token text; only unescaped parentheses form clause groups
  - escaped whitespace (`\ `) stays inside a single token (prevents implicit-OR token splitting)
  - explicit conjunction handling aligned to Lucene classic parser clause semantics (`QueryParserBase.addClause`) rather than strict boolean precedence
  - malformed boolean syntax guards (`AND term`, `term AND`, consecutive operators) raising compile-time errors
  - optional malformed-query compatibility mode (`--lenient-parse-errors`, or `SearchOptions.lenient_parse_errors=True`) that converts compile-time query errors into deterministic empty-hit `__nohit__` plans (used by conformance/perf runners for Lucene-style malformed-row parity)
  - `+/-/NOT` modifiers handled in the same clause engine as explicit boolean conjunctions (no separate planner branch)
  - required/prohibited clauses (`+term`, `-term`, `+ "phrase"`, `- "phrase"`) with Lucene-like matching semantics:
    - `+` clauses are required
    - `-` clauses are excluded
    - pure negative queries return no hits
  - phrase queries (`"..."`, escaped quotes `\"`, and `"..."~N` mapped to FTS5 `NEAR(...)` expressions) with escaped-quote phrases routed through the same phrase BM25 scorer as equivalent unescaped phrases
  - field-scoped terms (`page:...`, `title:...`); escaped field separator (`\:`) is treated as literal text
  - field-scoped phrases (`page:"..."`, `title:"..."`)
  - field-scoped grouped clauses (`page:(...)`, `title:(...)`) including required/prohibited prefixes
  - boost suffix parsing (`^N`) for terms/phrases with scalar score application on single-clause and simple two-term boolean (`A^x AND/OR B^y`) forms, plus term/phrase/group/wildcard/fuzzy clause boosts in grouped fallback queries and grouped boosts like `(A OR B)^N` / `title:(A OR B)^N` (including required/grouped placements). Escaped caret (`\^`) is treated as literal text, escaped-quote phrases (for example `title:"باب \"المصور\""^2`) are boost-parsed and scored correctly, and mixed scoped boosted groups (for example `title:(...)^2 OR page:(...)^3`) no longer misparse as a single malformed scoped group.
  - prefix terms (`term*`) enabled by default (policy-off via `--disable-prefix-search`)
  - suffix terms (`*term`) via FTS lexicon expansion (`fts5vocab`)
  - wildcard patterns (`*` / `?`) including infix and contains forms via lexicon expansion (`fts5vocab`); escaped wildcard chars (`\*`, `\?`) and escaped brackets (`\[`, `\]`) are parsed as literals
  - fuzzy terms (`term~` / `term~1` / `term~2`) via lexicon + bounded edit distance; escaped fuzzy suffix (`\~`) is treated as literal text
  - field routing (`--field page|title|both`)
  - deterministic ordering (`score`, then identity tie-break)
  - Lucene-style norm-quantized BM25 reranking (`SmallFloat intToByte4/byte4ToInt`) for:
    - single-term queries
    - simple boolean queries with two term operands (`A AND B`, `A OR B`)
    - quoted phrases (including `~N` slop clauses) that analyze to 0+ terms, with dedicated phrase-frequency scoring
    - mixed boolean phrase composition (`phrase AND/OR term`, `phrase AND/OR phrase`) including scoped clauses, slop, and boosts
  - raw term/phrase emission for plain queries (no pre-normalization rewrite), preserving Lucene analyzer order at query time (stop filter before normalization)
  - Lucene-style Arabic light stemming behavior through the tokenizer pipeline (query and index terms both analyzed via `sqlite_tokenizer_ar`)
  - query-time analyzer/helpers (`analyze`, normalization, strict-form sensitivity checks, wildcard match, Levenshtein distance, Lucene norm-cache helpers, AST leaf flattening for boost extraction, scoped-token/boost/fuzzy/top-level-group-boost/whole-scoped-group/rank-boost-preprocess/boosted-group-span helpers) now call C UDFs from the tokenizer extension so shared logic is single-sourced in `tokenizer/src/sqlite_tokenizer_ar.c`
  - execution-plan AST compilation now consumes tokenizer C AST output when available (`parse_query_ast_with_udf`), with Python parser fallback
  - explicit boolean clauses drop analyzer-empty stopword operands instead of forcing empty-match expressions
- Query-time normalization controls are exposed:
  - `--respect-diacritics`
  - `--respect-hamza-forms`
  - `--respect-letter-forms` (`ة/ه`, `ى/ي`)
  - `--respect-digit-forms`
  - `--wildcard-max-expansions`
  - `--disable-wildcard-search`
  - `--disable-fuzzy-search`
  - `--lenient-parse-errors`
  - Current behavior: strict-respect flags apply a raw-text post-filter over `page_content_store` / `title_content_store` to narrow folded matches to exact-form hits, including literals extracted from grouped field clauses and wildcard/fuzzy literals. Wildcard/fuzzy candidate expansion is normalized against index vocabulary forms first, then strict-form filtering is applied as a second stage.
  - Requirement: canonical schema must include `page_content_store` and `title_content_store` (ingester now populates both).

## Parser Validation

- `split_query(...)` tokenization now has a canonical renderer (`render_query_tokens(...)`) so parser output can be round-tripped deterministically.
- `scripts/test_query_parser_roundtrip.py` runs roundtrip checks across fixture query packs (`inputs*.jsonl`), enforces an explicit expected-invalid set for malformed fixture rows, checks representative AST shapes, and guards extra malformed regressions (for example invalid scoped grouped-boost syntax).

## Gaps

- Prefix/suffix/wildcard lexicon behavior is implemented, but large-tier expansion cost/limits are not yet benchmarked on full Fixture tiers.
- Runtime phrase scoring now runs source-text-aware verification via `page_content_store` / `title_content_store` + tokenizer position UDFs to preserve Lucene stopword position-gap semantics; remaining work is benchmarking/tuning this path on full Fixture tiers.
