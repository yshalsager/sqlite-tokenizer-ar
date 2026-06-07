#!/usr/bin/env python3
import json
import sqlite3
import tempfile
from pathlib import Path

from generate_ranking_baseline import setup_ranking_fixture_db
from sqlite_query_compat import SearchCompileError, SearchOptions, run_search


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


def assert_deterministic_ordering(case_id: str, field: str, hits: list[dict], tol: float) -> None:
    if not hits:
        return
    prev_score = float(hits[0]['score'])
    prev_tie = (
        (str(hits[0]['field']), int(hits[0]['book_id']), int(hits[0]['item_id']))
        if field == 'both'
        else (int(hits[0]['book_id']), int(hits[0]['item_id']))
    )
    for index in range(1, len(hits)):
        row = hits[index]
        score = float(row['score'])
        tie = (
            (str(row['field']), int(row['book_id']), int(row['item_id']))
            if field == 'both'
            else (int(row['book_id']), int(row['item_id']))
        )
        if score < (prev_score - tol):
            raise SystemExit(
                'error: ranking output is not sorted by score '
                f'id={case_id!r} prev_score={prev_score} score={score} index={index}'
            )
        if abs(score - prev_score) <= tol and tie < prev_tie:
            raise SystemExit(
                'error: ranking tie-break is not deterministic '
                f'id={case_id!r} prev_tie={prev_tie!r} tie={tie!r} index={index}'
            )
        prev_score = score
        prev_tie = tie


def main() -> None:
    query_compat_dir = Path(__file__).resolve().parents[1]
    root = query_compat_dir.parent
    schema_path = (root / 'ingester' / 'sql' / '001_canonical_schema.sql').resolve()
    extension_path = (root / 'tokenizer' / 'build' / 'sqlite_tokenizer_ar.so').resolve()
    baseline_path = (root / 'tests' / 'fixtures' / 'queries' / 'ranking.baseline.core.jsonl').resolve()
    if not schema_path.exists():
        raise SystemExit(f'error: schema not found: {schema_path}')
    if not extension_path.exists():
        raise SystemExit(f'error: tokenizer extension not found: {extension_path}')
    if not baseline_path.exists():
        raise SystemExit(f'error: ranking baseline fixture not found: {baseline_path}')

    baseline_rows = []
    for line in baseline_path.read_text(encoding='utf-8').splitlines():
        if not line.strip():
            continue
        baseline_rows.append(json.loads(line))
    if not baseline_rows:
        raise SystemExit('error: ranking baseline fixture is empty')

    checked = 0
    with tempfile.TemporaryDirectory(prefix='ranking_baseline_test_') as tmp_dir:
        db_path = Path(tmp_dir) / 'ranking.sqlite3'
        conn = sqlite3.connect(str(db_path))
        try:
            setup_ranking_fixture_db(conn, schema_path, extension_path)
            for row in baseline_rows:
                case_id = str(row.get('id', ''))
                query = str(row.get('query', ''))
                field = str(row.get('field', 'page'))
                limit = max(1, int(row.get('limit', 20)))
                expected_status = str(row.get('status', ''))
                tol = float(row.get('score_tolerance', 1e-6))
                options = SearchOptions(lenient_parse_errors=bool(row.get('lenient_parse_errors', False)))

                if expected_status == 'error':
                    try:
                        run_search(conn, query=query, field=field, options=options, limit=limit)
                    except SearchCompileError as exc:
                        if str(exc) != str(row.get('error', '')):
                            raise SystemExit(
                                'error: ranking baseline error mismatch '
                                f'id={case_id!r} expected={row.get("error")!r} actual={str(exc)!r}'
                            )
                    else:
                        raise SystemExit(f'error: expected compile error for ranking case id={case_id!r}')
                    checked += 1
                    continue

                if expected_status != 'ok':
                    raise SystemExit(f'error: ranking baseline has unsupported status id={case_id!r} status={expected_status!r}')

                result = run_search(conn, query=query, field=field, options=options, limit=limit)
                if result.get('compiled') != row.get('compiled'):
                    raise SystemExit(
                        'error: ranking baseline compiled mismatch '
                        f'id={case_id!r} expected={row.get("compiled")!r} actual={result.get("compiled")!r}'
                    )

                actual_hits = normalize_hits(result.get('hits', []))
                expected_hits = list(row.get('hits', []))
                if len(actual_hits) != len(expected_hits):
                    raise SystemExit(
                        'error: ranking baseline hit count mismatch '
                        f'id={case_id!r} expected={len(expected_hits)} actual={len(actual_hits)}'
                    )

                for idx, (actual, expected) in enumerate(zip(actual_hits, expected_hits)):
                    actual_identity = (actual['field'], actual['book_id'], actual['item_id'])
                    expected_identity = (str(expected['field']), int(expected['book_id']), int(expected['item_id']))
                    if actual_identity != expected_identity:
                        raise SystemExit(
                            'error: ranking identity/order mismatch '
                            f'id={case_id!r} idx={idx} expected={expected_identity!r} actual={actual_identity!r}'
                        )
                    actual_score = float(actual['score'])
                    expected_score = float(expected['score'])
                    if abs(actual_score - expected_score) > tol:
                        raise SystemExit(
                            'error: ranking score mismatch '
                            f'id={case_id!r} idx={idx} expected={expected_score} actual={actual_score} tol={tol}'
                        )

                assert_deterministic_ordering(case_id, field, actual_hits, tol)
                checked += 1
        finally:
            conn.close()

    print(f'ok: ranking_baseline checked={checked}')


if __name__ == '__main__':
    main()
