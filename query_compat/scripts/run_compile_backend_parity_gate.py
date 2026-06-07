#!/usr/bin/env python3
import argparse
import json
import sqlite3
from pathlib import Path

from sqlite_query_compat import SearchCompileError, SearchOptions, run_search_backend


def load_inputs(path: Path) -> list[dict]:
    rows: list[dict] = []
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
            }
        )
    return rows


def run_case(conn: sqlite3.Connection, case: dict, backend: str) -> dict:
    options = SearchOptions(lenient_parse_errors=bool(case['lenient_parse_errors']))
    try:
        result = run_search_backend(
            conn,
            query=str(case['query']),
            field=str(case['field']),
            options=options,
            limit=20,
            backend=backend,
        )
    except SearchCompileError as exc:
        return {'status': 'error', 'error': str(exc)}
    return {'status': 'ok', 'compiled': result.get('compiled')}


def run_compile_backend_parity_gate(
    conn: sqlite3.Connection,
    cases: list[dict],
    *,
    left_backend: str,
    right_backend: str,
) -> dict:
    mismatches: list[dict] = []
    compared = 0
    for case in cases:
        left = run_case(conn, case, left_backend)
        right = run_case(conn, case, right_backend)
        compared += 1
        if left != right:
            mismatches.append(
                {
                    'id': case.get('id', ''),
                    'query': case.get('query', ''),
                    'field': case.get('field', ''),
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
    parser = argparse.ArgumentParser(description='Compare compile outcomes across two query backends')
    parser.add_argument('--db', required=True, help='SQLite DB path')
    parser.add_argument('--inputs', required=True, help='compile input JSONL path')
    parser.add_argument('--left-backend', default='python', choices=['python', 'c'])
    parser.add_argument('--right-backend', default='c', choices=['python', 'c'])
    parser.add_argument('--out', default='', help='optional JSON summary output path')
    args = parser.parse_args()

    db_path = Path(args.db).resolve()
    if not db_path.exists():
        raise SystemExit(f'error: db not found: {db_path}')
    inputs_path = Path(args.inputs).resolve()
    if not inputs_path.exists():
        raise SystemExit(f'error: compile inputs fixture not found: {inputs_path}')
    cases = load_inputs(inputs_path)

    conn = sqlite3.connect(str(db_path))
    try:
        summary = run_compile_backend_parity_gate(
            conn,
            cases,
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
            f"ok: compile_backend_parity_gate compared={summary['compared']} "
            f"left={args.left_backend} right={args.right_backend}"
        )
        return
    raise SystemExit(
        'error: compile backend parity mismatches found '
        f"count={summary['mismatch_count']} compared={summary['compared']}"
    )


if __name__ == '__main__':
    main()
