#!/usr/bin/env python3
import argparse
import json
import sqlite3
from pathlib import Path

from sqlite_query_compat import SearchCompileError, parse_query_ast_backend


def load_queries(path: Path) -> list[dict]:
    rows: list[dict] = []
    for line in path.read_text(encoding='utf-8').splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        query = str(row.get('query', '')).strip()
        if query == '':
            continue
        rows.append({'id': str(row.get('id', '')), 'query': query})
    return rows


def run_ast_backend_parity_gate(
    conn: sqlite3.Connection,
    queries: list[dict],
    *,
    left_backend: str,
    right_backend: str,
) -> dict:
    mismatches: list[dict] = []
    compared = 0
    for row in queries:
        query_id = str(row.get('id', ''))
        query = str(row['query'])
        left = parse_query_ast_backend(conn, query, left_backend)
        try:
            right = parse_query_ast_backend(conn, query, right_backend)
        except SearchCompileError as exc:
            raise
        compared += 1
        if left != right:
            mismatches.append(
                {
                    'id': query_id,
                    'query': query,
                    'left': left,
                    'right': right,
                }
            )

    status = 'ok' if len(mismatches) == 0 else 'mismatch'
    return {
        'status': status,
        'compared': compared,
        'mismatch_count': len(mismatches),
        'mismatches': mismatches,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description='Compare AST output across two parser backends')
    parser.add_argument('--db', required=True, help='SQLite DB path')
    parser.add_argument('--queries', required=True, help='JSONL query fixture path (id/query)')
    parser.add_argument('--left-backend', default='python', choices=['python', 'c'])
    parser.add_argument('--right-backend', default='c', choices=['python', 'c'])
    parser.add_argument('--out', default='', help='optional JSON summary output path')
    args = parser.parse_args()

    db_path = Path(args.db).resolve()
    if not db_path.exists():
        raise SystemExit(f'error: db not found: {db_path}')
    queries_path = Path(args.queries).resolve()
    if not queries_path.exists():
        raise SystemExit(f'error: queries fixture not found: {queries_path}')
    queries = load_queries(queries_path)

    conn = sqlite3.connect(str(db_path))
    try:
        summary = run_ast_backend_parity_gate(
            conn,
            queries,
            left_backend=args.left_backend,
            right_backend=args.right_backend,
        )
    finally:
        conn.close()

    if args.out.strip() != '':
        out_path = Path(args.out).resolve()
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')

    if summary['status'] == 'ok':
        print(
            f"ok: ast_backend_parity_gate compared={summary['compared']} "
            f"left={args.left_backend} right={args.right_backend}"
        )
        return
    raise SystemExit(
        'error: AST backend parity mismatches found '
        f"count={summary['mismatch_count']} compared={summary['compared']}"
    )


if __name__ == '__main__':
    main()
