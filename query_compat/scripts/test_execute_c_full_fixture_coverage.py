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
    'tests/fixtures/queries/inputs.parity.jsonl',
    'tests/fixtures/queries/inputs.full.jsonl',
    'tests/fixtures/queries/inputs.real.jsonl',
]

EXPECTED_UNSUPPORTED: set[tuple[str, str]] = set()

EXPECTED_PY_ERRORS = {
    ('tests/fixtures/queries/inputs.full.jsonl', 'q581'),
    ('tests/fixtures/queries/inputs.full.jsonl', 'q582'),
    ('tests/fixtures/queries/inputs.full.jsonl', 'q583'),
    ('tests/fixtures/queries/inputs.full.jsonl', 'q584'),
    ('tests/fixtures/queries/inputs.full.jsonl', 'q585'),
    ('tests/fixtures/queries/inputs.full.jsonl', 'q588'),
    ('tests/fixtures/queries/inputs.full.jsonl', 'q589'),
    ('tests/fixtures/queries/inputs.full.jsonl', 'q590'),
}

EXPECTED_C_ERRORS = {
    ('tests/fixtures/queries/inputs.full.jsonl', 'q581'),
    ('tests/fixtures/queries/inputs.full.jsonl', 'q582'),
    ('tests/fixtures/queries/inputs.full.jsonl', 'q583'),
    ('tests/fixtures/queries/inputs.full.jsonl', 'q584'),
    ('tests/fixtures/queries/inputs.full.jsonl', 'q585'),
    ('tests/fixtures/queries/inputs.full.jsonl', 'q588'),
    ('tests/fixtures/queries/inputs.full.jsonl', 'q589'),
    ('tests/fixtures/queries/inputs.full.jsonl', 'q590'),
}


def load_queries(root: Path) -> list[tuple[str, str, str, str]]:
    rows: list[tuple[str, str, str, str]] = []
    for rel_path in FIXTURE_FILES:
        path = (root / rel_path).resolve()
        for line in path.read_text(encoding='utf-8').splitlines():
            if not line.strip():
                continue
            row = json.loads(line)
            query = str(row.get('query', '')).strip()
            if query == '':
                continue
            rows.append((rel_path, str(row.get('id', '')), query, str(row.get('field', '') or 'both')))
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
        raise SystemExit('error: no fixture queries loaded for full execute C coverage check')

    unsupported: list[tuple[str, str, str, str]] = []
    mismatch: list[tuple[str, str, str]] = []
    py_errors: list[tuple[str, str, str, str]] = []
    c_errors: list[tuple[str, str, str, str]] = []

    with tempfile.TemporaryDirectory(prefix='execute_c_full_fixture_coverage_') as tmp_dir:
        db_path = Path(tmp_dir) / 'execute_full_fixture.sqlite3'
        conn = sqlite3.connect(str(db_path))
        try:
            setup_fixture_db(conn, schema_path, extension_path)
            options = SearchOptions()
            for rel_path, query_id, query, field in queries:
                py_result = None
                c_result = None
                try:
                    py_result = run_search_backend(conn, query=query, field=field, options=options, limit=20, backend='python')
                except SearchCompileError as exc:
                    py_errors.append((rel_path, query_id, query, str(exc)))
                try:
                    c_result = run_search_backend(conn, query=query, field=field, options=options, limit=20, backend='c')
                except SearchCompileError as exc:
                    message = str(exc)
                    if message.startswith('C backend is not available'):
                        unsupported.append((rel_path, query_id, query, message))
                    else:
                        c_errors.append((rel_path, query_id, query, message))

                if py_result is not None and c_result is not None and py_result != c_result:
                    mismatch.append((rel_path, query_id, query))
        finally:
            conn.close()

    if mismatch:
        preview = mismatch[:10]
        raise SystemExit(
            'error: execute C full fixture coverage has backend mismatches '
            f'mismatch={len(mismatch)} preview={preview!r}'
        )

    unsupported_keys = {(rel_path, query_id) for rel_path, query_id, _query, _message in unsupported}
    if unsupported_keys != EXPECTED_UNSUPPORTED:
        preview = unsupported[:10]
        missing = sorted(EXPECTED_UNSUPPORTED - unsupported_keys)
        extra = sorted(unsupported_keys - EXPECTED_UNSUPPORTED)
        raise SystemExit(
            'error: execute C full fixture unsupported set drift '
            f'expected={len(EXPECTED_UNSUPPORTED)} actual={len(unsupported_keys)} '
            f'missing={missing!r} extra={extra!r} preview={preview!r}'
        )

    py_error_keys = {(rel_path, query_id) for rel_path, query_id, _query, _message in py_errors}
    if py_error_keys != EXPECTED_PY_ERRORS:
        preview = py_errors[:10]
        missing = sorted(EXPECTED_PY_ERRORS - py_error_keys)
        extra = sorted(py_error_keys - EXPECTED_PY_ERRORS)
        raise SystemExit(
            'error: python full fixture compile-error set drift '
            f'expected={len(EXPECTED_PY_ERRORS)} actual={len(py_error_keys)} '
            f'missing={missing!r} extra={extra!r} preview={preview!r}'
        )

    c_error_keys = {(rel_path, query_id) for rel_path, query_id, _query, _message in c_errors}
    if c_error_keys != EXPECTED_C_ERRORS:
        preview = c_errors[:10]
        missing = sorted(EXPECTED_C_ERRORS - c_error_keys)
        extra = sorted(c_error_keys - EXPECTED_C_ERRORS)
        raise SystemExit(
            'error: c full fixture compile-error set drift '
            f'expected={len(EXPECTED_C_ERRORS)} actual={len(c_error_keys)} '
            f'missing={missing!r} extra={extra!r} preview={preview!r}'
        )

    py_error_map = {(rel_path, query_id): message for rel_path, query_id, _query, message in py_errors}
    c_error_map = {(rel_path, query_id): message for rel_path, query_id, _query, message in c_errors}
    if c_error_map != py_error_map:
        mismatched = []
        for key, py_message in py_error_map.items():
            c_message = c_error_map.get(key)
            if c_message != py_message:
                mismatched.append((key, py_message, c_message))
            if len(mismatched) >= 10:
                break
        raise SystemExit(
            'error: c full fixture compile-error message drift '
            f'count={len(mismatched)} preview={mismatched!r}'
        )

    print(
        f'ok: execute_c_full_fixture_coverage checked={len(queries)} '
        f'unsupported={len(unsupported_keys)} python_errors={len(py_error_keys)} c_errors={len(c_error_keys)} mismatch=0'
    )


if __name__ == '__main__':
    main()
