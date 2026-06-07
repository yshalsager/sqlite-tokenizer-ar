#!/usr/bin/env python3
import argparse
import json
import sqlite3
import tempfile
from pathlib import Path

from sqlite_query_compat import SearchCompileError, SearchOptions, run_search


def setup_fixture_db(conn: sqlite3.Connection, schema_path: Path, extension_path: Path) -> None:
    conn.enable_load_extension(True)
    conn.load_extension(str(extension_path))
    conn.executescript(schema_path.read_text(encoding='utf-8'))

    page_docs = [
        (1, 10, 11, 'المنصور باحث في اللغة'),
        (2, 10, 12, 'المصور يكتب المقال'),
        (3, 10, 13, 'قُرْآن وإيمان ١٢٣'),
        (4, 10, 14, 'قران وايمان 123'),
        (5, 10, 15, 'المنصور مزور'),
        (6, 10, 16, 'منصور متقدم'),
        (7, 10, 17, 'مصور ماهر'),
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

    title_docs = [
        (1, 10, 21, 'باب المنصور'),
        (2, 10, 22, 'باب المصور'),
        (3, 10, 23, 'مدخل اللغة'),
    ]
    for rowid, book_id, title_id, title in title_docs:
        conn.execute(
            """
            INSERT INTO title_doc_map(rowid, book_id, title_id, title_level, title_parent_id, source_deleted, source_sha1)
            VALUES(?, ?, ?, 1, NULL, 0, '')
            """,
            (rowid, book_id, title_id),
        )
        conn.execute('INSERT INTO title_fts(rowid, title) VALUES(?, ?)', (rowid, title))
        conn.execute('INSERT INTO title_content_store(rowid, title) VALUES(?, ?)', (rowid, title))
    conn.commit()


def load_compile_inputs(path: Path) -> list[dict]:
    rows = []
    for line in path.read_text(encoding='utf-8').splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        rows.append(
            {
                'id': str(row.get('id', '')),
                'query': str(row.get('query', '')),
                'field': str(row.get('field', 'both')),
                'lenient_parse_errors': bool(row.get('lenient_parse_errors', False)),
                'expect_error': str(row.get('expect_error', '')).strip(),
            }
        )
    return rows


def main() -> None:
    parser = argparse.ArgumentParser(description='Generate compile-plan baseline fixture')
    parser.add_argument(
        '--inputs',
        default='tests/fixtures/queries/compile.core.inputs.jsonl',
        help='compile input JSONL path',
    )
    parser.add_argument(
        '--out',
        default='tests/fixtures/queries/compile.baseline.core.jsonl',
        help='output baseline JSONL path',
    )
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[2]
    schema_path = (root / 'ingester' / 'sql' / '001_canonical_schema.sql').resolve()
    extension_path = (root / 'tokenizer' / 'build' / 'sqlite_tokenizer_ar.so').resolve()
    if not schema_path.exists():
        raise SystemExit(f'error: schema not found: {schema_path}')
    if not extension_path.exists():
        raise SystemExit(f'error: tokenizer extension not found: {extension_path}')

    input_path = (root / args.inputs).resolve()
    if not input_path.exists():
        raise SystemExit(f'error: compile input fixture not found: {input_path}')
    rows = load_compile_inputs(input_path)

    out_rows: list[dict] = []
    with tempfile.TemporaryDirectory(prefix='compile_baseline_') as tmp_dir:
        db_path = Path(tmp_dir) / 'compile.sqlite3'
        conn = sqlite3.connect(str(db_path))
        try:
            setup_fixture_db(conn, schema_path, extension_path)
            for row in rows:
                options = SearchOptions(lenient_parse_errors=bool(row['lenient_parse_errors']))
                item = {
                    'id': row['id'],
                    'query': row['query'],
                    'field': row['field'],
                    'lenient_parse_errors': bool(row['lenient_parse_errors']),
                }
                try:
                    result = run_search(conn, query=row['query'], field=row['field'], options=options, limit=20)
                except SearchCompileError as exc:
                    item['status'] = 'error'
                    item['error'] = str(exc)
                else:
                    item['status'] = 'ok'
                    item['compiled'] = result['compiled']
                out_rows.append(item)
        finally:
            conn.close()

    out_path = (root / args.out).resolve()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open('w', encoding='utf-8') as handle:
        for row in out_rows:
            handle.write(json.dumps(row, ensure_ascii=False, separators=(',', ':')))
            handle.write('\n')
    print(f'ok: generated compile baseline rows={len(out_rows)} out={out_path}')


if __name__ == '__main__':
    main()
