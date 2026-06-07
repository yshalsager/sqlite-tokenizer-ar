#!/usr/bin/env python3
import sqlite3
import tempfile
from pathlib import Path

from generate_ranking_baseline import setup_ranking_fixture_db
from run_ranking_backend_parity_gate import load_ranking_inputs, run_ranking_backend_parity_gate


def main() -> None:
    query_compat_dir = Path(__file__).resolve().parents[1]
    root = query_compat_dir.parent
    schema_path = (root / 'ingester' / 'sql' / '001_canonical_schema.sql').resolve()
    extension_path = (root / 'tokenizer' / 'build' / 'sqlite_tokenizer_ar.so').resolve()
    inputs_path = (root / 'tests' / 'fixtures' / 'queries' / 'ranking.core.inputs.jsonl').resolve()
    if not schema_path.exists():
        raise SystemExit(f'error: schema not found: {schema_path}')
    if not extension_path.exists():
        raise SystemExit(f'error: tokenizer extension not found: {extension_path}')
    if not inputs_path.exists():
        raise SystemExit(f'error: ranking inputs fixture not found: {inputs_path}')
    cases = load_ranking_inputs(inputs_path)

    with tempfile.TemporaryDirectory(prefix='ranking_backend_parity_gate_test_') as tmp_dir:
        db_path = Path(tmp_dir) / 'ranking.sqlite3'
        conn = sqlite3.connect(str(db_path))
        try:
            setup_ranking_fixture_db(conn, schema_path, extension_path)
            summary_ok = run_ranking_backend_parity_gate(
                conn,
                cases,
                left_backend='python',
                right_backend='python',
                score_tolerance=1e-6,
            )
            if summary_ok.get('status') != 'ok' or int(summary_ok.get('mismatch_count', -1)) != 0:
                raise SystemExit(f'error: unexpected ranking python/python parity result: {summary_ok!r}')

            summary_c = run_ranking_backend_parity_gate(
                conn,
                cases,
                left_backend='python',
                right_backend='c',
                score_tolerance=1e-6,
            )
            if (
                str(summary_c.get('status', '')) != 'ok'
                or int(summary_c.get('mismatch_count', -1)) != 0
                or int(summary_c.get('compared', -1)) != len(cases)
            ):
                raise SystemExit(f'error: unexpected ranking backend parity result: {summary_c!r}')
        finally:
            conn.close()

    print('ok: ranking_backend_parity_gate_runner')


if __name__ == '__main__':
    main()
