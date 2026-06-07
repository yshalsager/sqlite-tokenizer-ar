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
        raise SystemExit('error: no fixture queries loaded for scoring-helper fallback budget test')

    fallback_counts = {
        'f32_mul_with_udf': 0,
        'f32_add_with_udf': 0,
        'lucene_idf_with_udf': 0,
        'lucene_term_score_with_udf': 0,
        'levenshtein_distance_with_udf': 0,
    }

    original_f32_mul_python = ref.f32_mul_python
    original_f32_add_python = ref.f32_add_python
    original_lucene_idf_python = ref.lucene_idf_python
    original_lucene_term_score_python = ref.lucene_term_score_python
    original_levenshtein_distance = ref.levenshtein_distance

    def f32_mul_python_probe(left: float, right: float) -> float:
        if caller_name() == 'f32_mul_with_udf':
            fallback_counts['f32_mul_with_udf'] += 1
        return original_f32_mul_python(left, right)

    def f32_add_python_probe(left: float, right: float) -> float:
        if caller_name() == 'f32_add_with_udf':
            fallback_counts['f32_add_with_udf'] += 1
        return original_f32_add_python(left, right)

    def lucene_idf_python_probe(doc_count: int, doc_freq: int) -> float:
        if caller_name() == 'lucene_idf_with_udf':
            fallback_counts['lucene_idf_with_udf'] += 1
        return original_lucene_idf_python(doc_count, doc_freq)

    def lucene_term_score_python_probe(weight: float, tf: float, doc_len: int, k1: float, b: float, avgdl: float) -> float:
        if caller_name() == 'lucene_term_score_with_udf':
            fallback_counts['lucene_term_score_with_udf'] += 1
        return original_lucene_term_score_python(weight, tf, doc_len, k1, b, avgdl)

    def levenshtein_distance_probe(a: str, b: str, max_edits: int) -> int:
        if caller_name() == 'levenshtein_distance_with_udf':
            fallback_counts['levenshtein_distance_with_udf'] += 1
        return original_levenshtein_distance(a, b, max_edits)

    with tempfile.TemporaryDirectory(prefix='scoring_udf_python_fallback_budget_') as tmp_dir:
        db_path = Path(tmp_dir) / 'scoring_udf_budget.sqlite3'
        conn = sqlite3.connect(str(db_path))
        try:
            setup_fixture_db(conn, schema_path, extension_path)
            ref.f32_mul_python = f32_mul_python_probe
            ref.f32_add_python = f32_add_python_probe
            ref.lucene_idf_python = lucene_idf_python_probe
            ref.lucene_term_score_python = lucene_term_score_python_probe
            ref.levenshtein_distance = levenshtein_distance_probe

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
            ref.f32_mul_python = original_f32_mul_python
            ref.f32_add_python = original_f32_add_python
            ref.lucene_idf_python = original_lucene_idf_python
            ref.lucene_term_score_python = original_lucene_term_score_python
            ref.levenshtein_distance = original_levenshtein_distance
            conn.close()

    non_zero = {name: count for name, count in fallback_counts.items() if count != 0}
    if non_zero:
        raise SystemExit(
            'error: scoring helper Python fallback budget drift '
            f'expected=0 actual={non_zero!r}'
        )

    print(
        'ok: scoring_udf_python_fallback_budget '
        f'queries={len(queries)} fallback_counts={fallback_counts}'
    )


if __name__ == '__main__':
    main()
