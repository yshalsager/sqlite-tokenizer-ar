#!/usr/bin/env python3
import json
import sqlite3
from pathlib import Path

from sqlite_query_compat import iter_query_ast_leaves, iter_query_ast_leaves_c_backend


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


def to_debug(leaves: list[tuple[object, str]]) -> list[tuple[str, str, str]]:
    out: list[tuple[str, str, str]] = []
    for node, occur in leaves:
        out.append((str(occur), str(getattr(node, 'kind', '')), str(getattr(node, 'value', ''))))
    return out


def main() -> None:
    query_compat_dir = Path(__file__).resolve().parents[1]
    root = query_compat_dir.parent
    extension_path = (root / 'tokenizer' / 'build' / 'sqlite_tokenizer_ar.so').resolve()
    if not extension_path.exists():
        raise SystemExit(f'error: tokenizer extension not found: {extension_path}')

    queries = load_queries(root)
    if not queries:
        raise SystemExit('error: no fixture queries loaded for AST leaves UDF check')

    conn = sqlite3.connect(':memory:')
    try:
        conn.enable_load_extension(True)
        conn.load_extension(str(extension_path))

        checked = 0
        for rel_path, query_id, query in queries:
            py_leaves = to_debug(iter_query_ast_leaves(query))
            c_leaves = to_debug(iter_query_ast_leaves_c_backend(conn, query))
            if c_leaves != py_leaves:
                raise SystemExit(
                    'error: C AST leaves mismatch '
                    f'fixture={rel_path} id={query_id} query={query!r} expected={py_leaves!r} actual={c_leaves!r}'
                )
            checked += 1
    finally:
        conn.close()

    print(f'ok: ast_leaves_udf checked={checked}')


if __name__ == '__main__':
    main()
