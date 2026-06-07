#!/usr/bin/env python3
import inspect
import json
import sqlite3
import tempfile
from pathlib import Path

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
        raise SystemExit('error: no fixture queries loaded for token-helper fallback budget test')

    fallback_counts = {
        'parse_scoped_token_with_udf': 0,
        'parse_fuzzy_token_with_udf': 0,
        'strip_boost_with_udf': 0,
    }

    original_parse_scoped_token = qc.parse_scoped_token
    original_parse_fuzzy_token = qc.parse_fuzzy_token
    original_strip_boost = qc.strip_boost

    def parse_scoped_token_probe(token: str) -> tuple[str | None, str]:
        if caller_name() == 'parse_scoped_token_with_udf':
            fallback_counts['parse_scoped_token_with_udf'] += 1
        return original_parse_scoped_token(token)

    def parse_fuzzy_token_probe(token: str) -> tuple[str, int] | None:
        if caller_name() == 'parse_fuzzy_token_with_udf':
            fallback_counts['parse_fuzzy_token_with_udf'] += 1
        return original_parse_fuzzy_token(token)

    def strip_boost_probe(token: str) -> tuple[str, float | None]:
        if caller_name() == 'strip_boost_with_udf':
            fallback_counts['strip_boost_with_udf'] += 1
        return original_strip_boost(token)

    with tempfile.TemporaryDirectory(prefix='token_udf_python_fallback_budget_') as tmp_dir:
        db_path = Path(tmp_dir) / 'token_udf_budget.sqlite3'
        conn = sqlite3.connect(str(db_path))
        try:
            setup_fixture_db(conn, schema_path, extension_path)
            qc.parse_scoped_token = parse_scoped_token_probe
            qc.parse_fuzzy_token = parse_fuzzy_token_probe
            qc.strip_boost = strip_boost_probe

            options = qc.SearchOptions()
            for query, field in queries:
                try:
                    qc.run_search_backend(
                        conn,
                        query=query,
                        field=field,
                        options=options,
                        limit=20,
                        backend='python',
                    )
                except qc.SearchCompileError:
                    continue
        finally:
            qc.parse_scoped_token = original_parse_scoped_token
            qc.parse_fuzzy_token = original_parse_fuzzy_token
            qc.strip_boost = original_strip_boost
            conn.close()

    non_zero = {name: count for name, count in fallback_counts.items() if count != 0}
    if non_zero:
        raise SystemExit(
            'error: token helper Python fallback budget drift '
            f'expected=0 actual={non_zero!r}'
        )

    print(
        'ok: token_udf_python_fallback_budget '
        f'queries={len(queries)} fallback_counts={fallback_counts}'
    )


if __name__ == '__main__':
    main()
