#!/usr/bin/env python3
import re
import sqlite3

import sqlite_query_compat as qc


def expect_missing_udf_raises(fn, label: str) -> None:
    try:
        fn()
    except qc.SearchCompileError as exc:
        text = str(exc)
        if 'missing UDF' in text or 'no such function' in text:
            return
        if 'execution failed' in text:
            return
        raise SystemExit(f'error: {label} raised unexpected SearchCompileError: {exc!r}') from exc
    except sqlite3.OperationalError as exc:
        if 'no such function' in str(exc):
            return
        raise SystemExit(f'error: {label} raised unexpected sqlite3.OperationalError: {exc!r}') from exc
    raise SystemExit(f'error: expected {label} to raise on missing UDF')


def main() -> None:
    conn = sqlite3.connect(':memory:')
    try:
        simple_query_re = re.compile(r'^[^\s"():*?~^+\-]+$')

        expect_missing_udf_raises(lambda: qc.parse_query_ast_with_udf(conn, 'المنصور OR المصور'), 'parse_query_ast_with_udf')
        expect_missing_udf_raises(
            lambda: qc.iter_query_ast_leaves_with_udf(conn, 'المنصور OR المصور'),
            'iter_query_ast_leaves_with_udf',
        )
        expect_missing_udf_raises(lambda: qc.parse_scoped_token_with_udf(conn, 'page:كتاب'), 'parse_scoped_token_with_udf')
        expect_missing_udf_raises(lambda: qc.parse_fuzzy_token_with_udf(conn, 'كتاب~1'), 'parse_fuzzy_token_with_udf')
        expect_missing_udf_raises(lambda: qc.strip_boost_with_udf(conn, 'كتاب^2'), 'strip_boost_with_udf')
        expect_missing_udf_raises(
            lambda: qc.parse_top_level_group_boost_with_udf(conn, '(المنصور OR المصور)^2'),
            'parse_top_level_group_boost_with_udf',
        )
        expect_missing_udf_raises(
            lambda: qc.parse_whole_scoped_group_with_udf(conn, 'title:(المنصور OR المصور)^2'),
            'parse_whole_scoped_group_with_udf',
        )
        expect_missing_udf_raises(
            lambda: qc.preprocess_rank_boost_with_udf(conn, 'title:(المنصور OR المصور)^2', 'title'),
            'preprocess_rank_boost_with_udf',
        )
        expect_missing_udf_raises(
            lambda: qc.parse_boosted_token_clause_with_udf(conn, 'title:كتاب^2', 'title'),
            'parse_boosted_token_clause_with_udf',
        )
        expect_missing_udf_raises(
            lambda: qc.parse_simple_boolean_with_udf(conn, 'كتاب AND علم', simple_query_re),
            'parse_simple_boolean_with_udf',
        )
        expect_missing_udf_raises(
            lambda: qc.parse_simple_phrase_query_with_udf(conn, '"طريق العلم"~2'),
            'parse_simple_phrase_query_with_udf',
        )
        expect_missing_udf_raises(
            lambda: qc.parse_single_phrase_clause_with_udf(conn, 'title:"طريق العلم"~2^1.5'),
            'parse_single_phrase_clause_with_udf',
        )
        expect_missing_udf_raises(
            lambda: qc.parse_phrase_term_boolean_with_udf(conn, '"طريق العلم" AND باب'),
            'parse_phrase_term_boolean_with_udf',
        )
        expect_missing_udf_raises(
            lambda: qc.extract_boosted_phrase_spans_with_udf(conn, 'title:"طريق العلم"^2'),
            'extract_boosted_phrase_spans_with_udf',
        )
        expect_missing_udf_raises(
            lambda: qc.extract_boosted_group_spans_with_udf(conn, 'باب AND ("طريق العلم" OR شرح)^2', 'page'),
            'extract_boosted_group_spans_with_udf',
        )
        expect_missing_udf_raises(lambda: qc.lucene_idf_with_udf(conn, 100, 7), 'lucene_idf_with_udf')
        expect_missing_udf_raises(
            lambda: qc.lucene_term_score_with_udf(conn, 1.5, 3.0, 200, 1.2, 0.75, 100.0),
            'lucene_term_score_with_udf',
        )
        expect_missing_udf_raises(lambda: qc.levenshtein_distance_with_udf(conn, 'كتاب', 'كتيب', 2), 'levenshtein_distance_with_udf')
        expect_missing_udf_raises(lambda: qc.f32_mul_with_udf(conn, 1.5, 2.5), 'f32_mul_with_udf')
        expect_missing_udf_raises(lambda: qc.f32_add_with_udf(conn, 1.5, 2.5), 'f32_add_with_udf')
        expect_missing_udf_raises(lambda: qc.wildcard_matches_text_with_udf(conn, 'الكتاب', 'الكت*'), 'wildcard_matches_text_with_udf')
    finally:
        conn.close()

    print('ok: udf_missing_strict_policy')


if __name__ == '__main__':
    main()
