#!/usr/bin/env python3
import json
import sqlite3
import tempfile
from pathlib import Path

from generate_compile_baseline import setup_fixture_db
from sqlite_query_compat import SearchCompileError, parse_query_ast_c_backend


FIXTURE_FILES = [
    'tests/fixtures/queries/inputs.smoke.jsonl',
    'tests/fixtures/queries/inputs.complex.jsonl',
    'tests/fixtures/queries/inputs.snippets.jsonl',
    'tests/fixtures/queries/inputs.parity.jsonl',
    'tests/fixtures/queries/inputs.full.jsonl',
    'tests/fixtures/queries/inputs.real.jsonl',
]

EXPECTED_UNSUPPORTED: set[tuple[str, str]] = set()

EXPECTED_PARSE_ERRORS = {
    ('tests/fixtures/queries/inputs.full.jsonl', 'q581'),
    ('tests/fixtures/queries/inputs.full.jsonl', 'q582'),
    ('tests/fixtures/queries/inputs.full.jsonl', 'q583'),
    ('tests/fixtures/queries/inputs.full.jsonl', 'q584'),
    ('tests/fixtures/queries/inputs.full.jsonl', 'q585'),
    ('tests/fixtures/queries/inputs.full.jsonl', 'q588'),
    ('tests/fixtures/queries/inputs.full.jsonl', 'q589'),
    ('tests/fixtures/queries/inputs.full.jsonl', 'q590'),
}

EXPECTED_PARSE_ERROR_MESSAGES = {
    ('tests/fixtures/queries/inputs.full.jsonl', 'q581'): 'unclosed group in query',
    ('tests/fixtures/queries/inputs.full.jsonl', 'q582'): 'unmatched closing parenthesis',
    ('tests/fixtures/queries/inputs.full.jsonl', 'q583'): 'unclosed quote in query',
    ('tests/fixtures/queries/inputs.full.jsonl', 'q584'): 'dangling boolean operator',
    ('tests/fixtures/queries/inputs.full.jsonl', 'q585'): 'dangling field scope',
    ('tests/fixtures/queries/inputs.full.jsonl', 'q588'): 'dangling boolean operator',
    ('tests/fixtures/queries/inputs.full.jsonl', 'q589'): 'unclosed field-group parentheses in query',
    ('tests/fixtures/queries/inputs.full.jsonl', 'q590'): 'unclosed group in query',
}


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
    schema_path = (root / 'ingester' / 'sql' / '001_canonical_schema.sql').resolve()
    extension_path = (root / 'tokenizer' / 'build' / 'sqlite_tokenizer_ar.so').resolve()
    if not schema_path.exists():
        raise SystemExit(f'error: schema not found: {schema_path}')
    if not extension_path.exists():
        raise SystemExit(f'error: tokenizer extension not found: {extension_path}')

    queries = load_queries(root)
    if not queries:
        raise SystemExit('error: no fixture queries loaded for full AST C coverage check')

    unsupported: list[tuple[str, str, str]] = []
    parse_errors: list[tuple[str, str, str, str]] = []

    with tempfile.TemporaryDirectory(prefix='ast_c_full_fixture_coverage_') as tmp_dir:
        db_path = Path(tmp_dir) / 'ast_c_full_fixture.sqlite3'
        conn = sqlite3.connect(str(db_path))
        try:
            setup_fixture_db(conn, schema_path, extension_path)
            for rel_path, query_id, query in queries:
                try:
                    parse_query_ast_c_backend(conn, query)
                except SearchCompileError as exc:
                    message = str(exc)
                    if 'unsupported query shape for current C parser rollout' in message:
                        unsupported.append((rel_path, query_id, query))
                        continue
                    parse_errors.append((rel_path, query_id, query, message))
        finally:
            conn.close()

    unsupported_keys = {(rel_path, query_id) for rel_path, query_id, _query in unsupported}
    if unsupported_keys != EXPECTED_UNSUPPORTED:
        preview = unsupported[:10]
        missing = sorted(EXPECTED_UNSUPPORTED - unsupported_keys)
        extra = sorted(unsupported_keys - EXPECTED_UNSUPPORTED)
        raise SystemExit(
            'error: AST C full fixture unsupported set drift '
            f'expected={len(EXPECTED_UNSUPPORTED)} actual={len(unsupported_keys)} '
            f'missing={missing!r} extra={extra!r} preview={preview!r}'
        )

    parse_error_keys = {(rel_path, query_id) for rel_path, query_id, _query, _message in parse_errors}
    if parse_error_keys != EXPECTED_PARSE_ERRORS:
        preview = parse_errors[:10]
        missing = sorted(EXPECTED_PARSE_ERRORS - parse_error_keys)
        extra = sorted(parse_error_keys - EXPECTED_PARSE_ERRORS)
        raise SystemExit(
            'error: AST C full fixture parse-error set drift '
            f'expected={len(EXPECTED_PARSE_ERRORS)} actual={len(parse_error_keys)} '
            f'missing={missing!r} extra={extra!r} preview={preview!r}'
        )

    parse_error_message_map = {(rel_path, query_id): message for rel_path, query_id, _query, message in parse_errors}
    if parse_error_message_map != EXPECTED_PARSE_ERROR_MESSAGES:
        mismatched = []
        for key, expected_message in EXPECTED_PARSE_ERROR_MESSAGES.items():
            actual_message = parse_error_message_map.get(key)
            if actual_message != expected_message:
                mismatched.append((key, expected_message, actual_message))
            if len(mismatched) >= 10:
                break
        raise SystemExit(
            'error: AST C full fixture parse-error message drift '
            f'count={len(mismatched)} preview={mismatched!r}'
        )

    print(
        f'ok: ast_c_full_fixture_coverage checked={len(queries)} '
        f'unsupported={len(unsupported_keys)} parse_errors={len(parse_error_keys)}'
    )


if __name__ == '__main__':
    main()
