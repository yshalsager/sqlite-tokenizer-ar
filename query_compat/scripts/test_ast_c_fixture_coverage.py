#!/usr/bin/env python3
import json
import sqlite3
from pathlib import Path

from sqlite_query_compat import SearchCompileError, parse_query_ast_backend


FIXTURE_FILES = [
    'tests/fixtures/queries/inputs.smoke.jsonl',
    'tests/fixtures/queries/inputs.complex.jsonl',
    'tests/fixtures/queries/inputs.snippets.jsonl',
]
MIN_SUPPORTED = 47


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
        raise SystemExit('error: no fixture queries loaded for AST C coverage check')

    conn = sqlite3.connect(':memory:')
    try:
        conn.enable_load_extension(True)
        conn.load_extension(str(extension_path))
        supported = 0
        unsupported: list[tuple[str, str, str]] = []
        for rel_path, query_id, query in queries:
            try:
                parse_query_ast_backend(conn, query, 'c')
            except SearchCompileError:
                unsupported.append((rel_path, query_id, query))
            else:
                supported += 1
    finally:
        conn.close()

    total = len(queries)
    if supported < MIN_SUPPORTED:
        preview = unsupported[:5]
        raise SystemExit(
            'error: C AST fixture coverage below floor '
            f'supported={supported} total={total} min_supported={MIN_SUPPORTED} '
            f'preview_unsupported={preview!r}'
        )

    print(f'ok: ast_c_fixture_coverage supported={supported} total={total} unsupported={total - supported}')


if __name__ == '__main__':
    main()
