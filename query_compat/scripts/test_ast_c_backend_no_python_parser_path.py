#!/usr/bin/env python3
import json
import sqlite3
from pathlib import Path

import sqlite_query_compat as qc


FIXTURE_FILES = [
    'tests/fixtures/queries/inputs.smoke.jsonl',
    'tests/fixtures/queries/inputs.complex.jsonl',
    'tests/fixtures/queries/inputs.snippets.jsonl',
    'tests/fixtures/queries/inputs.parity.jsonl',
    'tests/fixtures/queries/inputs.full.jsonl',
    'tests/fixtures/queries/inputs.real.jsonl',
]


def load_queries(root: Path) -> list[str]:
    rows: list[str] = []
    for rel_path in FIXTURE_FILES:
        path = (root / rel_path).resolve()
        for line in path.read_text(encoding='utf-8').splitlines():
            if not line.strip():
                continue
            row = json.loads(line)
            query = str(row.get('query', '')).strip()
            if query == '':
                continue
            rows.append(query)
    return rows


def main() -> None:
    query_compat_dir = Path(__file__).resolve().parents[1]
    root = query_compat_dir.parent
    extension_path = (root / 'tokenizer' / 'build' / 'sqlite_tokenizer_ar.so').resolve()
    if not extension_path.exists():
        raise SystemExit(f'error: tokenizer extension not found: {extension_path}')

    queries = load_queries(root)
    if not queries:
        raise SystemExit('error: no fixture queries loaded for AST C-path isolation gate')

    original_split_query = qc.split_query
    original_parse_query_ast = qc.parse_query_ast
    original_iter_query_ast_leaves = qc.iter_query_ast_leaves
    failures: list[tuple[str, str]] = []

    def split_query_probe(*_args, **_kwargs):
        raise RuntimeError('python parser path called: split_query')

    def parse_query_ast_probe(*_args, **_kwargs):
        raise RuntimeError('python parser path called: parse_query_ast')

    def iter_query_ast_leaves_probe(*_args, **_kwargs):
        raise RuntimeError('python parser path called: iter_query_ast_leaves')

    conn = sqlite3.connect(':memory:')
    try:
        conn.enable_load_extension(True)
        conn.load_extension(str(extension_path))

        qc.split_query = split_query_probe
        qc.parse_query_ast = parse_query_ast_probe
        qc.iter_query_ast_leaves = iter_query_ast_leaves_probe

        for query in queries:
            try:
                qc.parse_query_ast_backend(conn, query, 'c')
                qc.parse_query_ast_with_udf(conn, query)
                qc.iter_query_ast_leaves_with_udf(conn, query)
            except qc.SearchCompileError:
                continue
            except RuntimeError as exc:
                failures.append((query, str(exc)))
    finally:
        qc.split_query = original_split_query
        qc.parse_query_ast = original_parse_query_ast
        qc.iter_query_ast_leaves = original_iter_query_ast_leaves
        conn.close()

    if failures:
        raise SystemExit(
            'error: C AST backend used Python parser path '
            f'count={len(failures)} preview={failures[:10]!r}'
        )

    print(f'ok: ast_c_backend_no_python_parser_path queries={len(queries)}')


if __name__ == '__main__':
    main()
