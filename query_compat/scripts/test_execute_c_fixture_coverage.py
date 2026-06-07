#!/usr/bin/env python3
import json
import sqlite3
import tempfile
from pathlib import Path

from generate_compile_baseline import setup_fixture_db
from sqlite_query_compat import SearchCompileError, SearchOptions, run_search_backend


FIXTURE_FILES = [
    'tests/fixtures/queries/inputs.smoke.jsonl',
    'tests/fixtures/queries/inputs.complex.jsonl',
    'tests/fixtures/queries/inputs.snippets.jsonl',
]


def load_queries(root: Path) -> list[tuple[str, str, str]]:
    rows: list[tuple[str, str, str]] = []
    for rel_path in FIXTURE_FILES:
        path = (root / rel_path).resolve()
        for line in path.read_text(encoding='utf-8').splitlines():
            if not line.strip():
                continue
            row = json.loads(line)
            query = str(row.get('query', '')).strip()
            if query == '':
                continue
            rows.append((rel_path, str(row.get('id', '')), query))
    return rows


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
        raise SystemExit('error: no fixture queries loaded for execute C coverage check')

    unsupported: list[tuple[str, str, str, str]] = []
    mismatch: list[tuple[str, str, str]] = []

    with tempfile.TemporaryDirectory(prefix='execute_c_fixture_coverage_') as tmp_dir:
        db_path = Path(tmp_dir) / 'execute_fixture.sqlite3'
        conn = sqlite3.connect(str(db_path))
        try:
            setup_fixture_db(conn, schema_path, extension_path)
            options = SearchOptions()
            for rel_path, query_id, query in queries:
                py_result = run_search_backend(conn, query=query, field='both', options=options, limit=20, backend='python')
                try:
                    c_result = run_search_backend(conn, query=query, field='both', options=options, limit=20, backend='c')
                except SearchCompileError as exc:
                    message = str(exc)
                    if message.startswith('C backend is not available'):
                        unsupported.append((rel_path, query_id, query, message))
                        continue
                    raise
                if py_result != c_result:
                    mismatch.append((rel_path, query_id, query))
        finally:
            conn.close()

    if unsupported:
        preview = unsupported[:5]
        raise SystemExit(
            'error: execute C fixture coverage has unsupported shapes '
            f'unsupported={len(unsupported)} preview={preview!r}'
        )

    if mismatch:
        preview = mismatch[:5]
        raise SystemExit(
            'error: execute C fixture coverage has backend mismatches '
            f'mismatch={len(mismatch)} preview={preview!r}'
        )

    print(f'ok: execute_c_fixture_coverage checked={len(queries)} unsupported=0 mismatch=0')


if __name__ == '__main__':
    main()
