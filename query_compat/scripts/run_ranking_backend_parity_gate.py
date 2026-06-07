#!/usr/bin/env python3
import argparse
import json
import sqlite3
from pathlib import Path

from generate_ranking_baseline import load_ranking_inputs
from sqlite_query_compat import SearchCompileError, SearchOptions, run_search_backend


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


def run_case(conn: sqlite3.Connection, case: dict, backend: str) -> dict:
    options = SearchOptions(lenient_parse_errors=bool(case.get('lenient_parse_errors', False)))
    try:
        result = run_search_backend(
            conn,
            query=str(case['query']),
            field=str(case['field']),
            options=options,
            limit=max(1, int(case.get('limit', 20))),
            backend=backend,
        )
    except SearchCompileError as exc:
        return {'status': 'error', 'error': str(exc)}
    return {'status': 'ok', 'compiled': result.get('compiled'), 'hits': normalize_hits(result.get('hits', []))}


def rows_match(left: dict, right: dict, tol: float) -> bool:
    if left.get('status') != right.get('status'):
        return False
    if left.get('status') != 'ok':
        return str(left.get('error', '')) == str(right.get('error', ''))
    if left.get('compiled') != right.get('compiled'):
        return False
    left_hits = list(left.get('hits', []))
    right_hits = list(right.get('hits', []))
    if len(left_hits) != len(right_hits):
        return False
    for left_hit, right_hit in zip(left_hits, right_hits):
        left_identity = (left_hit['field'], int(left_hit['book_id']), int(left_hit['item_id']))
        right_identity = (right_hit['field'], int(right_hit['book_id']), int(right_hit['item_id']))
        if left_identity != right_identity:
            return False
        if abs(float(left_hit['score']) - float(right_hit['score'])) > tol:
            return False
    return True


def run_ranking_backend_parity_gate(
    conn: sqlite3.Connection,
    cases: list[dict],
    *,
    left_backend: str,
    right_backend: str,
    score_tolerance: float,
) -> dict:
    mismatches: list[dict] = []
    compared = 0
    for case in cases:
        left = run_case(conn, case, left_backend)
        right = run_case(conn, case, right_backend)
        compared += 1
        if not rows_match(left, right, score_tolerance):
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
    parser = argparse.ArgumentParser(description='Compare ranking outcomes across two query backends')
    parser.add_argument('--db', required=True, help='SQLite DB path')
    parser.add_argument('--inputs', required=True, help='ranking input JSONL path')
    parser.add_argument('--left-backend', default='python', choices=['python', 'c'])
    parser.add_argument('--right-backend', default='c', choices=['python', 'c'])
    parser.add_argument('--score-tolerance', type=float, default=1e-6)
    parser.add_argument('--out', default='', help='optional JSON summary output path')
    args = parser.parse_args()

    db_path = Path(args.db).resolve()
    if not db_path.exists():
        raise SystemExit(f'error: db not found: {db_path}')
    inputs_path = Path(args.inputs).resolve()
    if not inputs_path.exists():
        raise SystemExit(f'error: ranking inputs fixture not found: {inputs_path}')
    cases = load_ranking_inputs(inputs_path)

    conn = sqlite3.connect(str(db_path))
    try:
        summary = run_ranking_backend_parity_gate(
            conn,
            cases,
            left_backend=args.left_backend,
            right_backend=args.right_backend,
            score_tolerance=max(0.0, float(args.score_tolerance)),
        )
    finally:
        conn.close()

    if args.out.strip() != '':
        out_path = Path(args.out).resolve()
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')

    if summary['status'] == 'ok':
        print(
            f"ok: ranking_backend_parity_gate compared={summary['compared']} "
            f"left={args.left_backend} right={args.right_backend}"
        )
        return
    raise SystemExit(
        'error: ranking backend parity mismatches found '
        f"count={summary['mismatch_count']} compared={summary['compared']}"
    )


if __name__ == '__main__':
    main()
