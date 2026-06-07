#!/usr/bin/env python3
import sqlite3
import tempfile
from pathlib import Path

from run_backend_parity_gate import run_backend_parity_gate
from sqlite_query_compat import SearchOptions


def main() -> None:
    query_compat_dir = Path(__file__).resolve().parents[1]
    schema_path = (query_compat_dir.parent / 'ingester' / 'sql' / '001_canonical_schema.sql').resolve()
    extension_path = (query_compat_dir.parent / 'tokenizer' / 'build' / 'sqlite_tokenizer_ar.so').resolve()
    if not schema_path.exists():
        raise SystemExit(f'error: schema not found: {schema_path}')
    if not extension_path.exists():
        raise SystemExit(f'error: tokenizer extension not found: {extension_path}')

    with tempfile.TemporaryDirectory(prefix='backend_parity_gate_') as tmp_dir:
        db_path = Path(tmp_dir) / 'search.sqlite3'
        conn = sqlite3.connect(str(db_path))
        try:
            conn.enable_load_extension(True)
            conn.load_extension(str(extension_path))
            conn.executescript(schema_path.read_text(encoding='utf-8'))
            conn.execute(
                """
                INSERT INTO page_doc_map(rowid, book_id, page_id, page_no, source_deleted, source_sha1)
                VALUES(1, 10, 11, 11, 0, '')
                """
            )
            conn.execute('INSERT INTO page_fts(rowid, body) VALUES(1, ?)', ('المنصور باحث في اللغة',))
            conn.execute('INSERT INTO page_content_store(rowid, body) VALUES(1, ?)', ('المنصور باحث في اللغة',))
            conn.commit()

            queries = [{'id': 'q1', 'query': 'المنصور'}]
            options = SearchOptions()

            summary_ok = run_backend_parity_gate(
                conn,
                queries,
                field='page',
                limit=20,
                options=options,
                left_backend='python',
                right_backend='python',
            )
            if summary_ok['status'] != 'ok' or summary_ok['mismatch_count'] != 0 or summary_ok['compared'] != 1:
                raise SystemExit(f'error: unexpected python/python parity result: {summary_ok!r}')

            summary_c = run_backend_parity_gate(
                conn,
                queries,
                field='page',
                limit=20,
                options=options,
                left_backend='python',
                right_backend='c',
            )
            if (
                str(summary_c.get('status', '')) != 'ok'
                or int(summary_c.get('mismatch_count', -1)) != 0
                or int(summary_c.get('compared', -1)) != len(queries)
            ):
                raise SystemExit(f'error: unexpected c backend parity result: {summary_c!r}')

            print('ok: backend_parity_gate_runner')
        finally:
            conn.close()


if __name__ == '__main__':
    main()
