#!/usr/bin/env python3
import ast
import re
from pathlib import Path


RUNTIME_BANNED_NAMES = {
    'parse_top_level_group_boost',
    'parse_whole_scoped_group',
    'levenshtein_distance',
    'long_to_int4',
    'int4_to_long',
    'int_to_byte4',
    'byte4_to_int',
    '_apply_add_ast_clause',
    '_parse_query_ast_sequence',
    '_parse_query_ast_cached',
    '_iter_query_ast_leaves_cached',
}

WRAPPER_ROUTING = {
    'is_escaped_char': 'is_escaped_char_reference',
    'read_quoted_segment': 'read_quoted_segment_reference',
    'has_unescaped_char': 'has_unescaped_char_reference',
    'find_unescaped_char_from': 'find_unescaped_char_from_reference',
    'find_unescaped_char': 'find_unescaped_char_reference',
    'has_unescaped_wildcard': 'has_unescaped_wildcard_reference',
    'unescape_query_escapes': 'unescape_query_escapes_reference',
    'parse_boost_number': 'parse_boost_number_reference',
    'parse_group_boost_suffix': 'parse_group_boost_suffix_reference',
    'parse_field_group_segment': 'parse_field_group_segment_reference',
    'split_query': 'split_query_reference',
    'render_query_tokens': 'render_query_tokens_reference',
    'parse_query_ast': 'parse_query_ast_reference',
    'query_ast_to_debug': 'query_ast_to_debug_reference',
    'query_ast_from_debug': 'query_ast_from_debug_reference',
    'compose_clause_occur': 'compose_clause_occur_reference',
    'iter_query_ast_leaves': 'iter_query_ast_leaves_reference',
    'to_float32': 'to_float32_reference',
    'parse_scoped_token': 'parse_scoped_token_reference',
    'parse_fuzzy_token': 'parse_fuzzy_token_reference',
    'strip_boost': 'strip_boost_reference',
    'quote_match_term': 'quote_match_term_reference',
    'vocab_table_name': 'vocab_table_name_reference',
    'table_exists': 'table_exists_reference',
    'ensure_vocab': 'ensure_vocab_reference',
    'search_options_payload': 'search_options_payload_reference',
    'wildcard_matches_text': 'wildcard_matches_text_reference',
    'normalize_wildcard_pattern': 'normalize_wildcard_pattern_reference',
    'wildcard_has_literal': 'wildcard_has_literal_reference',
    'analyze_phrase_terms_with_positions': 'analyze_phrase_terms_with_positions_reference',
    'normalize_query_text': 'normalize_query_text_reference',
    'normalize_text_for_strict': 'normalize_text_for_strict_reference',
    'literal_needs_strict_check': 'literal_needs_strict_check_reference',
    'load_hit_raw_text': 'load_hit_raw_text_reference',
    'hit_has_analyzed_term': 'hit_has_analyzed_term_reference',
    'hit_matches_clause_expression': 'hit_matches_clause_expression_reference',
    'apply_boost_factor': 'apply_boost_factor_reference',
    'expand_suffix_terms': 'expand_suffix_terms_reference',
    'expand_wildcard_terms': 'expand_wildcard_terms_reference',
    'expand_fuzzy_terms': 'expand_fuzzy_terms_reference',
    'extract_boosted_terms_for_field': 'extract_boosted_terms_for_field_reference',
    'extract_boosted_token_expressions_for_field': 'extract_boosted_token_expressions_for_field_reference',
    'extract_boosted_phrase_expressions_for_field': 'extract_boosted_phrase_expressions_for_field_reference',
    'extract_boosted_group_expressions_for_field': 'extract_boosted_group_expressions_for_field_reference',
    'apply_clause_term_boosts': 'apply_clause_term_boosts_reference',
    'strict_literal_matches': 'strict_literal_matches_reference',
    'literal_presence_matches': 'literal_presence_matches_reference',
    'parse_strict_literals': 'parse_strict_literals_reference',
    'filter_hits_for_strict_modes': 'filter_hits_for_strict_modes_reference',
    'compile_token_expression': 'compile_token_expression_reference',
    'build_execution_plan': 'build_execution_plan_reference',
    'compile_match_expression': 'compile_match_expression_reference',
    'search_field': 'search_field_reference',
    'run_search': 'run_search_reference',
    'run_search_c_backend': 'run_search_c_backend_reference',
    'run_search_backend': 'run_search_backend_reference',
    'parse_options': 'parse_options_reference',
    'build_cli_parser': 'build_cli_parser_reference',
    'main': 'main_reference',
}


def main() -> None:
    query_compat_dir = Path(__file__).resolve().parents[1]
    runtime_path = (query_compat_dir / 'scripts' / 'sqlite_query_compat.py').resolve()
    if not runtime_path.exists():
        raise SystemExit(f'error: runtime module not found: {runtime_path}')

    source = runtime_path.read_text(encoding='utf-8')
    tree = ast.parse(source)
    function_nodes = {
        node.name: node for node in tree.body if isinstance(node, ast.FunctionDef)
    }
    function_names = re.findall(r'^def\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(', source, flags=re.MULTILINE)
    if not function_names:
        raise SystemExit('error: no function definitions found in runtime module')

    python_suffix_names = sorted(name for name in function_names if name.endswith('_python'))
    if python_suffix_names:
        raise SystemExit(
            'error: runtime module contains Python-reference helper definitions '
            f'ending with _python: {python_suffix_names!r}'
        )

    blocked_present = sorted(name for name in function_names if name in RUNTIME_BANNED_NAMES)
    if blocked_present:
        raise SystemExit(
            'error: runtime module contains moved reference-only helpers '
            f'blocked={blocked_present!r}'
        )

    wrapper_failures: list[str] = []
    for wrapper_name, reference_name in WRAPPER_ROUTING.items():
        node = function_nodes.get(wrapper_name)
        if node is None:
            wrapper_failures.append(f'missing wrapper function {wrapper_name}')
            continue
        if len(node.body) != 2:
            wrapper_failures.append(f'{wrapper_name}: expected exactly 2 statements (import + return)')
            continue
        import_stmt, return_stmt = node.body
        if not isinstance(import_stmt, ast.ImportFrom):
            wrapper_failures.append(f'{wrapper_name}: first statement must be ImportFrom')
            continue
        if import_stmt.module != 'python_reference_helpers':
            wrapper_failures.append(f'{wrapper_name}: import must come from python_reference_helpers')
            continue
        imported_names = [alias.name for alias in import_stmt.names]
        if imported_names != [reference_name]:
            wrapper_failures.append(
                f'{wrapper_name}: import must be only {reference_name} (got {imported_names!r})'
            )
            continue
        if not isinstance(return_stmt, ast.Return) or not isinstance(return_stmt.value, ast.Call):
            wrapper_failures.append(f'{wrapper_name}: second statement must be return <call>')
            continue
        call_expr = return_stmt.value
        if not isinstance(call_expr.func, ast.Name) or call_expr.func.id != reference_name:
            wrapper_failures.append(f'{wrapper_name}: return call must target {reference_name}')
            continue
    if wrapper_failures:
        raise SystemExit(
            'error: runtime wrapper routing is not thin import+return '
            f'count={len(wrapper_failures)} failures={wrapper_failures!r}'
        )

    print(
        'ok: runtime_reference_cleanup_guard '
        f'functions={len(function_names)} blocked_checked={len(RUNTIME_BANNED_NAMES)} wrappers_checked={len(WRAPPER_ROUTING)}'
    )


if __name__ == '__main__':
    main()
