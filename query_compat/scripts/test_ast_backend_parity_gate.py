#!/usr/bin/env python3
import sqlite3
import tempfile
from pathlib import Path

from run_ast_backend_parity_gate import run_ast_backend_parity_gate


def main() -> None:
    query_compat_dir = Path(__file__).resolve().parents[1]
    extension_path = (query_compat_dir.parent / 'tokenizer' / 'build' / 'sqlite_tokenizer_ar.so').resolve()
    if not extension_path.exists():
        raise SystemExit(f'error: tokenizer extension not found: {extension_path}')

    with tempfile.TemporaryDirectory(prefix='ast_backend_parity_gate_') as tmp_dir:
        db_path = Path(tmp_dir) / 'parser.sqlite3'
        conn = sqlite3.connect(str(db_path))
        try:
            conn.enable_load_extension(True)
            conn.load_extension(str(extension_path))
            queries = [
                {'id': 'q1', 'query': 'المنصور AND المصور'},
                {'id': 'q2', 'query': 'title:(المنصور OR المصور)^2'},
            ]

            summary_ok = run_ast_backend_parity_gate(
                conn,
                queries,
                left_backend='python',
                right_backend='python',
            )
            if summary_ok['status'] != 'ok' or summary_ok['mismatch_count'] != 0 or summary_ok['compared'] != len(queries):
                raise SystemExit(f'error: unexpected AST python/python parity result: {summary_ok!r}')

            summary_c = run_ast_backend_parity_gate(
                conn,
                queries,
                left_backend='python',
                right_backend='c',
            )
            if (
                str(summary_c.get('status', '')) != 'ok'
                or int(summary_c.get('mismatch_count', -1)) != 0
                or int(summary_c.get('compared', -1)) != len(queries)
            ):
                raise SystemExit(f'error: unexpected c AST backend parity result: {summary_c!r}')

            print('ok: ast_backend_parity_gate_runner')
        finally:
            conn.close()


if __name__ == '__main__':
    main()
