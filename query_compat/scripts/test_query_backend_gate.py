#!/usr/bin/env python3
import sqlite3
import tempfile
from pathlib import Path

from sqlite_query_compat import SearchCompileError, SearchOptions, run_search, run_search_backend


def main() -> None:
    query_compat_dir = Path(__file__).resolve().parents[1]
    schema_path = (query_compat_dir.parent / 'ingester' / 'sql' / '001_canonical_schema.sql').resolve()
    extension_path = (query_compat_dir.parent / 'tokenizer' / 'build' / 'sqlite_tokenizer_ar.so').resolve()
    if not schema_path.exists():
        raise SystemExit(f'error: schema not found: {schema_path}')
    if not extension_path.exists():
        raise SystemExit(f'error: tokenizer extension not found: {extension_path}')

    with tempfile.TemporaryDirectory(prefix='query_backend_gate_') as tmp_dir:
        db_path = Path(tmp_dir) / 'search.sqlite3'
        conn = sqlite3.connect(str(db_path))
        try:
            conn.enable_load_extension(True)
            conn.load_extension(str(extension_path))
            conn.executescript(schema_path.read_text(encoding='utf-8'))

            page_docs = [
                (1, 10, 11, 'المنصور باحث في اللغة'),
                (2, 10, 12, 'المصور يكتب المقال'),
                (3, 10, 13, 'قُرْآن وإيمان ١٢٣'),
            ]
            for rowid, book_id, page_id, body in page_docs:
                conn.execute(
                    """
                    INSERT INTO page_doc_map(rowid, book_id, page_id, page_no, source_deleted, source_sha1)
                    VALUES(?, ?, ?, ?, 0, '')
                    """,
                    (rowid, book_id, page_id, page_id),
                )
                conn.execute('INSERT INTO page_fts(rowid, body) VALUES(?, ?)', (rowid, body))
                conn.execute('INSERT INTO page_content_store(rowid, body) VALUES(?, ?)', (rowid, body))
            conn.commit()

            options = SearchOptions()
            expected_python = run_search(conn, query='المنصور', field='page', options=options, limit=20)
            actual_python = run_search_backend(conn, query='المنصور', field='page', options=options, limit=20, backend='python')
            if expected_python != actual_python:
                raise SystemExit(
                    'error: python backend selector mismatch '
                    f'expected={expected_python!r} actual={actual_python!r}'
                )

            try:
                run_search_backend(conn, query='المنصور', field='page', options=options, limit=20, backend='ruby')
            except SearchCompileError as exc:
                if 'unsupported backend' not in str(exc):
                    raise SystemExit(f'error: unexpected unsupported-backend message: {exc!r}')
            else:
                raise SystemExit('error: expected unsupported backend to raise SearchCompileError')

            try:
                actual_c = run_search_backend(conn, query='المنصور', field='page', options=options, limit=20, backend='c')
            except SearchCompileError as exc:
                message = str(exc)
                if 'C backend is not available' not in message or 'sqlite_tokenizer_ar_execute_query_json' not in message:
                    raise SystemExit(f'error: unexpected c-backend-missing message: {message!r}')
            else:
                if actual_c != expected_python:
                    raise SystemExit(
                        'error: c backend response mismatch when backend is available '
                        f'expected={expected_python!r} actual={actual_c!r}'
                    )

            print('ok: query_backend_gate')
        finally:
            conn.close()


if __name__ == '__main__':
    main()
