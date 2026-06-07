#!/usr/bin/env python3
import json
from pathlib import Path

from sqlite_query_compat import parse_query_ast, query_ast_to_debug


def main() -> None:
    query_compat_dir = Path(__file__).resolve().parents[1]
    baseline_path = (query_compat_dir.parent / 'tests' / 'fixtures' / 'queries' / 'ast.baseline.core.jsonl').resolve()
    if not baseline_path.exists():
        raise SystemExit(f'error: AST baseline fixture not found: {baseline_path}')

    checked = 0
    for line in baseline_path.read_text(encoding='utf-8').splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        source = str(row.get('source', ''))
        query_id = str(row.get('id', ''))
        query = str(row.get('query', ''))
        expected = row.get('ast')
        actual = query_ast_to_debug(parse_query_ast(query))
        if actual != expected:
            raise SystemExit(
                'error: AST baseline mismatch '
                f'source={source!r} id={query_id!r} query={query!r} '
                f'expected={expected!r} actual={actual!r}'
            )
        checked += 1

    if checked == 0:
        raise SystemExit('error: AST baseline is empty')
    print(f'ok: ast_baseline checked={checked}')


if __name__ == '__main__':
    main()
