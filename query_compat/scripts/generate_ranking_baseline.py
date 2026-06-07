#!/usr/bin/env python3
import argparse
import json
import sqlite3
import tempfile
from pathlib import Path

from sqlite_query_compat import SearchCompileError, SearchOptions, run_search


def setup_ranking_fixture_db(conn: sqlite3.Connection, schema_path: Path, extension_path: Path) -> None:
    conn.enable_load_extension(True)
    conn.load_extension(str(extension_path))
    conn.executescript(schema_path.read_text(encoding='utf-8'))

    page_docs = [
        (1, 10, 101, 'منصور في اللغة'),
        (2, 10, 102, 'منصور منصور في اللغة'),
        (3, 10, 103, 'مصور في اللغة'),
        (4, 10, 104, 'طريق العلم نافع'),
        (5, 10, 105, 'طريق طويل إلى العلم النافع'),
        (6, 10, 106, 'قال العلم نور للطلاب'),
        (7, 10, 107, 'العلم نور للطلاب'),
        (8, 10, 108, 'نور العلم نور'),
        (9, 10, 109, 'كتاب مفيد'),
        (10, 10, 110, 'كتاب مفيد'),
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
        (1, 10, 201, 'باب منصور'),
        (2, 10, 202, 'باب العلم نور'),
        (3, 10, 203, 'كتاب مفيد'),
        (4, 10, 204, 'كتاب مفيد'),
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


def load_ranking_inputs(path: Path) -> list[dict]:
    rows: list[dict] = []
    for line in path.read_text(encoding='utf-8').splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        rows.append(
            {
                'id': str(row.get('id', '')),
                'query': str(row.get('query', '')),
                'field': str(row.get('field', 'page')),
                'limit': max(1, int(row.get('limit', 20))),
                'lenient_parse_errors': bool(row.get('lenient_parse_errors', False)),
            }
        )
    return rows


def normalize_hits(hits: list[dict]) -> list[dict]:
    out = []
    for hit in hits:
        out.append(
            {
                'field': str(hit['field']),
                'book_id': int(hit['book_id']),
                'item_id': int(hit['item_id']),
                'score': float(hit['score']),
            }
        )
    return out


def main() -> None:
    parser = argparse.ArgumentParser(description='Generate ranking baseline fixture')
    parser.add_argument(
        '--inputs',
        default='tests/fixtures/queries/ranking.core.inputs.jsonl',
        help='ranking input JSONL path',
    )
    parser.add_argument(
        '--out',
        default='tests/fixtures/queries/ranking.baseline.core.jsonl',
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
        raise SystemExit(f'error: ranking input fixture not found: {input_path}')
    rows = load_ranking_inputs(input_path)

    out_rows: list[dict] = []
    with tempfile.TemporaryDirectory(prefix='ranking_baseline_') as tmp_dir:
        db_path = Path(tmp_dir) / 'ranking.sqlite3'
        conn = sqlite3.connect(str(db_path))
        try:
            setup_ranking_fixture_db(conn, schema_path, extension_path)
            for row in rows:
                options = SearchOptions(lenient_parse_errors=bool(row['lenient_parse_errors']))
                item = {
                    'id': row['id'],
                    'query': row['query'],
                    'field': row['field'],
                    'limit': int(row['limit']),
                    'lenient_parse_errors': bool(row['lenient_parse_errors']),
                    'score_tolerance': 1e-6,
                }
                try:
                    result = run_search(conn, query=row['query'], field=row['field'], options=options, limit=int(row['limit']))
                except SearchCompileError as exc:
                    item['status'] = 'error'
                    item['error'] = str(exc)
                else:
                    item['status'] = 'ok'
                    item['compiled'] = result['compiled']
                    item['hits'] = normalize_hits(result['hits'])
                out_rows.append(item)
        finally:
            conn.close()

    out_path = (root / args.out).resolve()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open('w', encoding='utf-8') as handle:
        for row in out_rows:
            handle.write(json.dumps(row, ensure_ascii=False, separators=(',', ':')))
            handle.write('\n')
    print(f'ok: generated ranking baseline rows={len(out_rows)} out={out_path}')


if __name__ == '__main__':
    main()
