#!/usr/bin/env python3
import sqlite3
import tempfile
from pathlib import Path

from generate_compile_baseline import setup_fixture_db
from sqlite_query_compat import SearchCompileError, parse_query_ast_c_backend


SUPPORTED_RUNTIME_QUERIES = [
    '\\(المنصور\\)',
    'المنصور\\ باحث',
    'قران AND ايمان AND 123',
    'المنصور OR المصور -المصور',
    'المنصور OR المصور AND باحث',
    'المنصور AND المصور OR باحث',
]


def main() -> None:
    query_compat_dir = Path(__file__).resolve().parents[1]
    root = query_compat_dir.parent
    schema_path = (root / 'ingester' / 'sql' / '001_canonical_schema.sql').resolve()
    extension_path = (root / 'tokenizer' / 'build' / 'sqlite_tokenizer_ar.so').resolve()
    if not schema_path.exists():
        raise SystemExit(f'error: schema not found: {schema_path}')
    if not extension_path.exists():
        raise SystemExit(f'error: tokenizer extension not found: {extension_path}')

    with tempfile.TemporaryDirectory(prefix='ast_c_runtime_gap_') as tmp_dir:
        db_path = Path(tmp_dir) / 'runtime_gap.sqlite3'
        conn = sqlite3.connect(str(db_path))
        try:
            setup_fixture_db(conn, schema_path, extension_path)

            for query in SUPPORTED_RUNTIME_QUERIES:
                try:
                    parse_query_ast_c_backend(conn, query)
                except SearchCompileError as exc:
                    raise SystemExit(
                        'error: expected C parser support for runtime query '
                        f'query={query!r} error={exc!r}'
                    ) from exc
        finally:
            conn.close()

    print(
        'ok: ast_c_runtime_gap '
        f'supported={len(SUPPORTED_RUNTIME_QUERIES)}'
    )


if __name__ == '__main__':
    main()
