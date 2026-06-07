#!/usr/bin/env python3
import json
import sqlite3
import tempfile
from pathlib import Path

import sqlite_query_compat as qc
from generate_compile_baseline import setup_fixture_db


FIXTURE_FILES = [
    'tests/fixtures/queries/inputs.smoke.jsonl',
    'tests/fixtures/queries/inputs.complex.jsonl',
    'tests/fixtures/queries/inputs.snippets.jsonl',
    'tests/fixtures/queries/inputs.parity.jsonl',
    'tests/fixtures/queries/inputs.full.jsonl',
    'tests/fixtures/queries/inputs.real.jsonl',
]

EXPECTED_PY_FALLBACK_QUERIES: set[str] = set()


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
    schema_path = (root / 'ingester' / 'sql' / '001_canonical_schema.sql').resolve()
    extension_path = (root / 'tokenizer' / 'build' / 'sqlite_tokenizer_ar.so').resolve()
    if not schema_path.exists():
        raise SystemExit(f'error: schema not found: {schema_path}')
    if not extension_path.exists():
        raise SystemExit(f'error: tokenizer extension not found: {extension_path}')

    queries = load_queries(root)
    if not queries:
        raise SystemExit('error: no fixture queries loaded for AST fallback budget test')

    fallback_queries: list[str] = []
    original_parse_query_ast = qc.parse_query_ast

    def parse_query_ast_probe(query: str) -> qc.QueryAst:
        fallback_queries.append(query.strip())
        return original_parse_query_ast(query)

    with tempfile.TemporaryDirectory(prefix='ast_udf_python_fallback_budget_') as tmp_dir:
        db_path = Path(tmp_dir) / 'ast_fallback_budget.sqlite3'
        conn = sqlite3.connect(str(db_path))
        try:
            setup_fixture_db(conn, schema_path, extension_path)
            qc.parse_query_ast = parse_query_ast_probe
            for query in queries:
                try:
                    qc.parse_query_ast_with_udf(conn, query)
                except qc.SearchCompileError:
                    continue
        finally:
            qc.parse_query_ast = original_parse_query_ast
            conn.close()

    actual_set = set(fallback_queries)
    if actual_set != EXPECTED_PY_FALLBACK_QUERIES:
        missing = sorted(EXPECTED_PY_FALLBACK_QUERIES - actual_set)
        extra = sorted(actual_set - EXPECTED_PY_FALLBACK_QUERIES)
        preview = sorted(actual_set)[:12]
        raise SystemExit(
            'error: AST UDF Python fallback budget drift '
            f'expected={len(EXPECTED_PY_FALLBACK_QUERIES)} actual={len(actual_set)} '
            f'missing={missing!r} extra={extra!r} preview={preview!r}'
        )

    print(
        'ok: ast_udf_python_fallback_budget '
        f'queries={len(queries)} fallback_queries={len(actual_set)}'
    )


if __name__ == '__main__':
    main()
