#!/usr/bin/env python3
import json
import sqlite3
import tempfile
from pathlib import Path

import python_reference_helpers as ref
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


def raising_reference(name: str):
    def _raiser(*_args, **_kwargs):
        raise RuntimeError(f'reference helper called from c backend: {name}')

    return _raiser


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
        raise SystemExit('error: no fixture queries loaded for c-backend reference-helper gate')

    helper_names = [
        'parse_top_level_group_boost',
        'parse_whole_scoped_group',
        'parse_boosted_token_clause_python',
        'parse_simple_boolean_python',
        'parse_simple_phrase_query_python',
        'parse_single_phrase_clause_python',
        'parse_phrase_term_boolean_python',
        'extract_boosted_phrase_spans_python',
        'extract_boosted_group_spans_python',
        'f32_mul_python',
        'f32_add_python',
        'levenshtein_distance',
        'build_norm_inverse_cache_python',
        'lucene_term_score_python',
        'lucene_idf_python',
    ]

    originals: dict[str, object] = {name: getattr(ref, name) for name in helper_names}
    failures: list[tuple[str, str, str]] = []

    with tempfile.TemporaryDirectory(prefix='python_reference_unused_c_backend_') as tmp_dir:
        db_path = Path(tmp_dir) / 'reference_unused.sqlite3'
        conn = sqlite3.connect(str(db_path))
        try:
            setup_fixture_db(conn, schema_path, extension_path)
            for name in helper_names:
                setattr(ref, name, raising_reference(name))

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
            for name, original in originals.items():
                setattr(ref, name, original)
            conn.close()

    if failures:
        raise SystemExit(
            'error: c backend executed python reference helper '
            f'count={len(failures)} preview={failures[:10]!r}'
        )

    print(f'ok: python_reference_unused_c_backend queries={len(queries)}')


if __name__ == '__main__':
    main()
