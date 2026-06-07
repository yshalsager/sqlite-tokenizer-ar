#!/usr/bin/env python3
import sqlite3
import tempfile
from pathlib import Path

from generate_compile_baseline import setup_fixture_db
from run_compile_backend_parity_gate import run_compile_backend_parity_gate


def main() -> None:
    query_compat_dir = Path(__file__).resolve().parents[1]
    root = query_compat_dir.parent
    schema_path = (root / 'ingester' / 'sql' / '001_canonical_schema.sql').resolve()
    extension_path = (root / 'tokenizer' / 'build' / 'sqlite_tokenizer_ar.so').resolve()
    if not schema_path.exists():
        raise SystemExit(f'error: schema not found: {schema_path}')
    if not extension_path.exists():
        raise SystemExit(f'error: tokenizer extension not found: {extension_path}')

    with tempfile.TemporaryDirectory(prefix='compile_backend_parity_') as tmp_dir:
        db_path = Path(tmp_dir) / 'compile.sqlite3'
        conn = sqlite3.connect(str(db_path))
        try:
            setup_fixture_db(conn, schema_path, extension_path)
            cases = [
                {'id': 'c1', 'query': 'المنصور', 'field': 'both', 'lenient_parse_errors': False},
                {'id': 'c2', 'query': 'title:', 'field': 'both', 'lenient_parse_errors': False},
                {'id': 'c3', 'query': 'title:', 'field': 'both', 'lenient_parse_errors': True},
            ]

            summary_ok = run_compile_backend_parity_gate(
                conn,
                cases,
                left_backend='python',
                right_backend='python',
            )
            if summary_ok['status'] != 'ok' or summary_ok['mismatch_count'] != 0 or summary_ok['compared'] != len(cases):
                raise SystemExit(f'error: unexpected compile python/python parity result: {summary_ok!r}')

            summary_c = run_compile_backend_parity_gate(
                conn,
                cases,
                left_backend='python',
                right_backend='c',
            )
            if (
                str(summary_c.get('status', '')) != 'ok'
                or int(summary_c.get('mismatch_count', -1)) != 0
                or int(summary_c.get('compared', -1)) != len(cases)
            ):
                raise SystemExit(f'error: unexpected compile c backend parity result: {summary_c!r}')

            print('ok: compile_backend_parity_gate_runner')
        finally:
            conn.close()


if __name__ == '__main__':
    main()
