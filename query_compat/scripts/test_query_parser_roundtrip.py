#!/usr/bin/env python3
import json
from pathlib import Path

from sqlite_query_compat import SearchCompileError, parse_query_ast, query_ast_to_debug, render_query_tokens, split_query


EXPECTED_INVALID = {
    ('inputs.full.jsonl', 'q583'): 'unclosed quote in query',
    ('inputs.full.jsonl', 'q589'): 'unclosed field-group parentheses in query',
}

EXTRA_ROUNDTRIP_QUERIES = [
    '"قول \\"خاص\\""',
    '"قول \\"خاص\\""^2',
    'title:"باب \\"خاص\\""',
    'title:"باب \\"خاص\\""^2',
    'title:("باب \\"خاص\\"" OR المصور)^2',
    '("طريق \\"العلم\\""~2 OR نافع) AND +title:("باب \\"خاص\\"" OR المصور)',
    'م\\*ور OR منسور\\~1',
    'title\\:المصور OR المصور',
    '\\(المنصور\\) OR المصور',
    'المنصور\\ باحث OR المصور',
    '+title\\:(المنصور OR المصور)',
    '\\[abc\\] OR المصور',
    'المنصور\\^2 OR المنصور^2',
]

INVALID_FIXTURE_FILENAME = 'parser.invalid.extra.jsonl'

AST_SHAPE_CASES = [
    (
        'المنصور AND المصور',
        {
            'clauses': [
                {'occur': 'MUST', 'node': {'kind': 'token', 'value': 'المنصور'}},
                {'occur': 'MUST', 'node': {'kind': 'token', 'value': 'المصور'}},
            ]
        },
    ),
    (
        'المنصور OR (المصور NOT مزور)',
        {
            'clauses': [
                {'occur': 'SHOULD', 'node': {'kind': 'token', 'value': 'المنصور'}},
                {
                    'occur': 'SHOULD',
                    'node': {
                        'kind': 'group',
                        'clauses': [
                            {'occur': 'SHOULD', 'node': {'kind': 'token', 'value': 'المصور'}},
                            {'occur': 'MUST_NOT', 'node': {'kind': 'token', 'value': 'مزور'}},
                        ],
                    },
                },
            ]
        },
    ),
    (
        '+(المنصور OR المصور) AND -مزور',
        {
            'clauses': [
                {
                    'occur': 'MUST',
                    'node': {
                        'kind': 'group',
                        'clauses': [
                            {'occur': 'SHOULD', 'node': {'kind': 'token', 'value': 'المنصور'}},
                            {'occur': 'SHOULD', 'node': {'kind': 'token', 'value': 'المصور'}},
                        ],
                    },
                },
                {'occur': 'MUST_NOT', 'node': {'kind': 'token', 'value': 'مزور'}},
            ]
        },
    ),
]


def main() -> None:
    query_compat_dir = Path(__file__).resolve().parents[1]
    fixture_dir = (query_compat_dir.parent / 'tests' / 'fixtures' / 'queries').resolve()
    if not fixture_dir.exists():
        raise SystemExit(f'error: fixture directory not found: {fixture_dir}')
    invalid_fixture_path = fixture_dir / INVALID_FIXTURE_FILENAME
    if not invalid_fixture_path.exists():
        raise SystemExit(f'error: invalid fixture not found: {invalid_fixture_path}')

    checked = 0
    invalid_seen: dict[tuple[str, str], str] = {}
    for path in sorted(fixture_dir.glob('inputs*.jsonl')):
        for line in path.read_text(encoding='utf-8').splitlines():
            if not line.strip():
                continue
            row = json.loads(line)
            query_id = str(row.get('id', ''))
            query = str(row.get('query', ''))
            key = (path.name, query_id)
            try:
                tokens = split_query(query)
            except SearchCompileError as exc:
                invalid_seen[key] = str(exc)
                continue

            rendered = render_query_tokens(tokens)
            parsed_again = split_query(rendered)
            if parsed_again != tokens:
                raise SystemExit(
                    'error: parser roundtrip token mismatch '
                    f'fixture={path.name} id={query_id} query={query!r} rendered={rendered!r} '
                    f'first={tokens!r} second={parsed_again!r}'
                )
            rendered_again = render_query_tokens(parsed_again)
            if rendered_again != rendered:
                raise SystemExit(
                    'error: parser roundtrip canonical string mismatch '
                    f'fixture={path.name} id={query_id} first={rendered!r} second={rendered_again!r}'
                )
            checked += 1

    expected_invalid = {
        key: value for key, value in EXPECTED_INVALID.items() if (fixture_dir / key[0]).exists()
    }
    if invalid_seen != expected_invalid:
        raise SystemExit(
            f'error: unexpected invalid fixture set (expected={expected_invalid!r}, actual={invalid_seen!r})'
        )

    for index, query in enumerate(EXTRA_ROUNDTRIP_QUERIES, start=1):
        tokens = split_query(query)
        rendered = render_query_tokens(tokens)
        parsed_again = split_query(rendered)
        if parsed_again != tokens:
            raise SystemExit(
                'error: parser escaped-roundtrip token mismatch '
                f'index={index} query={query!r} rendered={rendered!r} '
                f'first={tokens!r} second={parsed_again!r}'
            )

    for index, (query, expected) in enumerate(AST_SHAPE_CASES, start=1):
        actual = query_ast_to_debug(parse_query_ast(query))
        if actual != expected:
            raise SystemExit(
                'error: parser AST shape mismatch '
                f'index={index} query={query!r} expected={expected!r} actual={actual!r}'
            )

    extra_invalid_cases = []
    for line in invalid_fixture_path.read_text(encoding='utf-8').splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        extra_invalid_cases.append(
            (
                str(row.get('id', '')),
                str(row.get('stage', '')),
                str(row.get('query', '')),
                str(row.get('error_contains', '')),
            )
        )

    for index, (case_id, stage, query, expected_error) in enumerate(extra_invalid_cases, start=1):
        if stage not in {'split_query', 'parse_query_ast'}:
            raise SystemExit(
                'error: parser invalid fixture has unsupported stage '
                f'index={index} id={case_id!r} stage={stage!r}'
            )
        parser_fn = split_query if stage == 'split_query' else parse_query_ast
        try:
            parser_fn(query)
        except SearchCompileError as exc:
            if expected_error not in str(exc):
                raise SystemExit(
                    'error: parser extra-invalid mismatch '
                    f'index={index} id={case_id!r} stage={stage!r} query={query!r} '
                    f'expected={expected_error!r} actual={str(exc)!r}'
                )
            continue
        raise SystemExit(
            'error: parser should reject malformed extra-invalid query '
            f'index={index} id={case_id!r} stage={stage!r} query={query!r}'
        )

    print(
        f'ok: query_parser_roundtrip checked={checked} invalid_expected={len(invalid_seen)} '
        f'escaped_roundtrip={len(EXTRA_ROUNDTRIP_QUERIES)} ast_shapes={len(AST_SHAPE_CASES)} '
        f'extra_invalid={len(extra_invalid_cases)}'
    )


if __name__ == '__main__':
    main()
