#!/usr/bin/env python3
import json
import sqlite3
import tempfile
from pathlib import Path

from generate_compile_baseline import setup_fixture_db
from generate_expansion_strict_baseline import evaluate_case


def main() -> None:
    query_compat_dir = Path(__file__).resolve().parents[1]
    root = query_compat_dir.parent
    schema_path = (root / 'ingester' / 'sql' / '001_canonical_schema.sql').resolve()
    extension_path = (root / 'tokenizer' / 'build' / 'sqlite_tokenizer_ar.so').resolve()
    baseline_path = (root / 'tests' / 'fixtures' / 'queries' / 'expansion.strict.baseline.jsonl').resolve()
    if not schema_path.exists():
        raise SystemExit(f'error: schema not found: {schema_path}')
    if not extension_path.exists():
        raise SystemExit(f'error: tokenizer extension not found: {extension_path}')
    if not baseline_path.exists():
        raise SystemExit(f'error: expansion/strict baseline fixture not found: {baseline_path}')

    baseline_rows = []
    for line in baseline_path.read_text(encoding='utf-8').splitlines():
        if not line.strip():
            continue
        baseline_rows.append(json.loads(line))
    if not baseline_rows:
        raise SystemExit('error: expansion/strict baseline fixture is empty')

    checked = 0
    with tempfile.TemporaryDirectory(prefix='expansion_strict_baseline_test_') as tmp_dir:
        db_path = Path(tmp_dir) / 'helpers.sqlite3'
        conn = sqlite3.connect(str(db_path))
        try:
            setup_fixture_db(conn, schema_path, extension_path)
            for row in baseline_rows:
                case_id = str(row.get('id', ''))
                expected = row.get('result')
                actual = evaluate_case(conn, row)
                if actual != expected:
                    raise SystemExit(
                        'error: expansion/strict baseline mismatch '
                        f'id={case_id!r} expected={expected!r} actual={actual!r}'
                    )
                checked += 1
        finally:
            conn.close()

    print(f'ok: expansion_strict_baseline checked={checked}')


if __name__ == '__main__':
    main()
