#!/usr/bin/env python3
import json
import sqlite3
from pathlib import Path

from sqlite_query_compat import parse_query_ast, parse_query_ast_with_udf, query_ast_to_debug


FIXTURE_FILES = [
    'tests/fixtures/queries/inputs.smoke.jsonl',
    'tests/fixtures/queries/inputs.complex.jsonl',
    'tests/fixtures/queries/inputs.snippets.jsonl',
]


def load_queries(root: Path) -> list[tuple[str, str, str]]:
    rows: list[tuple[str, str, str]] = []
    for rel_path in FIXTURE_FILES:
        path = (root / rel_path).resolve()
        for line in path.read_text(encoding='utf-8').splitlines():
            if not line.strip():
                continue
            row = json.loads(line)
            query = str(row.get('query', '')).strip()
            if query == '':
                continue
            rows.append((rel_path, str(row.get('id', '')), query))
    return rows


def main() -> None:
    query_compat_dir = Path(__file__).resolve().parents[1]
    root = query_compat_dir.parent
    extension_path = (root / 'tokenizer' / 'build' / 'sqlite_tokenizer_ar.so').resolve()
    if not extension_path.exists():
        raise SystemExit(f'error: tokenizer extension not found: {extension_path}')

    queries = load_queries(root)
    if not queries:
        raise SystemExit('error: no fixture queries loaded for AST internal parity check')

    conn = sqlite3.connect(':memory:')
    try:
        conn.enable_load_extension(True)
        conn.load_extension(str(extension_path))

        checked = 0
        for rel_path, query_id, query in queries:
            expected = query_ast_to_debug(parse_query_ast(query))
            actual = query_ast_to_debug(parse_query_ast_with_udf(conn, query))
            if actual != expected:
                raise SystemExit(
                    'error: AST internal parity mismatch '
                    f'fixture={rel_path} id={query_id} query={query!r} expected={expected!r} actual={actual!r}'
                )
            checked += 1
    finally:
        conn.close()

    print(f'ok: ast_udf_internal_parity checked={checked}')


if __name__ == '__main__':
    main()
