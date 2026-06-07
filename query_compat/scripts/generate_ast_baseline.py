#!/usr/bin/env python3
import argparse
import json
from pathlib import Path

from sqlite_query_compat import parse_query_ast, query_ast_to_debug


def parse_inputs_arg(value: str) -> list[str]:
    out = []
    for raw in value.split(','):
        name = raw.strip()
        if name != '':
            out.append(name)
    return out


def main() -> None:
    parser = argparse.ArgumentParser(description='Generate AST baseline fixture from query input fixtures')
    parser.add_argument(
        '--inputs',
        default='inputs.smoke.jsonl,inputs.complex.jsonl,inputs.snippets.jsonl',
        help='comma-separated fixture filenames under tests/fixtures/queries',
    )
    parser.add_argument(
        '--out',
        default='tests/fixtures/queries/ast.baseline.core.jsonl',
        help='output baseline JSONL path',
    )
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[2]
    fixture_dir = root / 'tests' / 'fixtures' / 'queries'
    if not fixture_dir.exists():
        raise SystemExit(f'error: fixture directory not found: {fixture_dir}')

    rows: list[dict] = []
    for name in parse_inputs_arg(args.inputs):
        path = fixture_dir / name
        if not path.exists():
            raise SystemExit(f'error: input fixture not found: {path}')
        for line in path.read_text(encoding='utf-8').splitlines():
            if not line.strip():
                continue
            row = json.loads(line)
            query = str(row.get('query', ''))
            query_id = str(row.get('id', ''))
            ast = query_ast_to_debug(parse_query_ast(query))
            rows.append(
                {
                    'source': name,
                    'id': query_id,
                    'query': query,
                    'ast': ast,
                }
            )

    rows.sort(key=lambda item: (str(item['source']), str(item['id']), str(item['query'])))
    out_path = (root / args.out).resolve()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open('w', encoding='utf-8') as handle:
        for row in rows:
            handle.write(json.dumps(row, ensure_ascii=False, separators=(',', ':')))
            handle.write('\n')
    print(f'ok: generated ast baseline rows={len(rows)} out={out_path}')


if __name__ == '__main__':
    main()
