#!/usr/bin/env python3
import re
import sqlite3
from pathlib import Path

from python_reference_helpers import (
    extract_boosted_group_spans_python,
    extract_boosted_phrase_spans_python,
    f32_add_python,
    f32_mul_python,
    lucene_idf_python,
    lucene_term_score_python,
    parse_boosted_token_clause_python,
    parse_phrase_term_boolean_python,
    parse_simple_boolean_python,
    parse_simple_phrase_query_python,
    parse_single_phrase_clause_python,
    parse_top_level_group_boost,
    parse_whole_scoped_group,
)
from sqlite_query_compat import (
    preprocess_rank_boost_c_backend,
    extract_boosted_group_spans_c_backend,
    extract_boosted_phrase_spans_c_backend,
    parse_top_level_group_boost_c_backend,
    parse_whole_scoped_group_c_backend,
    parse_boost_factor_for_field,
    strip_boost_for_ranking,
    parse_fuzzy_token,
    parse_fuzzy_token_c_backend,
    SearchCompileError,
    parse_scoped_token,
    parse_scoped_token_c_backend,
    strip_boost,
    strip_boost_c_backend,
    parse_boosted_token_clause_c_backend,
    parse_boosted_token_clause_with_udf,
    parse_simple_boolean_with_udf,
    parse_simple_phrase_query_with_udf,
    parse_single_phrase_clause_with_udf,
    parse_single_phrase_clause_c_backend,
    parse_phrase_term_boolean_c_backend,
    parse_phrase_term_boolean_with_udf,
)


def main() -> None:
    query_compat_dir = Path(__file__).resolve().parents[1]
    extension_path = (query_compat_dir.parent / 'tokenizer' / 'build' / 'sqlite_tokenizer_ar.so').resolve()
    if not extension_path.exists():
        raise SystemExit(f'error: tokenizer extension not found: {extension_path}')

    conn = sqlite3.connect(':memory:')
    try:
        conn.enable_load_extension(True)
        conn.load_extension(str(extension_path))

        scoped_cases = [
            'كتاب',
            'page:كتاب',
            'PAGE:Kitab',
            'title:"باب"',
            'foo:bar',
            'page:',
            'page\\:كتاب',
            'page:كتاب\\:باب',
        ]
        for token in scoped_cases:
            expected = parse_scoped_token(token)
            actual = parse_scoped_token_c_backend(conn, token)
            if actual != expected:
                raise SystemExit(
                    'error: parse_scoped_token C helper mismatch '
                    f'token={token!r} expected={expected!r} actual={actual!r}'
                )

        boost_cases = [
            'كتاب',
            'كتاب^2',
            'كتاب^2.5',
            'page:كتاب^3',
            '"طريق"^4',
            'كتاب\\^2',
            'كتاب^x',
        ]
        for token in boost_cases:
            expected = strip_boost(token)
            actual = strip_boost_c_backend(conn, token)
            if actual != expected:
                raise SystemExit(
                    'error: strip_boost C helper mismatch '
                    f'token={token!r} expected={expected!r} actual={actual!r}'
                )

        invalid_cases = ['^2', '^2.5']
        for token in invalid_cases:
            py_failed = False
            c_failed = False
            try:
                strip_boost(token)
            except SearchCompileError:
                py_failed = True
            try:
                strip_boost_c_backend(conn, token)
            except SearchCompileError:
                c_failed = True
            if (not py_failed) or (not c_failed):
                raise SystemExit(
                    'error: strip_boost invalid-token parity mismatch '
                    f'token={token!r} python_failed={py_failed} c_failed={c_failed}'
                )

        boosted_token_clause_cases = [
            ('كتاب^2', 'page'),
            ('كتاب^2', 'both'),
            ('page:كتاب^2', 'page'),
            ('page:كتاب^2', 'both'),
            ('page:كتاب^2', 'title'),
            ('title:كتاب^2', 'title'),
            ('title:كتاب^2', 'both'),
            ('title:كتاب^2', 'page'),
            ('كتاب*^3', 'page'),
            ('كتاب*^3', 'both'),
            ('كتاب~1^4', 'title'),
            ('كتاب~1^4', 'both'),
            ('كتاب^1', 'page'),
            ('كتاب^x', 'page'),
            ('^2', 'page'),
        ]
        for token, runtime_field in boosted_token_clause_cases:
            py_failed = False
            c_failed = False
            try:
                expected = parse_boosted_token_clause_python(token, runtime_field)
            except SearchCompileError:
                py_failed = True
                expected = None
            try:
                actual_direct = parse_boosted_token_clause_c_backend(conn, token, runtime_field)
            except SearchCompileError:
                c_failed = True
                actual_direct = None
            if py_failed != c_failed:
                raise SystemExit(
                    'error: parse_boosted_token_clause error parity mismatch '
                    f'token={token!r} runtime_field={runtime_field!r} python_failed={py_failed} c_failed={c_failed}'
                )
            if (not py_failed) and actual_direct != expected:
                raise SystemExit(
                    'error: parse_boosted_token_clause C helper mismatch '
                    f'token={token!r} runtime_field={runtime_field!r} expected={expected!r} actual={actual_direct!r}'
                )
            wrapped_failed = False
            try:
                wrapped = parse_boosted_token_clause_with_udf(conn, token, runtime_field)
            except SearchCompileError:
                wrapped_failed = True
                wrapped = None
            if c_failed != wrapped_failed:
                raise SystemExit(
                    'error: parse_boosted_token_clause wrapper error parity mismatch '
                    f'token={token!r} runtime_field={runtime_field!r} c_failed={c_failed} wrapped_failed={wrapped_failed}'
                )
            if (not c_failed) and wrapped != expected:
                raise SystemExit(
                    'error: parse_boosted_token_clause UDF wrapper mismatch '
                    f'token={token!r} runtime_field={runtime_field!r} expected={expected!r} actual={wrapped!r}'
                )

        fuzzy_cases = [
            'كتاب',
            'كتاب~',
            'كتاب~1',
            'كتاب~2',
            'كتاب~3',
            'كتاب\\~2',
            'كتاب~x',
            '~2',
        ]
        for token in fuzzy_cases:
            expected = parse_fuzzy_token(token)
            actual = parse_fuzzy_token_c_backend(conn, token)
            if actual != expected:
                raise SystemExit(
                    'error: parse_fuzzy_token C helper mismatch '
                    f'token={token!r} expected={expected!r} actual={actual!r}'
                )

        top_group_cases = [
            '(المنصور OR المصور)^2',
            '("طريق العلم" OR "طريق الادب")^1.5',
            'title:(المنصور OR المصور)^2',
            '(المنصور OR المصور) AND باب',
            '(المنصور OR المصور)',
            '((المنصور OR المصور)^2 AND باب)^3',
            '("\\\"طريق العلم\\\"")^4',
        ]
        for query in top_group_cases:
            expected = parse_top_level_group_boost(query)
            actual = parse_top_level_group_boost_c_backend(conn, query)
            if actual != expected:
                raise SystemExit(
                    'error: parse_top_level_group_boost C helper mismatch '
                    f'query={query!r} expected={expected!r} actual={actual!r}'
                )

        whole_scoped_group_cases = [
            'title:(المنصور OR المصور)',
            'title:(المنصور OR المصور)^2',
            'page:("طريق العلم" OR "طريق الادب")',
            'title:(المنصور OR المصور) AND باب',
            '+title:(المنصور OR المصور)',
            'foo:(المنصور OR المصور)',
        ]
        for query in whole_scoped_group_cases:
            expected = parse_whole_scoped_group(query)
            actual = parse_whole_scoped_group_c_backend(conn, query)
            if actual != expected:
                raise SystemExit(
                    'error: parse_whole_scoped_group C helper mismatch '
                    f'query={query!r} expected={expected!r} actual={actual!r}'
                )

        preprocess_cases = [
            ('title:(المنصور OR المصور)^2', 'title'),
            ('title:(المنصور OR المصور)^2', 'page'),
            ('(title:(المنصور^3 OR المصور))^2', 'title'),
            ('(title:(المنصور^3 OR المصور))^2', 'page'),
            ('(المنصور OR المصور)^2', 'page'),
            ('(المنصور OR المصور)^2', 'title'),
            ('المنصور^2', 'page'),
            ('title:المنصور^2', 'title'),
            ('title:"طريق العلم"^2.5', 'title'),
            ('title:"طريق العلم"^2.5', 'page'),
            ('"طريق العلم"^1.5', 'page'),
            ('page:عنوان^3', 'page'),
            ('page:عنوان^3', 'title'),
        ]
        for query, runtime_field in preprocess_cases:
            actual = preprocess_rank_boost_c_backend(conn, query, runtime_field)
            if actual is None:
                raise SystemExit(
                    'error: preprocess_rank_boost unexpectedly returned None '
                    f'for covered case query={query!r} runtime_field={runtime_field!r}'
                )
            expected = (
                parse_boost_factor_for_field(conn, query, runtime_field),
                strip_boost_for_ranking(conn, query, runtime_field),
            )
            if actual != expected:
                raise SystemExit(
                    'error: preprocess_rank_boost C helper mismatch '
                    f'query={query!r} runtime_field={runtime_field!r} expected={expected!r} actual={actual!r}'
                )

        boosted_group_span_cases = [
            ('باب AND ("هذا كتاب" OR فصل)^2', 'page'),
            ('باب AND ("هذا كتاب" OR فصل)^2', 'title'),
            ('title:("هذا كتاب" OR فصل)^2 AND باب', 'title'),
            ('title:("هذا كتاب" OR فصل)^2 AND باب', 'page'),
            ('("هذا كتاب" OR فصل)^2', 'title'),
            ('title:(("هذا كتاب" OR فصل)^2 OR باب) AND شرح', 'title'),
        ]
        for query, runtime_field in boosted_group_span_cases:
            actual = extract_boosted_group_spans_c_backend(conn, query, runtime_field)
            if actual is None:
                continue
            expected = extract_boosted_group_spans_python(query, runtime_field)
            if actual != expected:
                raise SystemExit(
                    'error: extract_boosted_group_spans C helper mismatch '
                    f'query={query!r} runtime_field={runtime_field!r} expected={expected!r} actual={actual!r}'
                )

        boosted_phrase_span_cases = [
            '"طريق العلم"^2',
            'page:"طريق العلم"~3^2.5 OR "باب"^1.3',
            'title:"\\\"طريق\\\" العلم"^4',
            'title:("طريق العلم"^2 OR "شرح متن"^3)',
            '"طريق" OR "علم"',
            '"طريق\\^العلم"^2',
        ]
        for query in boosted_phrase_span_cases:
            actual = extract_boosted_phrase_spans_c_backend(conn, query)
            if actual is None:
                continue
            expected = extract_boosted_phrase_spans_python(query)
            if actual != expected:
                raise SystemExit(
                    'error: extract_boosted_phrase_spans C helper mismatch '
                    f'query={query!r} expected={expected!r} actual={actual!r}'
                )

        f32_cases = [
            (0.0, 0.0),
            (1.0, 2.0),
            (1.5, 3.25),
            (-2.0, 4.0),
            (12345.6789, 0.125),
        ]
        for left, right in f32_cases:
            mul_row = conn.execute('SELECT sqlite_tokenizer_ar_f32_mul(?, ?)', (left, right)).fetchone()
            add_row = conn.execute('SELECT sqlite_tokenizer_ar_f32_add(?, ?)', (left, right)).fetchone()
            if mul_row is None or mul_row[0] is None or add_row is None or add_row[0] is None:
                raise SystemExit('error: f32 helper UDF returned NULL unexpectedly')
            actual_mul = float(mul_row[0])
            actual_add = float(add_row[0])
            expected_mul = float(f32_mul_python(left, right))
            expected_add = float(f32_add_python(left, right))
            if actual_mul != expected_mul:
                raise SystemExit(
                    'error: f32_mul UDF mismatch '
                    f'left={left!r} right={right!r} expected={expected_mul!r} actual={actual_mul!r}'
                )
            if actual_add != expected_add:
                raise SystemExit(
                    'error: f32_add UDF mismatch '
                    f'left={left!r} right={right!r} expected={expected_add!r} actual={actual_add!r}'
                )

        score_cases = [
            (1.0, 1.0, 30, 1.2, 0.75, 120.0),
            (2.5, 3.0, 220, 1.2, 0.75, 95.0),
            (0.7, 2.0, 0, 1.2, 0.75, 70.0),
            (1.8, 5.0, 800, 1.2, 0.75, 180.0),
        ]
        for weight, tf, doc_len, k1, b, avgdl in score_cases:
            row = conn.execute(
                'SELECT sqlite_tokenizer_ar_lucene_term_score(?, ?, ?, ?, ?, ?)',
                (weight, tf, doc_len, k1, b, avgdl),
            ).fetchone()
            if row is None or row[0] is None:
                raise SystemExit('error: lucene_term_score UDF returned NULL unexpectedly')
            actual = float(row[0])
            expected = float(lucene_term_score_python(weight, tf, doc_len, k1, b, avgdl))
            if actual != expected:
                raise SystemExit(
                    'error: lucene_term_score UDF mismatch '
                    f'weight={weight!r} tf={tf!r} doc_len={doc_len!r} expected={expected!r} actual={actual!r}'
                )

        idf_cases = [
            (1, 1),
            (8, 1),
            (8, 3),
            (8, 32),
            (64, 7),
        ]
        for doc_count, doc_freq in idf_cases:
            row = conn.execute(
                'SELECT sqlite_tokenizer_ar_lucene_idf(?, ?)',
                (doc_count, doc_freq),
            ).fetchone()
            if row is None or row[0] is None:
                raise SystemExit('error: lucene_idf UDF returned NULL unexpectedly')
            actual = float(row[0])
            expected = float(lucene_idf_python(doc_count, doc_freq))
            if actual != expected:
                raise SystemExit(
                    'error: lucene_idf UDF mismatch '
                    f'doc_count={doc_count!r} doc_freq={doc_freq!r} expected={expected!r} actual={actual!r}'
                )

        simple_query_re = re.compile(r'^[^\s"():*?~^+\-]+$')
        simple_boolean_cases = [
            'كتاب AND علم',
            'كتاب^2 AND علم',
            'كتاب AND علم^3.5',
            'كتاب^2 OR علم^3',
            'كتاب OR علم',
            'كتاب OR',
            '"كتاب" AND علم',
            'page:كتاب AND علم',
            'كتاب AND علم~1',
            'كتاب -علم AND باب',
        ]
        for query in simple_boolean_cases:
            expected = parse_simple_boolean_python(query, simple_query_re)
            actual = parse_simple_boolean_with_udf(conn, query, simple_query_re)
            if actual != expected:
                raise SystemExit(
                    'error: parse_simple_boolean UDF mismatch '
                    f'query={query!r} expected={expected!r} actual={actual!r}'
                )

        phrase_query_cases = [
            '"طريق العلم"',
            ' "طريق العلم" ',
            '"طريق \\"العلم\\""',
            '"طريق العلم"~3',
            '"طريق العلم"~0005',
            '"طريق العلم"~',
            '"طريق العلم"~2.5',
            'title:"طريق العلم"',
            '"طريق العلم',
        ]
        for query in phrase_query_cases:
            expected = parse_simple_phrase_query_python(query)
            actual = parse_simple_phrase_query_with_udf(conn, query)
            if actual != expected:
                raise SystemExit(
                    'error: parse_simple_phrase_query UDF mismatch '
                    f'query={query!r} expected={expected!r} actual={actual!r}'
                )

        single_phrase_clause_cases = [
            '"طريق العلم"',
            '"طريق العلم"~3',
            '"طريق العلم"^2',
            '"طريق العلم"~3^2.5',
            'title:"طريق العلم"',
            'page:"طريق العلم"~2^1.5',
            'title:"طريق العلم" OR باب',
            'باب',
        ]
        for query in single_phrase_clause_cases:
            expected = parse_single_phrase_clause_python(query)
            actual_direct = parse_single_phrase_clause_c_backend(conn, query)
            actual_wrapped = parse_single_phrase_clause_with_udf(conn, query)
            if actual_direct != expected:
                raise SystemExit(
                    'error: parse_single_phrase_clause C helper mismatch '
                    f'query={query!r} expected={expected!r} actual={actual_direct!r}'
                )
            if actual_wrapped != expected:
                raise SystemExit(
                    'error: parse_single_phrase_clause UDF wrapper mismatch '
                    f'query={query!r} expected={expected!r} actual={actual_wrapped!r}'
                )

        phrase_term_boolean_cases = [
            '"طريق العلم" AND "باب الفقه"',
            '"طريق العلم"~3 OR title:"باب الفقه"^2',
            'page:"طريق العلم"^1.5 AND عنوان^2',
            'title:باب^2 OR "كتاب العلم"~4^1.25',
            'باب AND علم',
            '"طريق العلم" AND',
            'page:"طريق العلم"~2.5 AND باب',
        ]
        for query in phrase_term_boolean_cases:
            expected_python = parse_phrase_term_boolean_python(query)
            actual_c = parse_phrase_term_boolean_c_backend(conn, query)
            if actual_c != expected_python:
                raise SystemExit(
                    'error: parse_phrase_term_boolean C helper mismatch '
                    f'query={query!r} expected={expected_python!r} actual={actual_c!r}'
                )
            expected_fallback = parse_phrase_term_boolean_python(query)
            actual_with_udf = parse_phrase_term_boolean_with_udf(conn, query)
            if actual_with_udf != expected_fallback:
                raise SystemExit(
                    'error: parse_phrase_term_boolean UDF wrapper mismatch '
                    f'query={query!r} expected={expected_fallback!r} actual={actual_with_udf!r}'
                )
    finally:
        conn.close()

    print('ok: token_helpers_udf')


if __name__ == '__main__':
    main()
