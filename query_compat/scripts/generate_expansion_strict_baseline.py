#!/usr/bin/env python3
import argparse
import json
import sqlite3
import tempfile
from pathlib import Path

from generate_compile_baseline import setup_fixture_db
from sqlite_query_compat import (
    SearchOptions,
    StrictLiteral,
    expand_fuzzy_terms,
    expand_suffix_terms,
    expand_wildcard_terms,
    levenshtein_distance_with_udf,
    literal_needs_strict_check,
    normalize_wildcard_pattern,
    strict_literal_matches,
    wildcard_matches_text_with_udf,
)


def load_inputs(path: Path) -> list[dict]:
    rows = []
    for line in path.read_text(encoding='utf-8').splitlines():
        if not line.strip():
            continue
        rows.append(json.loads(line))
    return rows


def parse_options(value: dict | None) -> SearchOptions:
    value = {} if value is None else dict(value)
    return SearchOptions(
        ignore_diacritics=bool(value.get('ignore_diacritics', True)),
        ignore_hamza_forms=bool(value.get('ignore_hamza_forms', True)),
        ignore_letter_forms=bool(value.get('ignore_letter_forms', True)),
        ignore_digit_forms=bool(value.get('ignore_digit_forms', True)),
    )


def evaluate_case(conn: sqlite3.Connection, case: dict) -> object:
    kind = str(case.get('kind', ''))
    if kind == 'expand_suffix':
        return expand_suffix_terms(
            conn,
            str(case['fts_table']),
            str(case['suffix']),
            int(case.get('max_expansions', 256)),
        )
    if kind == 'expand_wildcard':
        return expand_wildcard_terms(
            conn,
            str(case['fts_table']),
            str(case['pattern']),
            int(case.get('max_expansions', 256)),
        )
    if kind == 'expand_fuzzy':
        return expand_fuzzy_terms(
            conn,
            str(case['fts_table']),
            str(case['term']),
            int(case.get('max_edits', 1)),
            int(case.get('max_expansions', 128)),
        )
    if kind == 'normalize_wildcard':
        return normalize_wildcard_pattern(conn, str(case['pattern']))
    if kind == 'wildcard_match':
        return wildcard_matches_text_with_udf(conn, str(case['text']), str(case['pattern']))
    if kind == 'levenshtein':
        return levenshtein_distance_with_udf(
            conn,
            str(case['left']),
            str(case['right']),
            int(case.get('max_edits', 1)),
        )
    if kind == 'literal_needs_strict':
        options = parse_options(case.get('options'))
        return literal_needs_strict_check(conn, str(case['text']), options)
    if kind == 'strict_literal_match':
        options = parse_options(case.get('options'))
        literal = StrictLiteral(
            text=str(case['literal_text']),
            is_phrase=bool(case.get('is_phrase', False)),
            is_pattern=bool(case.get('is_pattern', False)),
            required=True,
            prohibited=False,
        )
        return strict_literal_matches(conn, str(case['raw_text']), literal, options)
    raise SystemExit(f'error: unsupported case kind: {kind!r}')


def main() -> None:
    parser = argparse.ArgumentParser(description='Generate expansion/strict helper baseline fixture')
    parser.add_argument(
        '--inputs',
        default='tests/fixtures/queries/expansion.strict.inputs.jsonl',
        help='input fixture JSONL path',
    )
    parser.add_argument(
        '--out',
        default='tests/fixtures/queries/expansion.strict.baseline.jsonl',
        help='output baseline JSONL path',
    )
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[2]
    schema_path = (root / 'ingester' / 'sql' / '001_canonical_schema.sql').resolve()
    extension_path = (root / 'tokenizer' / 'build' / 'sqlite_tokenizer_ar.so').resolve()
    input_path = (root / args.inputs).resolve()
    if not schema_path.exists():
        raise SystemExit(f'error: schema not found: {schema_path}')
    if not extension_path.exists():
        raise SystemExit(f'error: tokenizer extension not found: {extension_path}')
    if not input_path.exists():
        raise SystemExit(f'error: input fixture not found: {input_path}')

    rows = load_inputs(input_path)
    out_rows = []
    with tempfile.TemporaryDirectory(prefix='expansion_strict_baseline_') as tmp_dir:
        db_path = Path(tmp_dir) / 'helpers.sqlite3'
        conn = sqlite3.connect(str(db_path))
        try:
            setup_fixture_db(conn, schema_path, extension_path)
            for case in rows:
                row = dict(case)
                row['result'] = evaluate_case(conn, case)
                out_rows.append(row)
        finally:
            conn.close()

    out_path = (root / args.out).resolve()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open('w', encoding='utf-8') as handle:
        for row in out_rows:
            handle.write(json.dumps(row, ensure_ascii=False, separators=(',', ':')))
            handle.write('\n')
    print(f'ok: generated expansion/strict baseline rows={len(out_rows)} out={out_path}')


if __name__ == '__main__':
    main()
