#!/usr/bin/env python3
import python_reference_helpers as ref
import sqlite_query_compat as qc


def make_raiser(name: str):
    def _raiser(*_args, **_kwargs):
        raise RuntimeError(f'wrapper_routing_probe:{name}')

    return _raiser


def main() -> None:
    cases = [
        ('is_escaped_char', 'is_escaped_char_reference', ('abc', 1), {}),
        ('read_quoted_segment', 'read_quoted_segment_reference', ('"abc"', 1), {}),
        ('has_unescaped_char', 'has_unescaped_char_reference', ('a\\:b', ':'), {}),
        ('find_unescaped_char_from', 'find_unescaped_char_from_reference', ('a\\:b:c', ':', 0), {}),
        ('find_unescaped_char', 'find_unescaped_char_reference', ('a\\:b:c', ':'), {}),
        ('has_unescaped_wildcard', 'has_unescaped_wildcard_reference', ('ab*cd',), {}),
        ('unescape_query_escapes', 'unescape_query_escapes_reference', ('a\\:b\\*c',), {}),
        ('parse_boost_number', 'parse_boost_number_reference', ('1.5',), {}),
        ('parse_group_boost_suffix', 'parse_group_boost_suffix_reference', ('(a)^2', 3), {}),
        ('parse_field_group_segment', 'parse_field_group_segment_reference', ('page:(abc)', 0), {}),
        ('split_query', 'split_query_reference', ('foo AND bar',), {}),
        ('render_query_tokens', 'render_query_tokens_reference', ([('token', 'foo')],), {}),
        ('parse_query_ast', 'parse_query_ast_reference', ('foo',), {}),
        ('query_ast_to_debug', 'query_ast_to_debug_reference', (qc.QueryAst(clauses=tuple()),), {}),
        ('query_ast_from_debug', 'query_ast_from_debug_reference', ({'clauses': []},), {}),
        ('compose_clause_occur', 'compose_clause_occur_reference', ('MUST', 'SHOULD'), {}),
        ('iter_query_ast_leaves', 'iter_query_ast_leaves_reference', ('foo',), {}),
        ('to_float32', 'to_float32_reference', (1.25,), {}),
        ('parse_scoped_token', 'parse_scoped_token_reference', ('page:foo',), {}),
        ('parse_fuzzy_token', 'parse_fuzzy_token_reference', ('foo~1',), {}),
        ('strip_boost', 'strip_boost_reference', ('foo^2',), {}),
        ('quote_match_term', 'quote_match_term_reference', ('foo',), {}),
        ('vocab_table_name', 'vocab_table_name_reference', ('page_fts',), {}),
        ('table_exists', 'table_exists_reference', (object(), 'sqlite_master'), {}),
        ('ensure_vocab', 'ensure_vocab_reference', (object(), 'page_fts'), {}),
        ('search_options_payload', 'search_options_payload_reference', (qc.SearchOptions(),), {}),
        ('wildcard_matches_text', 'wildcard_matches_text_reference', ('abcd', 'a*d'), {}),
        ('normalize_wildcard_pattern', 'normalize_wildcard_pattern_reference', (object(), '*abc?'), {}),
        ('wildcard_has_literal', 'wildcard_has_literal_reference', ('**?a',), {}),
        ('analyze_phrase_terms_with_positions', 'analyze_phrase_terms_with_positions_reference', (object(), 'abc def'), {}),
        ('normalize_query_text', 'normalize_query_text_reference', (object(), 'abc', qc.SearchOptions()), {}),
        ('normalize_text_for_strict', 'normalize_text_for_strict_reference', (object(), 'abc', qc.SearchOptions()), {}),
        ('literal_needs_strict_check', 'literal_needs_strict_check_reference', (object(), 'abc', qc.SearchOptions()), {}),
        ('load_hit_raw_text', 'load_hit_raw_text_reference', (object(), {'field': 'page', 'book_id': 1, 'item_id': 1}), {}),
        ('hit_has_analyzed_term', 'hit_has_analyzed_term_reference', (object(), 'page', 1, 1, 'term'), {}),
        ('hit_matches_clause_expression', 'hit_matches_clause_expression_reference', (object(), 'page', 1, 1, '\"term\"'), {}),
        ('apply_boost_factor', 'apply_boost_factor_reference', ([{'score': 1.0, 'book_id': 1, 'item_id': 1}], 1.1, object()), {}),
        ('expand_suffix_terms', 'expand_suffix_terms_reference', (object(), 'page_fts', 'abc', 10), {}),
        ('expand_wildcard_terms', 'expand_wildcard_terms_reference', (object(), 'page_fts', 'a*c', 10), {}),
        ('expand_fuzzy_terms', 'expand_fuzzy_terms_reference', (object(), 'page_fts', 'abc', 1, 10), {}),
        ('extract_boosted_terms_for_field', 'extract_boosted_terms_for_field_reference', ('abc', 'page', object()), {}),
        (
            'extract_boosted_token_expressions_for_field',
            'extract_boosted_token_expressions_for_field_reference',
            (object(), 'page_fts', 'abc*', 'page', qc.SearchOptions()),
            {},
        ),
        ('extract_boosted_phrase_expressions_for_field', 'extract_boosted_phrase_expressions_for_field_reference', (object(), '"abc def"', 'page'), {}),
        (
            'extract_boosted_group_expressions_for_field',
            'extract_boosted_group_expressions_for_field_reference',
            (object(), 'page_fts', '(abc OR def)^2', 'page', qc.SearchOptions()),
            {},
        ),
        (
            'apply_clause_term_boosts',
            'apply_clause_term_boosts_reference',
            (object(), [{'field': 'page', 'book_id': 1, 'item_id': 1, 'score': 1.0}], 'abc', 'page', object(), 'page_fts', qc.SearchOptions()),
            {},
        ),
        (
            'strict_literal_matches',
            'strict_literal_matches_reference',
            (object(), 'raw text', qc.StrictLiteral(text='abc', is_phrase=False, is_pattern=False, required=False, prohibited=False), qc.SearchOptions()),
            {},
        ),
        (
            'literal_presence_matches',
            'literal_presence_matches_reference',
            (object(), 'raw text', qc.StrictLiteral(text='abc', is_phrase=False, is_pattern=False, required=False, prohibited=False), qc.SearchOptions()),
            {},
        ),
        ('parse_strict_literals', 'parse_strict_literals_reference', (object(), 'abc', 'page'), {}),
        ('filter_hits_for_strict_modes', 'filter_hits_for_strict_modes_reference', (object(), 'abc', 'page', [{'field': 'page', 'book_id': 1, 'item_id': 1, 'score': 1.0}], qc.SearchOptions()), {}),
        ('compile_token_expression', 'compile_token_expression_reference', (object(), 'page_fts', 'page', 'abc*', qc.SearchOptions()), {}),
        ('build_execution_plan', 'build_execution_plan_reference', (object(), 'page_fts', 'page', 'abc', qc.SearchOptions()), {}),
        ('compile_match_expression', 'compile_match_expression_reference', (object(), 'page_fts', 'page', 'abc', qc.SearchOptions()), {}),
        ('search_field', 'search_field_reference', (object(), 'page', 'abc', qc.SearchOptions(), 10, object()), {}),
        ('run_search', 'run_search_reference', (object(), 'abc', 'page', qc.SearchOptions(), 10, None), {}),
        ('run_search_c_backend', 'run_search_c_backend_reference', (object(), 'abc', 'page', qc.SearchOptions(), 10), {}),
        ('run_search_backend', 'run_search_backend_reference', (object(), 'abc', 'page', qc.SearchOptions(), 10, 'c', None), {}),
        ('parse_options', 'parse_options_reference', (object(),), {}),
        ('build_cli_parser', 'build_cli_parser_reference', tuple(), {}),
        ('main', 'main_reference', tuple(), {}),
    ]

    failures: list[str] = []
    for runtime_name, reference_name, args, kwargs in cases:
        runtime_callable = getattr(qc, runtime_name)
        original_reference = getattr(ref, reference_name)
        setattr(ref, reference_name, make_raiser(reference_name))
        try:
            try:
                runtime_callable(*args, **kwargs)
                failures.append(f'{runtime_name}: expected wrapper probe to raise')
            except RuntimeError as exc:
                marker = f'wrapper_routing_probe:{reference_name}'
                if marker not in str(exc):
                    failures.append(f'{runtime_name}: unexpected runtime error {exc!r}')
        finally:
            setattr(ref, reference_name, original_reference)

    if failures:
        raise SystemExit(f'error: runtime reference wrapper routing failures count={len(failures)} details={failures!r}')

    print(f'ok: runtime_reference_wrapper_routing checked={len(cases)}')


if __name__ == '__main__':
    main()
