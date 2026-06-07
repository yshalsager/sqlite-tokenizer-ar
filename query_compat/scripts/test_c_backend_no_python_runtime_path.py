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


def load_queries(root: Path) -> list[tuple[str, str]]:
    rows: list[tuple[str, str]] = []
    for rel_path in FIXTURE_FILES:
        path = (root / rel_path).resolve()
        for line in path.read_text(encoding='utf-8').splitlines():
            if not line.strip():
                continue
            row = json.loads(line)
            query = str(row.get('query', '')).strip()
            if query == '':
                continue
            rows.append((query, str(row.get('field', '') or 'both')))
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
        raise SystemExit('error: no fixture queries loaded for C-path isolation gate')

    original_run_search = qc.run_search
    original_search_field = qc.search_field
    original_build_execution_plan = qc.build_execution_plan
    original_compile_match_expression = qc.compile_match_expression
    original_filter_hits_for_strict_modes = qc.filter_hits_for_strict_modes
    original_parse_strict_literals = qc.parse_strict_literals
    original_strict_literal_matches = qc.strict_literal_matches
    original_literal_presence_matches = qc.literal_presence_matches
    original_apply_clause_term_boosts = qc.apply_clause_term_boosts
    original_lucene_ranker = qc.LuceneRanker
    failures: list[tuple[str, str, str]] = []

    def run_search_probe(*_args, **_kwargs):
        raise RuntimeError('python runtime path called: run_search')

    def search_field_probe(*_args, **_kwargs):
        raise RuntimeError('python runtime path called: search_field')

    def build_execution_plan_probe(*_args, **_kwargs):
        raise RuntimeError('python runtime path called: build_execution_plan')

    def compile_match_expression_probe(*_args, **_kwargs):
        raise RuntimeError('python runtime path called: compile_match_expression')

    def filter_hits_for_strict_modes_probe(*_args, **_kwargs):
        raise RuntimeError('python runtime path called: filter_hits_for_strict_modes')

    def parse_strict_literals_probe(*_args, **_kwargs):
        raise RuntimeError('python runtime path called: parse_strict_literals')

    def strict_literal_matches_probe(*_args, **_kwargs):
        raise RuntimeError('python runtime path called: strict_literal_matches')

    def literal_presence_matches_probe(*_args, **_kwargs):
        raise RuntimeError('python runtime path called: literal_presence_matches')

    def apply_clause_term_boosts_probe(*_args, **_kwargs):
        raise RuntimeError('python runtime path called: apply_clause_term_boosts')

    class LuceneRankerProbe:
        def __init__(self, *_args, **_kwargs):
            raise RuntimeError('python runtime path called: LuceneRanker')

    with tempfile.TemporaryDirectory(prefix='c_backend_no_python_runtime_path_') as tmp_dir:
        db_path = Path(tmp_dir) / 'c_path_guard.sqlite3'
        conn = sqlite3.connect(str(db_path))
        try:
            setup_fixture_db(conn, schema_path, extension_path)
            qc.run_search = run_search_probe
            qc.search_field = search_field_probe
            qc.build_execution_plan = build_execution_plan_probe
            qc.compile_match_expression = compile_match_expression_probe
            qc.filter_hits_for_strict_modes = filter_hits_for_strict_modes_probe
            qc.parse_strict_literals = parse_strict_literals_probe
            qc.strict_literal_matches = strict_literal_matches_probe
            qc.literal_presence_matches = literal_presence_matches_probe
            qc.apply_clause_term_boosts = apply_clause_term_boosts_probe
            qc.LuceneRanker = LuceneRankerProbe

            options = qc.SearchOptions()
            for query, field in queries:
                try:
                    qc.run_search_backend(
                        conn,
                        query=query,
                        field=field,
                        options=options,
                        limit=20,
                        backend='c',
                    )
                except qc.SearchCompileError:
                    continue
                except RuntimeError as exc:
                    failures.append((query, field, str(exc)))
        finally:
            qc.run_search = original_run_search
            qc.search_field = original_search_field
            qc.build_execution_plan = original_build_execution_plan
            qc.compile_match_expression = original_compile_match_expression
            qc.filter_hits_for_strict_modes = original_filter_hits_for_strict_modes
            qc.parse_strict_literals = original_parse_strict_literals
            qc.strict_literal_matches = original_strict_literal_matches
            qc.literal_presence_matches = original_literal_presence_matches
            qc.apply_clause_term_boosts = original_apply_clause_term_boosts
            qc.LuceneRanker = original_lucene_ranker
            conn.close()

    if failures:
        raise SystemExit(
            'error: C backend used Python runtime path '
            f'count={len(failures)} preview={failures[:10]!r}'
        )

    print(f'ok: c_backend_no_python_runtime_path queries={len(queries)}')


if __name__ == '__main__':
    main()
