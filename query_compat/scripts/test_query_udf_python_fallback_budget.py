#!/usr/bin/env python3
import inspect
import json
import sqlite3
import tempfile
from pathlib import Path

import python_reference_helpers as ref
import sqlite_query_compat as qc
from generate_compile_baseline import setup_fixture_db


FIXTURE_FILES = [
    'tests/fixtures/queries/inputs.smoke.jsonl',
    'tests/fixtures/queries/inputs.complex.jsonl',
    'tests/fixtures/queries/inputs.snippets.jsonl',
    'tests/fixtures/queries/inputs.parity.jsonl',
    'tests/fixtures/queries/inputs.full.jsonl',
    'tests/fixtures/queries/inputs.real.jsonl',
]


def load_queries(root: Path) -> list[tuple[str, str]]:
    rows: list[tuple[str, str]] = []
    for rel_path in FIXTURE_FILES:
        path = (root / rel_path).resolve()
        for line in path.read_text(encoding='utf-8').splitlines():
            if not line.strip():
                continue
            row = json.loads(line)
            query = str(row.get('query', '')).strip()
            if query == '':
                continue
            rows.append((query, str(row.get('field', '') or 'both')))
    return rows


def caller_name() -> str:
    frame = inspect.currentframe()
    if frame is None or frame.f_back is None:
        return ''
    caller = frame.f_back
    return str(caller.f_code.co_name)


def main() -> None:
    query_compat_dir = Path(__file__).resolve().parents[1]
    root = query_compat_dir.parent
    schema_path = (root / 'ingester' / 'sql' / '001_canonical_schema.sql').resolve()
    extension_path = (root / 'tokenizer' / 'build' / 'sqlite_tokenizer_ar.so').resolve()
    if not schema_path.exists():
        raise SystemExit(f'error: schema not found: {schema_path}')
    if not extension_path.exists():
        raise SystemExit(f'error: tokenizer extension not found: {extension_path}')

    queries = load_queries(root)
    if not queries:
        raise SystemExit('error: no fixture queries loaded for query-helper fallback budget test')

    fallback_counts = {
        'parse_top_level_group_boost_with_udf': 0,
        'parse_whole_scoped_group_with_udf': 0,
        'parse_boosted_token_clause_with_udf': 0,
        'parse_simple_boolean_with_udf': 0,
        'parse_simple_phrase_query_with_udf': 0,
        'parse_single_phrase_clause_with_udf': 0,
        'parse_phrase_term_boolean_with_udf': 0,
        'extract_boosted_phrase_spans_with_udf': 0,
        'extract_boosted_group_spans_with_udf': 0,
    }

    original_parse_top_level_group_boost = ref.parse_top_level_group_boost
    original_parse_whole_scoped_group = ref.parse_whole_scoped_group
    original_parse_boosted_token_clause_python = ref.parse_boosted_token_clause_python
    original_parse_simple_boolean_python = ref.parse_simple_boolean_python
    original_parse_simple_phrase_query_python = ref.parse_simple_phrase_query_python
    original_parse_phrase_term_boolean_python = ref.parse_phrase_term_boolean_python
    original_extract_boosted_phrase_spans_python = ref.extract_boosted_phrase_spans_python
    original_extract_boosted_group_spans_python = ref.extract_boosted_group_spans_python

    def parse_top_level_group_boost_probe(query: str) -> tuple[str, float] | None:
        if caller_name() == 'parse_top_level_group_boost_with_udf':
            fallback_counts['parse_top_level_group_boost_with_udf'] += 1
        return original_parse_top_level_group_boost(query)

    def parse_whole_scoped_group_probe(query: str) -> tuple[str, str, float | None] | None:
        if caller_name() == 'parse_whole_scoped_group_with_udf':
            fallback_counts['parse_whole_scoped_group_with_udf'] += 1
        return original_parse_whole_scoped_group(query)

    def parse_boosted_token_clause_python_probe(token: str, runtime_field: str) -> dict | None:
        if caller_name() == 'parse_boosted_token_clause_with_udf':
            fallback_counts['parse_boosted_token_clause_with_udf'] += 1
        return original_parse_boosted_token_clause_python(token, runtime_field)

    def parse_simple_boolean_python_probe(query: str, simple_query_re) -> tuple[str, str, float, str, float] | None:
        if caller_name() == 'parse_simple_boolean_with_udf':
            fallback_counts['parse_simple_boolean_with_udf'] += 1
        return original_parse_simple_boolean_python(query, simple_query_re)

    def parse_simple_phrase_query_python_probe(query: str) -> tuple[str, int] | None:
        caller = caller_name()
        if caller == 'parse_simple_phrase_query_with_udf':
            fallback_counts['parse_simple_phrase_query_with_udf'] += 1
        elif caller == 'parse_single_phrase_clause_with_udf':
            fallback_counts['parse_single_phrase_clause_with_udf'] += 1
        return original_parse_simple_phrase_query_python(query)

    def parse_phrase_term_boolean_python_probe(query: str) -> dict | None:
        if caller_name() == 'parse_phrase_term_boolean_with_udf':
            fallback_counts['parse_phrase_term_boolean_with_udf'] += 1
        return original_parse_phrase_term_boolean_python(query)

    def extract_boosted_phrase_spans_python_probe(query: str) -> list[tuple[str | None, str, int, float]]:
        if caller_name() == 'extract_boosted_phrase_spans_with_udf':
            fallback_counts['extract_boosted_phrase_spans_with_udf'] += 1
        return original_extract_boosted_phrase_spans_python(query)

    def extract_boosted_group_spans_python_probe(query: str, runtime_field: str) -> list[tuple[str, float]]:
        if caller_name() == 'extract_boosted_group_spans_with_udf':
            fallback_counts['extract_boosted_group_spans_with_udf'] += 1
        return original_extract_boosted_group_spans_python(query, runtime_field)

    with tempfile.TemporaryDirectory(prefix='query_udf_python_fallback_budget_') as tmp_dir:
        db_path = Path(tmp_dir) / 'query_udf_budget.sqlite3'
        conn = sqlite3.connect(str(db_path))
        try:
            setup_fixture_db(conn, schema_path, extension_path)
            ref.parse_top_level_group_boost = parse_top_level_group_boost_probe
            ref.parse_whole_scoped_group = parse_whole_scoped_group_probe
            ref.parse_boosted_token_clause_python = parse_boosted_token_clause_python_probe
            ref.parse_simple_boolean_python = parse_simple_boolean_python_probe
            ref.parse_simple_phrase_query_python = parse_simple_phrase_query_python_probe
            ref.parse_phrase_term_boolean_python = parse_phrase_term_boolean_python_probe
            ref.extract_boosted_phrase_spans_python = extract_boosted_phrase_spans_python_probe
            ref.extract_boosted_group_spans_python = extract_boosted_group_spans_python_probe

            options = qc.SearchOptions()
            for query, field in queries:
                try:
                    qc.run_search_backend(
                        conn,
                        query=query,
                        field=field,
                        options=options,
                        limit=20,
                        backend='c',
                    )
                except qc.SearchCompileError:
                    continue
        finally:
            ref.parse_top_level_group_boost = original_parse_top_level_group_boost
            ref.parse_whole_scoped_group = original_parse_whole_scoped_group
            ref.parse_boosted_token_clause_python = original_parse_boosted_token_clause_python
            ref.parse_simple_boolean_python = original_parse_simple_boolean_python
            ref.parse_simple_phrase_query_python = original_parse_simple_phrase_query_python
            ref.parse_phrase_term_boolean_python = original_parse_phrase_term_boolean_python
            ref.extract_boosted_phrase_spans_python = original_extract_boosted_phrase_spans_python
            ref.extract_boosted_group_spans_python = original_extract_boosted_group_spans_python
            conn.close()

    non_zero = {name: count for name, count in fallback_counts.items() if count != 0}
    if non_zero:
        raise SystemExit(
            'error: query helper Python fallback budget drift '
            f'expected=0 actual={non_zero!r}'
        )

    print(
        'ok: query_udf_python_fallback_budget '
        f'queries={len(queries)} fallback_counts={fallback_counts}'
    )


if __name__ == '__main__':
    main()
