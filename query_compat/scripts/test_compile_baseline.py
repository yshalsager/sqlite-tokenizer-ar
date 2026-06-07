#!/usr/bin/env python3
import json
import sqlite3
import tempfile
from pathlib import Path

from generate_compile_baseline import setup_fixture_db
from sqlite_query_compat import SearchCompileError, SearchOptions, run_search


def main() -> None:
    query_compat_dir = Path(__file__).resolve().parents[1]
    root = query_compat_dir.parent
    schema_path = (root / 'ingester' / 'sql' / '001_canonical_schema.sql').resolve()
    extension_path = (root / 'tokenizer' / 'build' / 'sqlite_tokenizer_ar.so').resolve()
    baseline_path = (root / 'tests' / 'fixtures' / 'queries' / 'compile.baseline.core.jsonl').resolve()
    if not schema_path.exists():
        raise SystemExit(f'error: schema not found: {schema_path}')
    if not extension_path.exists():
        raise SystemExit(f'error: tokenizer extension not found: {extension_path}')
    if not baseline_path.exists():
        raise SystemExit(f'error: compile baseline fixture not found: {baseline_path}')

    baseline_rows = []
    for line in baseline_path.read_text(encoding='utf-8').splitlines():
        if not line.strip():
            continue
        baseline_rows.append(json.loads(line))
    if not baseline_rows:
        raise SystemExit('error: compile baseline fixture is empty')

    checked = 0
    with tempfile.TemporaryDirectory(prefix='compile_baseline_test_') as tmp_dir:
        db_path = Path(tmp_dir) / 'compile.sqlite3'
        conn = sqlite3.connect(str(db_path))
        try:
            setup_fixture_db(conn, schema_path, extension_path)
            for row in baseline_rows:
                query_id = str(row.get('id', ''))
                query = str(row.get('query', ''))
                field = str(row.get('field', 'both'))
                lenient_parse_errors = bool(row.get('lenient_parse_errors', False))
                expected_status = str(row.get('status', ''))

                options = SearchOptions(lenient_parse_errors=lenient_parse_errors)
                if expected_status == 'error':
                    try:
                        run_search(conn, query=query, field=field, options=options, limit=20)
                    except SearchCompileError as exc:
                        if str(exc) != str(row.get('error', '')):
                            raise SystemExit(
                                'error: compile baseline error mismatch '
                                f'id={query_id!r} query={query!r} field={field!r} '
                                f'expected={row.get("error")!r} actual={str(exc)!r}'
                            )
                    else:
                        raise SystemExit(
                            'error: compile baseline expected error but query compiled '
                            f'id={query_id!r} query={query!r} field={field!r}'
                        )
                elif expected_status == 'ok':
                    result = run_search(conn, query=query, field=field, options=options, limit=20)
                    compiled = result.get('compiled')
                    if compiled != row.get('compiled'):
                        raise SystemExit(
                            'error: compile baseline mismatch '
                            f'id={query_id!r} query={query!r} field={field!r} '
                            f'expected={row.get("compiled")!r} actual={compiled!r}'
                        )
                else:
                    raise SystemExit(
                        'error: compile baseline has unsupported status '
                        f'id={query_id!r} status={expected_status!r}'
                    )
                checked += 1
        finally:
            conn.close()

    print(f'ok: compile_baseline checked={checked}')


if __name__ == '__main__':
    main()
