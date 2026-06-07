#!/usr/bin/env python3
import argparse
from functools import lru_cache
import json
import math
from pathlib import Path
import re
import sqlite3
import struct

from sqlite_query_compat import (
    BOOL_OPS,
    BOOST_NUMBER_RE,
    BOOSTED_PHRASE_RE,
    EMPTY_TERM_SENTINEL,
    QUOTED_QUERY_CONTENT_RE,
    QueryAst,
    QueryAstClause,
    QueryAstGroup,
    QueryAstLeaf,
    SearchOptions,
    SearchCompileError,
    StrictLiteral,
    analyze_positions_with_udf,
    f32_mul_with_udf,
    levenshtein_distance_with_udf,
    normalize_text_with_udf,
    iter_query_ast_leaves_with_udf,
    parse_boosted_token_clause_with_udf,
    compile_token_expression,
    extract_boosted_phrase_spans_with_udf,
    parse_field_group_segment,
    is_escaped_char,
    extract_boosted_group_spans_with_udf,
    compile_match_expression,
    wildcard_matches_text_with_udf,
    parse_fuzzy_token_with_udf,
    parse_scoped_token_with_udf,
    strip_boost_with_udf,
    has_unescaped_wildcard,
    analyze_text_with_udf,
    compose_clause_occur,
    SqliteExecutionPlan,
    parse_query_ast_with_udf,
    parse_boost_factor_for_field,
    strip_boost_for_ranking,
    LuceneRanker,
)


def to_float32_reference(value: float) -> float:
    return struct.unpack('f', struct.pack('f', float(value)))[0]


def quote_match_term_reference(value: str) -> str:
    if value == '':
        return '"__empty__"'
    return '"' + value.replace('"', '""') + '"'


def vocab_table_name_reference(fts_table: str) -> str:
    if fts_table == 'page_fts':
        return 'qcv_page_vocab'
    if fts_table == 'title_fts':
        return 'qcv_title_vocab'
    raise SearchCompileError(f'unsupported FTS table: {fts_table}')


def table_exists_reference(conn: sqlite3.Connection, table_name: str) -> bool:
    row = conn.execute(
        "SELECT 1 FROM sqlite_master WHERE type IN ('table', 'view') AND name = ? LIMIT 1",
        (table_name,),
    ).fetchone()
    return row is not None


def ensure_vocab_reference(conn: sqlite3.Connection, fts_table: str) -> str:
    vocab = vocab_table_name_reference(fts_table)
    conn.execute(f"CREATE VIRTUAL TABLE IF NOT EXISTS {vocab} USING fts5vocab({fts_table}, 'row')")
    return vocab


def search_options_payload_reference(options: SearchOptions) -> dict:
    return {
        'allow_prefix_search': options.allow_prefix_search,
        'allow_suffix_search': options.allow_suffix_search,
        'allow_wildcard_search': options.allow_wildcard_search,
        'ignore_diacritics': options.ignore_diacritics,
        'ignore_hamza_forms': options.ignore_hamza_forms,
        'ignore_letter_forms': options.ignore_letter_forms,
        'ignore_digit_forms': options.ignore_digit_forms,
        'suffix_max_expansions': options.suffix_max_expansions,
        'wildcard_max_expansions': options.wildcard_max_expansions,
        'allow_fuzzy_search': options.allow_fuzzy_search,
        'fuzzy_max_expansions': options.fuzzy_max_expansions,
        'lenient_parse_errors': options.lenient_parse_errors,
    }


def wildcard_matches_text_reference(normalized_text: str, normalized_pattern: str) -> bool:
    if normalized_pattern == '':
        return True
    if ('*' not in normalized_pattern) and ('?' not in normalized_pattern):
        return normalized_pattern in normalized_text
    pattern_re = re.escape(normalized_pattern).replace(r'\*', '.*').replace(r'\?', '.')
    return re.search(pattern_re, normalized_text) is not None


def normalize_wildcard_pattern_reference(conn: sqlite3.Connection, pattern: str) -> str:
    relaxed = SearchOptions(ignore_diacritics=True, ignore_hamza_forms=True, ignore_letter_forms=True, ignore_digit_forms=True)
    return normalize_text_with_udf(conn, pattern, relaxed, lowercase=False, keep_wildcards=True, trim=True)


def wildcard_has_literal_reference(pattern: str) -> bool:
    for ch in pattern:
        if ch not in {'*', '?'}:
            return True
    return False


def analyze_phrase_terms_with_positions_reference(conn: sqlite3.Connection, phrase_text: str) -> tuple[list[str], list[int]]:
    analyzed = analyze_positions_with_udf(conn, phrase_text)
    if not analyzed:
        return [], []
    first_position = analyzed[0][1]
    terms: list[str] = []
    rel_positions: list[int] = []
    for term, position in analyzed:
        terms.append(str(term))
        rel_positions.append(max(0, int(position) - int(first_position)))
    return terms, rel_positions


def normalize_query_text_reference(conn: sqlite3.Connection, value: str, options: SearchOptions) -> str:
    relaxed = SearchOptions(ignore_diacritics=True, ignore_hamza_forms=True, ignore_letter_forms=True, ignore_digit_forms=True)
    return normalize_text_with_udf(conn, value, relaxed, lowercase=False, keep_wildcards=False, trim=True)


def normalize_text_for_strict_reference(conn: sqlite3.Connection, value: str, options: SearchOptions) -> str:
    return normalize_text_with_udf(conn, value, options, lowercase=True, keep_wildcards=False, trim=False)


def literal_needs_strict_check_reference(conn: sqlite3.Connection, text: str, options: SearchOptions) -> bool:
    if options.ignore_diacritics and options.ignore_hamza_forms and bool(options.ignore_letter_forms) and options.ignore_digit_forms:
        return False
    row = conn.execute(
        'SELECT sqlite_tokenizer_ar_has_sensitive_forms(?, ?, ?, ?, ?)',
        (
            text,
            1 if not options.ignore_diacritics else 0,
            1 if not options.ignore_hamza_forms else 0,
            1 if not bool(options.ignore_letter_forms) else 0,
            1 if not options.ignore_digit_forms else 0,
        ),
    ).fetchone()
    return bool(row and int(row[0]) != 0)


def load_hit_raw_text_reference(conn: sqlite3.Connection, hit: dict) -> str:
    field = str(hit['field'])
    book_id = int(hit['book_id'])
    item_id = int(hit['item_id'])
    if field == 'page':
        row = conn.execute(
            """
            SELECT s.body
            FROM page_content_store s
            JOIN page_doc_map m ON m.rowid = s.rowid
            WHERE m.book_id = ? AND m.page_id = ?
            LIMIT 1
            """,
            (book_id, item_id),
        ).fetchone()
        return '' if row is None else str(row[0] or '')
    if field == 'title':
        row = conn.execute(
            """
            SELECT s.title
            FROM title_content_store s
            JOIN title_doc_map m ON m.rowid = s.rowid
            WHERE m.book_id = ? AND m.title_id = ?
            LIMIT 1
            """,
            (book_id, item_id),
        ).fetchone()
        return '' if row is None else str(row[0] or '')
    return ''


def hit_has_analyzed_term_reference(conn: sqlite3.Connection, field: str, book_id: int, item_id: int, term: str) -> bool:
    if field == 'page':
        row = conn.execute(
            """
            SELECT 1
            FROM qcv_page_vocab_inst v
            JOIN page_doc_map m ON m.rowid = v.doc
            WHERE m.book_id = ? AND m.page_id = ? AND v.term = ?
            LIMIT 1
            """,
            (book_id, item_id, term),
        ).fetchone()
        return row is not None
    if field == 'title':
        row = conn.execute(
            """
            SELECT 1
            FROM qcv_title_vocab_inst v
            JOIN title_doc_map m ON m.rowid = v.doc
            WHERE m.book_id = ? AND m.title_id = ? AND v.term = ?
            LIMIT 1
            """,
            (book_id, item_id, term),
        ).fetchone()
        return row is not None
    return False


def hit_matches_clause_expression_reference(
    conn: sqlite3.Connection,
    field: str,
    book_id: int,
    item_id: int,
    expression: str,
) -> bool:
    if field == 'page':
        row = conn.execute(
            """
            SELECT 1
            FROM page_fts
            JOIN page_doc_map m ON m.rowid = page_fts.rowid
            WHERE m.book_id = ? AND m.page_id = ? AND page_fts MATCH ?
            LIMIT 1
            """,
            (book_id, item_id, expression),
        ).fetchone()
        return row is not None
    if field == 'title':
        row = conn.execute(
            """
            SELECT 1
            FROM title_fts
            JOIN title_doc_map m ON m.rowid = title_fts.rowid
            WHERE m.book_id = ? AND m.title_id = ? AND title_fts MATCH ?
            LIMIT 1
            """,
            (book_id, item_id, expression),
        ).fetchone()
        return row is not None
    return False


def apply_boost_factor_reference(hits: list[dict], factor: float, conn: sqlite3.Connection) -> list[dict]:
    if abs(factor - 1.0) < 1e-12:
        return hits
    factor_f32 = to_float32_reference(factor)
    boosted: list[dict] = []
    for hit in hits:
        row = dict(hit)
        row['score'] = float(f32_mul_with_udf(conn, float(row['score']), factor_f32))
        boosted.append(row)
    boosted.sort(key=lambda row: (row['score'], row['book_id'], row['item_id']))
    return boosted


def extract_boosted_terms_for_field_reference(query: str, runtime_field: str, ranker: 'LuceneRanker') -> list[tuple[str, float]]:
    boosted_terms: list[tuple[str, float]] = []
    for node, occur in iter_query_ast_leaves_with_udf(ranker.conn, query):
        kind = node.kind
        value = node.value
        if kind in {'field_group', 'field_group_boost'}:
            if kind == 'field_group':
                scope, inner_query = value.split('\t', 1)
            else:
                scope, _group_boost, inner_query = value.split('\t', 2)
            if scope == runtime_field:
                boosted_terms.extend(extract_boosted_terms_for_field_reference(inner_query, runtime_field, ranker))
            continue
        if kind != 'token':
            continue
        if occur == 'MUST_NOT':
            continue
        raw_token = value
        upper = raw_token.upper()
        if upper in BOOL_OPS or raw_token in {'(', ')'}:
            continue
        boosted_clause = parse_boosted_token_clause_with_udf(ranker.conn, raw_token, runtime_field)
        if boosted_clause is None:
            continue
        raw = str(boosted_clause['raw'])
        boost_value = float(boosted_clause['boost'])
        if raw == '' or bool(boosted_clause['has_wildcard']) or bool(boosted_clause['is_fuzzy']):
            continue
        analyzed = ranker.analyze_text(raw)
        if len(analyzed) != 1:
            continue
        term = analyzed[0]
        if term == '' or term == EMPTY_TERM_SENTINEL:
            continue
        boosted_terms.append((term, boost_value))
    return boosted_terms


def extract_boosted_token_expressions_for_field_reference(
    conn: sqlite3.Connection,
    fts_table: str,
    query: str,
    runtime_field: str,
    options: SearchOptions,
) -> list[tuple[str, float]]:
    boosted_expressions: list[tuple[str, float]] = []
    for node, occur in iter_query_ast_leaves_with_udf(conn, query):
        kind = node.kind
        value = node.value
        if kind in {'field_group', 'field_group_boost'}:
            if kind == 'field_group':
                scope, inner_query = value.split('\t', 1)
            else:
                scope, _group_boost, inner_query = value.split('\t', 2)
            if scope == runtime_field:
                boosted_expressions.extend(
                    extract_boosted_token_expressions_for_field_reference(conn, fts_table, inner_query, runtime_field, options)
                )
            continue
        if kind != 'token':
            continue
        if occur == 'MUST_NOT':
            continue
        raw_token = value
        upper = raw_token.upper()
        if upper in BOOL_OPS or raw_token in {'(', ')'}:
            continue
        boosted_clause = parse_boosted_token_clause_with_udf(conn, raw_token, runtime_field)
        if boosted_clause is None:
            continue
        scope_raw = boosted_clause['scope']
        scope = None if scope_raw is None else str(scope_raw).lower()
        raw = str(boosted_clause['raw'])
        boost_value = float(boosted_clause['boost'])
        if raw == '':
            continue
        if (not bool(boosted_clause['has_wildcard'])) and (not bool(boosted_clause['is_fuzzy'])):
            continue
        token_for_compile = raw if scope is None else f'{scope}:{raw}'
        expression = compile_token_expression(conn, fts_table, runtime_field, token_for_compile, options)
        if expression is None:
            continue
        boosted_expressions.append((expression, boost_value))
    return boosted_expressions


def extract_boosted_phrase_expressions_for_field_reference(
    conn: sqlite3.Connection,
    query: str,
    runtime_field: str,
) -> list[tuple[str, float]]:
    def split_field_group_segments(raw_query: str) -> tuple[str, list[str]]:
        out_chars: list[str] = []
        scoped_inners: list[str] = []
        index = 0
        in_quote = False
        while index < len(raw_query):
            ch = raw_query[index]
            if ch == '"' and (not is_escaped_char(raw_query, index)):
                in_quote = not in_quote
                out_chars.append(ch)
                index += 1
                continue
            if in_quote:
                out_chars.append(ch)
                index += 1
                continue

            if ch in {'+', '-'} and index + 1 < len(raw_query):
                parsed_prefixed = parse_field_group_segment(raw_query, index + 1)
                if parsed_prefixed is not None:
                    scope, inner_query, _boost, next_index = parsed_prefixed
                    if scope == runtime_field:
                        scoped_inners.append(inner_query)
                    out_chars.append(' ' * (next_index - index))
                    index = next_index
                    continue

            parsed = parse_field_group_segment(raw_query, index)
            if parsed is not None:
                scope, inner_query, _boost, next_index = parsed
                if scope == runtime_field:
                    scoped_inners.append(inner_query)
                out_chars.append(' ' * (next_index - index))
                index = next_index
                continue

            out_chars.append(ch)
            index += 1
        return ''.join(out_chars), scoped_inners

    out: list[tuple[str, float]] = []
    local_query, scoped_inners = split_field_group_segments(query)
    for scope_raw, phrase_value, explicit_slop, boost in extract_boosted_phrase_spans_with_udf(conn, local_query):
        if scope_raw is not None and scope_raw != runtime_field:
            continue
        terms, rel_positions = analyze_phrase_terms_with_positions_reference(conn, phrase_value)
        if len(terms) == 0:
            out.append((quote_match_term_reference('__nohit__'), boost))
            continue
        if len(terms) == 1:
            out.append((quote_match_term_reference(terms[0]), boost))
            continue
        implicit_slop = max(0, rel_positions[-1] - (len(terms) - 1))
        effective_slop = explicit_slop + implicit_slop
        if effective_slop <= 0:
            out.append((quote_match_term_reference(' '.join(terms)), boost))
            continue
        out.append((f"NEAR({' '.join(quote_match_term_reference(term) for term in terms)}, {effective_slop})", boost))
    for inner_query in scoped_inners:
        out.extend(extract_boosted_phrase_expressions_for_field_reference(conn, inner_query, runtime_field))
    return out


def extract_boosted_group_expressions_for_field_reference(
    conn: sqlite3.Connection,
    fts_table: str,
    query: str,
    runtime_field: str,
    options: SearchOptions,
) -> list[tuple[str, float]]:
    out: list[tuple[str, float]] = []
    for inner_query, boost in extract_boosted_group_spans_with_udf(conn, query, runtime_field):
        expression = compile_match_expression(conn, fts_table, runtime_field, inner_query, options)
        out.append((expression, boost))
    return out


def apply_clause_term_boosts_reference(
    conn: sqlite3.Connection,
    hits: list[dict],
    query: str,
    runtime_field: str,
    ranker: 'LuceneRanker',
    fts_table: str,
    options: SearchOptions,
) -> list[dict]:
    boosted_terms = extract_boosted_terms_for_field_reference(query, runtime_field, ranker)
    boosted_tokens = extract_boosted_token_expressions_for_field_reference(conn, fts_table, query, runtime_field, options)
    boosted_phrases = extract_boosted_phrase_expressions_for_field_reference(conn, query, runtime_field)
    boosted_groups = extract_boosted_group_expressions_for_field_reference(conn, fts_table, query, runtime_field, options)
    if (not boosted_terms) and (not boosted_tokens) and (not boosted_phrases) and (not boosted_groups):
        return hits

    out: list[dict] = []
    for hit in hits:
        row = dict(hit)
        factor = to_float32_reference(1.0)
        hit_field = str(row['field'])
        book_id = int(row['book_id'])
        item_id = int(row['item_id'])
        for term, boost in boosted_terms:
            if hit_has_analyzed_term_reference(conn, hit_field, book_id, item_id, term):
                factor = f32_mul_with_udf(conn, factor, boost)
        for expression, boost in boosted_tokens:
            if hit_matches_clause_expression_reference(conn, hit_field, book_id, item_id, expression):
                factor = f32_mul_with_udf(conn, factor, boost)
        for expression, boost in boosted_phrases:
            if hit_matches_clause_expression_reference(conn, hit_field, book_id, item_id, expression):
                factor = f32_mul_with_udf(conn, factor, boost)
        for expression, boost in boosted_groups:
            if hit_matches_clause_expression_reference(conn, hit_field, book_id, item_id, expression):
                factor = f32_mul_with_udf(conn, factor, boost)
        if abs(factor - 1.0) >= 1e-12:
            row['score'] = float(f32_mul_with_udf(conn, float(row['score']), factor))
        out.append(row)
    out.sort(key=lambda row: (row['score'], row['book_id'], row['item_id']))
    return out


def parse_strict_literals_reference(conn: sqlite3.Connection, query: str, runtime_field: str) -> list[StrictLiteral]:
    out: list[StrictLiteral] = []

    def append_operand_literals(kind: str, value: str, occur: str) -> None:
        required = (occur == 'MUST')
        prohibited = (occur == 'MUST_NOT')
        if kind == 'token':
            upper = value.upper()
            if upper in BOOL_OPS or value in {'(', ')'}:
                return
            raw_token = value
            if raw_token == '':
                return
            boosted_clause = parse_boosted_token_clause_with_udf(conn, raw_token, runtime_field)
            if boosted_clause is not None:
                raw = str(boosted_clause['raw'])
                fuzzy_parts = parse_fuzzy_token_with_udf(conn, raw) if bool(boosted_clause['is_fuzzy']) else None
                wildcard_pattern = bool(boosted_clause['has_wildcard'])
            else:
                scope, raw = parse_scoped_token_with_udf(conn, raw_token)
                if scope is not None and scope != runtime_field:
                    return
                raw, _boost = strip_boost_with_udf(conn, raw)
                if raw == '':
                    return
                fuzzy_parts = parse_fuzzy_token_with_udf(conn, raw)
                wildcard_pattern = has_unescaped_wildcard(raw)
            literal_text = raw
            is_pattern = False
            if fuzzy_parts is not None:
                fuzzy_base, _edits = fuzzy_parts
                if fuzzy_base.strip() == '':
                    return
                literal_text = fuzzy_base
            elif wildcard_pattern:
                if raw.strip('*?') == '':
                    return
                literal_text = raw
                is_pattern = True
            elif len(analyze_text_with_udf(conn, raw)) == 0:
                return
            out.append(StrictLiteral(text=literal_text, is_phrase=False, is_pattern=is_pattern, required=required, prohibited=prohibited))
            return

        if kind in {'phrase', 'phrase_slop', 'field_phrase', 'field_phrase_slop'}:
            phrase = value
            scope: str | None = None
            if kind == 'phrase_slop':
                _slop_raw, phrase = value.split('\t', 1)
            elif kind == 'field_phrase':
                scope, phrase = parse_scoped_token_with_udf(conn, value)
            elif kind == 'field_phrase_slop':
                scope, payload = parse_scoped_token_with_udf(conn, value)
                _slop_raw, phrase = payload.split('\t', 1)
            if scope is not None and scope != runtime_field:
                return
            if phrase.strip() == '':
                return
            out.append(StrictLiteral(text=phrase, is_phrase=True, is_pattern=False, required=required, prohibited=prohibited))
            return

        if kind in {'field_group', 'field_group_boost'}:
            if kind == 'field_group':
                scope, inner_query = value.split('\t', 1)
            else:
                scope, _boost_raw, inner_query = value.split('\t', 2)
            if scope != runtime_field:
                return
            for nested_node, nested_occur in iter_query_ast_leaves_with_udf(conn, inner_query):
                append_operand_literals(nested_node.kind, nested_node.value, compose_clause_occur(occur, nested_occur))

    for node, occur in iter_query_ast_leaves_with_udf(conn, query):
        append_operand_literals(node.kind, node.value, occur)

    positive = [idx for idx, literal in enumerate(out) if not literal.prohibited]
    if len(positive) == 1 and (not out[positive[0]].required):
        out[positive[0]].required = True
    return out


def strict_literal_matches_reference(
    conn: sqlite3.Connection,
    raw_text: str,
    literal: StrictLiteral,
    options: SearchOptions,
) -> bool:
    if not literal_needs_strict_check_reference(conn, literal.text, options):
        return True
    normalized_doc = normalize_text_for_strict_reference(conn, raw_text, options)
    normalized_literal = normalize_text_with_udf(
        conn,
        literal.text,
        options,
        lowercase=True,
        keep_wildcards=literal.is_pattern,
        trim=True,
    )
    if normalized_literal == '':
        return True
    if literal.is_pattern:
        return wildcard_matches_text_with_udf(conn, normalized_doc, normalized_literal)
    if literal.is_phrase:
        return normalized_literal in normalized_doc
    return normalized_literal in normalized_doc


def literal_presence_matches_reference(
    conn: sqlite3.Connection,
    raw_text: str,
    literal: StrictLiteral,
    options: SearchOptions,
) -> bool:
    normalized_doc = normalize_text_for_strict_reference(conn, raw_text, options)
    normalized_literal = normalize_text_with_udf(
        conn,
        literal.text,
        options,
        lowercase=True,
        keep_wildcards=literal.is_pattern,
        trim=True,
    )
    if normalized_literal == '':
        return True
    if literal.is_pattern:
        return wildcard_matches_text_with_udf(conn, normalized_doc, normalized_literal)
    return normalized_literal in normalized_doc


def filter_hits_for_strict_modes_reference(
    conn: sqlite3.Connection,
    query: str,
    runtime_field: str,
    hits: list[dict],
    options: SearchOptions,
) -> list[dict]:
    if (
        options.ignore_diacritics
        and options.ignore_hamza_forms
        and bool(options.ignore_letter_forms)
        and options.ignore_digit_forms
    ):
        return hits

    required_tables = []
    if runtime_field in {'page', 'both'}:
        required_tables.extend(['page_content_store', 'page_doc_map'])
    if runtime_field in {'title', 'both'}:
        required_tables.extend(['title_content_store', 'title_doc_map'])
    for table in required_tables:
        if not table_exists_reference(conn, table):
            raise SearchCompileError(f'strict mode requires {table} in schema')

    literals_by_field = {
        'page': parse_strict_literals_reference(conn, query, 'page'),
        'title': parse_strict_literals_reference(conn, query, 'title'),
    }
    if runtime_field == 'page':
        literals_by_field['title'] = []
    if runtime_field == 'title':
        literals_by_field['page'] = []

    filtered: list[dict] = []
    for hit in hits:
        hit_field = str(hit['field'])
        literals = literals_by_field.get(hit_field, [])
        if not literals:
            filtered.append(hit)
            continue

        evaluated = [literal for literal in literals if literal_needs_strict_check_reference(conn, literal.text, options)]
        if not evaluated:
            filtered.append(hit)
            continue

        raw_text = load_hit_raw_text_reference(conn, hit)
        prohibited = [literal for literal in evaluated if literal.prohibited]
        required = [literal for literal in evaluated if literal.required and not literal.prohibited]
        optional = [literal for literal in evaluated if (not literal.required) and (not literal.prohibited)]
        non_sensitive_optional = [
            literal
            for literal in literals
            if (not literal.prohibited) and (not literal.required) and (not literal_needs_strict_check_reference(conn, literal.text, options))
        ]

        if any(strict_literal_matches_reference(conn, raw_text, literal, options) for literal in prohibited):
            continue
        if required and not all(strict_literal_matches_reference(conn, raw_text, literal, options) for literal in required):
            continue
        if (not required) and optional:
            if any(literal_presence_matches_reference(conn, raw_text, literal, options) for literal in non_sensitive_optional):
                filtered.append(hit)
                continue
            if not any(strict_literal_matches_reference(conn, raw_text, literal, options) for literal in optional):
                continue
        filtered.append(hit)
    return filtered


def compile_token_expression_reference(
    conn: sqlite3.Connection,
    fts_table: str,
    runtime_field: str,
    token: str,
    options: SearchOptions,
) -> str | None:
    if token.startswith('+') or token.startswith('-'):
        raise SearchCompileError('required/prohibited (+/-) clauses are not supported yet')

    upper = token.upper()
    if upper in BOOL_OPS:
        return upper
    if token in {'(', ')'}:
        return token
    if token.endswith(':') and token[:-1].lower() in {'page', 'title'}:
        raise SearchCompileError('dangling field scope')

    scope, raw_token = parse_scoped_token_with_udf(conn, token)
    raw_token, _boost = strip_boost_with_udf(conn, raw_token)
    if scope is not None and scope != runtime_field:
        return quote_match_term_reference('__nohit__')

    if has_unescaped_char_reference(raw_token, '[') or has_unescaped_char_reference(raw_token, ']'):
        raise SearchCompileError(f'contains unsupported wildcard pattern: {raw_token}')

    if has_unescaped_wildcard(raw_token):
        if not options.allow_wildcard_search:
            raise SearchCompileError('wildcard search is disabled by policy')
        if raw_token.startswith('*') and not options.allow_suffix_search:
            raise SearchCompileError('suffix search is disabled by policy')
        if raw_token.endswith('*') and not options.allow_prefix_search:
            raise SearchCompileError('prefix search is disabled by policy')

        wildcard_pattern = normalize_wildcard_pattern_reference(conn, raw_token)
        if wildcard_pattern == '':
            raise SearchCompileError('empty wildcard term')
        if not wildcard_has_literal_reference(wildcard_pattern):
            raise SearchCompileError('wildcard term must include at least one literal character')

        is_simple_prefix = (
            wildcard_pattern.endswith('*')
            and wildcard_pattern.count('*') == 1
            and ('?' not in wildcard_pattern)
            and (not wildcard_pattern.startswith('*'))
        )
        if is_simple_prefix:
            prefix_base = wildcard_pattern[:-1].strip()
            if prefix_base == '':
                raise SearchCompileError('empty prefix term')
            return quote_match_term_reference(prefix_base) + '*'

        is_simple_suffix = (
            wildcard_pattern.startswith('*')
            and wildcard_pattern.count('*') == 1
            and ('?' not in wildcard_pattern)
            and (not wildcard_pattern.endswith('*'))
        )
        if is_simple_suffix:
            expanded = expand_suffix_terms_reference(conn, fts_table, wildcard_pattern[1:], options.suffix_max_expansions)
        else:
            expanded = expand_wildcard_terms_reference(conn, fts_table, wildcard_pattern, options.wildcard_max_expansions)
        if not expanded:
            return quote_match_term_reference('__nohit__')
        if len(expanded) == 1:
            return quote_match_term_reference(expanded[0])
        return '(' + ' OR '.join(quote_match_term_reference(term) for term in expanded) + ')'

    fuzzy_parts = parse_fuzzy_token_with_udf(conn, raw_token)
    if fuzzy_parts is not None:
        if not options.allow_fuzzy_search:
            raise SearchCompileError('fuzzy search is disabled by policy')
        base, edits = fuzzy_parts
        normalized_base = normalize_query_text_reference(conn, base, options)
        expanded = expand_fuzzy_terms_reference(conn, fts_table, normalized_base, edits, options.fuzzy_max_expansions)
        if not expanded:
            return quote_match_term_reference('__nohit__')
        if len(expanded) == 1:
            return quote_match_term_reference(expanded[0])
        return '(' + ' OR '.join(quote_match_term_reference(term) for term in expanded) + ')'

    if raw_token.endswith('*') and len(raw_token) > 1:
        if not options.allow_prefix_search:
            raise SearchCompileError('prefix search is disabled by policy')
        base = raw_token[:-1].strip()
        if base == '':
            raise SearchCompileError('empty prefix term')
        return quote_match_term_reference(base) + '*'

    if len(analyze_text_with_udf(conn, raw_token)) == 0:
        return None

    return quote_match_term_reference(raw_token)


def build_execution_plan_reference(
    conn: sqlite3.Connection,
    fts_table: str,
    runtime_field: str,
    query: str,
    options: SearchOptions,
) -> SqliteExecutionPlan:
    def compile_operand(kind: str, value: str) -> str | None:
        def compile_phrase_operand(phrase_value: str, explicit_slop: int) -> str:
            terms, rel_positions = analyze_phrase_terms_with_positions_reference(conn, phrase_value)
            if len(terms) == 0:
                return quote_match_term_reference('__nohit__')
            if len(terms) == 1:
                return quote_match_term_reference(terms[0])
            implicit_slop = max(0, rel_positions[-1] - (len(terms) - 1))
            effective_slop = max(0, int(explicit_slop)) + implicit_slop
            if effective_slop <= 0:
                return quote_match_term_reference(' '.join(terms))
            return f"NEAR({' '.join(quote_match_term_reference(term) for term in terms)}, {effective_slop})"

        if kind == 'phrase':
            return compile_phrase_operand(value, 0)
        if kind == 'phrase_slop':
            slop_raw, phrase_value = value.split('\t', 1)
            return compile_phrase_operand(phrase_value, max(0, int(slop_raw)))
        if kind == 'field_phrase':
            scope, phrase_value = parse_scoped_token_with_udf(conn, value)
            if scope is not None and scope != runtime_field:
                return quote_match_term_reference('__nohit__')
            return compile_phrase_operand(phrase_value, 0)
        if kind == 'field_phrase_slop':
            scope, payload = parse_scoped_token_with_udf(conn, value)
            slop_raw, phrase_value = payload.split('\t', 1)
            if scope is not None and scope != runtime_field:
                return quote_match_term_reference('__nohit__')
            return compile_phrase_operand(phrase_value, max(0, int(slop_raw)))
        if kind == 'field_group':
            scope, inner_query = value.split('\t', 1)
            if scope != runtime_field:
                return quote_match_term_reference('__nohit__')
            return compile_match_expression(conn, fts_table, runtime_field, inner_query, options)
        if kind == 'field_group_boost':
            scope, _boost_raw, inner_query = value.split('\t', 2)
            if scope != runtime_field:
                return quote_match_term_reference('__nohit__')
            return compile_match_expression(conn, fts_table, runtime_field, inner_query, options)
        return compile_token_expression_reference(conn, fts_table, runtime_field, value, options)

    def build_from_compiled_clauses(clauses: list[tuple[str, str]]) -> str:
        must_terms = [f'({expr})' for expr, occur in clauses if occur == 'MUST']
        should_terms = [f'({expr})' for expr, occur in clauses if occur == 'SHOULD']
        must_not_terms = [f'({expr})' for expr, occur in clauses if occur == 'MUST_NOT']

        if must_terms:
            positive = ' AND '.join(must_terms)
        elif should_terms:
            positive = ' OR '.join(should_terms)
        else:
            positive = ''

        if positive == '':
            return quote_match_term_reference('__nohit__')
        if not must_not_terms:
            return positive
        return f'({positive}) NOT (' + ' OR '.join(must_not_terms) + ')'

    def compile_ast_node(node: QueryAstLeaf | QueryAstGroup) -> str | None:
        if isinstance(node, QueryAstLeaf):
            return compile_operand(node.kind, node.value)
        return compile_ast_clauses(node.clauses)

    def compile_ast_clauses(clauses: tuple[QueryAstClause, ...]) -> str:
        compiled_clauses: list[tuple[str, str]] = []
        for clause in clauses:
            compiled = compile_ast_node(clause.node)
            if compiled is None:
                continue
            compiled_clauses.append((compiled, clause.occur))
        return build_from_compiled_clauses(compiled_clauses)

    ast = parse_query_ast_with_udf(conn, query)
    return SqliteExecutionPlan(match_expression=compile_ast_clauses(ast.clauses))


def compile_match_expression_reference(
    conn: sqlite3.Connection,
    fts_table: str,
    runtime_field: str,
    query: str,
    options: SearchOptions,
) -> str:
    return build_execution_plan_reference(conn, fts_table, runtime_field, query, options).match_expression


def search_field_reference(
    conn: sqlite3.Connection,
    field: str,
    query: str,
    options: SearchOptions,
    limit: int,
    ranker: 'LuceneRanker',
) -> tuple[str, list[dict]]:
    def is_single_multiterm_constant_query(local_query: str, runtime_field: str) -> bool:
        tokens = split_query_reference(local_query)
        if len(tokens) == 0:
            return False
        raw_token: str | None = None
        if len(tokens) == 1 and tokens[0][0] == 'token':
            raw_token = tokens[0][1]
        elif len(tokens) == 2 and tokens[0][0] == 'token' and tokens[0][1] in {'+', '-'} and tokens[1][0] == 'token':
            raw_token = tokens[1][1]
        else:
            return False
        if raw_token is None:
            return False
        if raw_token.startswith('+') or raw_token.startswith('-'):
            raw_token = raw_token[1:]
        if raw_token == '' or raw_token.upper() in BOOL_OPS:
            return False
        scope, raw = parse_scoped_token_with_udf(conn, raw_token)
        if scope is not None and scope != runtime_field:
            return False
        raw, _boost = strip_boost_with_udf(conn, raw)
        if raw == '':
            return False
        return has_unescaped_wildcard(raw)

    boost_factor = parse_boost_factor_for_field(conn, query, field)
    ranking_query = strip_boost_for_ranking(conn, query, field)

    if field == 'page':
        expr = compile_match_expression_reference(conn, 'page_fts', 'page', ranking_query, options)
        term = ranker.tokenize_simple_query(ranking_query)
        if term is not None:
            return expr, apply_boost_factor_reference(ranker.score_simple_term('page', term, limit), boost_factor, conn)
        mixed_phrase_term_hits = ranker.score_phrase_term_boolean('page', ranking_query, limit)
        if mixed_phrase_term_hits is not None:
            return expr, apply_boost_factor_reference(mixed_phrase_term_hits, boost_factor, conn)
        bool_hits = ranker.score_simple_boolean('page', ranking_query, limit)
        if bool_hits is not None:
            return expr, apply_boost_factor_reference(bool_hits, boost_factor, conn)
        phrase_hits = ranker.score_simple_phrase('page', ranking_query, limit)
        if phrase_hits is not None:
            return expr, apply_boost_factor_reference(phrase_hits, boost_factor, conn)
        if is_single_multiterm_constant_query(ranking_query, 'page'):
            rows = conn.execute(
                """
                SELECT m.book_id, m.page_id AS item_id
                FROM page_fts
                JOIN page_doc_map m ON m.rowid = page_fts.rowid
                WHERE page_fts MATCH ?
                ORDER BY m.book_id, m.page_id
                LIMIT ?
                """,
                (expr, limit),
            ).fetchall()
            hits = [{'field': 'page', 'book_id': int(b), 'item_id': int(i), 'score': -1.0} for b, i in rows]
            return expr, apply_boost_factor_reference(hits, boost_factor, conn)
        rows = conn.execute(
            """
            SELECT m.book_id, m.page_id AS item_id, bm25(page_fts) AS score
            FROM page_fts
            JOIN page_doc_map m ON m.rowid = page_fts.rowid
            WHERE page_fts MATCH ?
            ORDER BY score, m.book_id, m.page_id
            LIMIT ?
            """,
            (expr, limit),
        ).fetchall()
        hits = [{'field': 'page', 'book_id': int(b), 'item_id': int(i), 'score': float(s)} for b, i, s in rows]
        hits = apply_boost_factor_reference(hits, boost_factor, conn)
        return expr, apply_clause_term_boosts_reference(conn, hits, ranking_query, 'page', ranker, 'page_fts', options)

    if field == 'title':
        expr = compile_match_expression_reference(conn, 'title_fts', 'title', ranking_query, options)
        term = ranker.tokenize_simple_query(ranking_query)
        if term is not None:
            return expr, apply_boost_factor_reference(ranker.score_simple_term('title', term, limit), boost_factor, conn)
        mixed_phrase_term_hits = ranker.score_phrase_term_boolean('title', ranking_query, limit)
        if mixed_phrase_term_hits is not None:
            return expr, apply_boost_factor_reference(mixed_phrase_term_hits, boost_factor, conn)
        bool_hits = ranker.score_simple_boolean('title', ranking_query, limit)
        if bool_hits is not None:
            return expr, apply_boost_factor_reference(bool_hits, boost_factor, conn)
        phrase_hits = ranker.score_simple_phrase('title', ranking_query, limit)
        if phrase_hits is not None:
            return expr, apply_boost_factor_reference(phrase_hits, boost_factor, conn)
        if is_single_multiterm_constant_query(ranking_query, 'title'):
            rows = conn.execute(
                """
                SELECT m.book_id, m.title_id AS item_id
                FROM title_fts
                JOIN title_doc_map m ON m.rowid = title_fts.rowid
                WHERE title_fts MATCH ?
                ORDER BY m.book_id, m.title_id
                LIMIT ?
                """,
                (expr, limit),
            ).fetchall()
            hits = [{'field': 'title', 'book_id': int(b), 'item_id': int(i), 'score': -1.0} for b, i in rows]
            return expr, apply_boost_factor_reference(hits, boost_factor, conn)
        rows = conn.execute(
            """
            SELECT m.book_id, m.title_id AS item_id, bm25(title_fts) AS score
            FROM title_fts
            JOIN title_doc_map m ON m.rowid = title_fts.rowid
            WHERE title_fts MATCH ?
            ORDER BY score, m.book_id, m.title_id
            LIMIT ?
            """,
            (expr, limit),
        ).fetchall()
        hits = [{'field': 'title', 'book_id': int(b), 'item_id': int(i), 'score': float(s)} for b, i, s in rows]
        hits = apply_boost_factor_reference(hits, boost_factor, conn)
        return expr, apply_clause_term_boosts_reference(conn, hits, ranking_query, 'title', ranker, 'title_fts', options)

    raise SearchCompileError(f'unsupported field: {field}')


def run_search_reference(
    conn: sqlite3.Connection,
    query: str,
    field: str,
    options: SearchOptions,
    limit: int,
    ranker: LuceneRanker | None = None,
) -> dict:
    if query.strip() == '':
        raise SearchCompileError('query is empty after parsing')
    if field not in {'page', 'title', 'both'}:
        raise SearchCompileError(f'unsupported field selection: {field}')
    if limit < 1:
        raise SearchCompileError('limit must be >= 1')

    runtime_ranker = ranker if ranker is not None else LuceneRanker(conn)

    def nohit_result() -> dict:
        if field in {'page', 'title'}:
            return {'compiled': {field: '"__nohit__"'}, 'hits': []}
        return {'compiled': {'page': '"__nohit__"', 'title': '"__nohit__"'}, 'hits': []}

    try:
        if field in {'page', 'title'}:
            compiled, hits = search_field_reference(conn, field, query, options, limit, runtime_ranker)
            hits = filter_hits_for_strict_modes_reference(conn, query, field, hits, options)
            return {'compiled': {field: compiled}, 'hits': hits}

        page_compiled, page_hits = search_field_reference(conn, 'page', query, options, limit, runtime_ranker)
        title_compiled, title_hits = search_field_reference(conn, 'title', query, options, limit, runtime_ranker)
        page_hits = filter_hits_for_strict_modes_reference(conn, query, 'page', page_hits, options)
        title_hits = filter_hits_for_strict_modes_reference(conn, query, 'title', title_hits, options)
        merged = page_hits + title_hits
        merged.sort(key=lambda row: (row['score'], row['field'], row['book_id'], row['item_id']))
        return {'compiled': {'page': page_compiled, 'title': title_compiled}, 'hits': merged[:limit]}
    except SearchCompileError:
        if not options.lenient_parse_errors:
            raise
        return nohit_result()


def run_search_c_backend_reference(
    conn: sqlite3.Connection,
    query: str,
    field: str,
    options: SearchOptions,
    limit: int,
) -> dict:
    options_json = json.dumps(search_options_payload_reference(options), ensure_ascii=False)
    try:
        row = conn.execute(
            'SELECT sqlite_tokenizer_ar_execute_query_json(?, ?, ?, ?)',
            (query, field, int(limit), options_json),
        ).fetchone()
    except sqlite3.OperationalError as exc:
        message = str(exc)
        if 'no such function: sqlite_tokenizer_ar_execute_query_json' in message:
            raise SearchCompileError(
                'C backend is not available: missing UDF sqlite_tokenizer_ar_execute_query_json'
            ) from exc
        raise SearchCompileError(message) from exc
    if row is None or row[0] is None:
        raise SearchCompileError('C backend returned no result')
    try:
        payload = json.loads(str(row[0]))
    except json.JSONDecodeError as exc:
        raise SearchCompileError('C backend returned invalid JSON payload') from exc
    if not isinstance(payload, dict):
        raise SearchCompileError('C backend payload must be a JSON object')
    compiled = payload.get('compiled')
    hits = payload.get('hits')
    scores_are_f32 = bool(payload.get('scores_are_f32', True))
    if not isinstance(compiled, dict) or not isinstance(hits, list):
        raise SearchCompileError('C backend payload must include compiled{...} and hits[...]')
    normalized_hits: list[dict] = []
    for hit in hits:
        if not isinstance(hit, dict):
            raise SearchCompileError('C backend hit rows must be JSON objects')
        if 'field' not in hit or 'book_id' not in hit or 'item_id' not in hit or 'score' not in hit:
            raise SearchCompileError('C backend hit rows must include field/book_id/item_id/score')
        try:
            raw_score = float(hit['score'])
            normalized_hit = {
                'field': str(hit['field']),
                'book_id': int(hit['book_id']),
                'item_id': int(hit['item_id']),
                'score': float(to_float32_reference(raw_score) if scores_are_f32 else raw_score),
            }
        except (TypeError, ValueError) as exc:
            raise SearchCompileError('C backend hit rows include invalid value types') from exc
        normalized_hits.append(normalized_hit)
    return {'compiled': compiled, 'hits': normalized_hits}


def run_search_backend_reference(
    conn: sqlite3.Connection,
    query: str,
    field: str,
    options: SearchOptions,
    limit: int,
    backend: str,
    ranker: LuceneRanker | None = None,
) -> dict:
    backend_key = backend.strip().lower()
    if backend_key == 'python':
        return run_search_reference(conn, query=query, field=field, options=options, limit=limit, ranker=ranker)
    if backend_key == 'c':
        return run_search_c_backend_reference(conn, query=query, field=field, options=options, limit=limit)
    raise SearchCompileError(f'unsupported backend: {backend}')


def parse_options_reference(args: argparse.Namespace) -> SearchOptions:
    allow_prefix = True
    if args.disable_prefix_search and (not args.allow_prefix_search):
        allow_prefix = False
    ignore_letter_forms: bool | None = (not args.respect_letter_forms)
    if args.respect_hamza_forms and (not args.respect_letter_forms):
        ignore_letter_forms = None
    return SearchOptions(
        allow_prefix_search=allow_prefix,
        allow_suffix_search=(not args.disable_suffix_search),
        allow_wildcard_search=(not args.disable_wildcard_search),
        ignore_diacritics=(not args.respect_diacritics),
        ignore_hamza_forms=(not args.respect_hamza_forms),
        ignore_letter_forms=ignore_letter_forms,
        ignore_digit_forms=(not args.respect_digit_forms),
        suffix_max_expansions=args.suffix_max_expansions,
        wildcard_max_expansions=args.wildcard_max_expansions,
        allow_fuzzy_search=(not args.disable_fuzzy_search),
        fuzzy_max_expansions=args.fuzzy_max_expansions,
        lenient_parse_errors=args.lenient_parse_errors,
    )


def build_cli_parser_reference() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description='Run query-compat search over SQLite FTS schema')
    parser.add_argument('--db', required=True, help='SQLite DB path')
    parser.add_argument('--query', required=True, help='User query')
    parser.add_argument('--field', default='both', choices=['page', 'title', 'both'])
    parser.add_argument('--backend', default='c', choices=['python', 'c'], help='Query execution backend')
    parser.add_argument('--limit', type=int, default=20)
    parser.add_argument('--allow-prefix-search', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--disable-prefix-search', action='store_true', help='Disable trailing-* prefix terms')
    parser.add_argument('--disable-suffix-search', action='store_true', help='Disable leading-* suffix terms')
    parser.add_argument('--disable-wildcard-search', action='store_true', help='Disable wildcard terms (*, ?)')
    parser.add_argument('--respect-diacritics', action='store_true')
    parser.add_argument('--respect-hamza-forms', action='store_true')
    parser.add_argument('--respect-letter-forms', action='store_true')
    parser.add_argument('--respect-digit-forms', action='store_true')
    parser.add_argument('--suffix-max-expansions', type=int, default=256)
    parser.add_argument('--wildcard-max-expansions', type=int, default=256)
    parser.add_argument('--disable-fuzzy-search', action='store_true')
    parser.add_argument('--fuzzy-max-expansions', type=int, default=128)
    parser.add_argument('--lenient-parse-errors', action='store_true', help='Return empty hits for malformed queries')
    return parser


def main_reference() -> None:
    parser = build_cli_parser_reference()
    args = parser.parse_args()

    db_path = Path(args.db).resolve()
    if not db_path.exists():
        raise SystemExit(f'error: db not found: {db_path}')

    options = parse_options_reference(args)
    conn = sqlite3.connect(str(db_path))
    try:
        result = run_search_backend_reference(conn, query=args.query, field=args.field, options=options, limit=args.limit, backend=args.backend)
        output = {
            'backend': args.backend,
            'query': args.query,
            'field': args.field,
            'options': search_options_payload_reference(options),
            'compiled': result['compiled'],
            'hits': result['hits'],
        }
    except SearchCompileError as exc:
        raise SystemExit(f'error: {exc}') from exc
    finally:
        conn.close()

    print(json.dumps(output, ensure_ascii=False))


def expand_suffix_terms_reference(conn: sqlite3.Connection, fts_table: str, suffix: str, max_expansions: int) -> list[str]:
    if suffix == '':
        return []
    vocab = ensure_vocab_reference(conn, fts_table)
    rows = conn.execute(
        f'SELECT term FROM {vocab} WHERE term GLOB ? AND term <> ? ORDER BY term LIMIT ?',
        (f'*{suffix}', EMPTY_TERM_SENTINEL, max_expansions),
    ).fetchall()
    return [str(row[0]) for row in rows]


def expand_wildcard_terms_reference(
    conn: sqlite3.Connection,
    fts_table: str,
    wildcard_pattern: str,
    max_expansions: int,
) -> list[str]:
    vocab = ensure_vocab_reference(conn, fts_table)
    rows = conn.execute(
        f"""
        SELECT term
        FROM {vocab}
        WHERE term <> ? AND term GLOB ?
        ORDER BY term
        LIMIT ?
        """,
        (EMPTY_TERM_SENTINEL, wildcard_pattern, max_expansions),
    ).fetchall()
    return [str(row[0]) for row in rows]


def expand_fuzzy_terms_reference(
    conn: sqlite3.Connection,
    fts_table: str,
    term: str,
    max_edits: int,
    max_expansions: int,
) -> list[str]:
    if term == '':
        return []
    vocab = ensure_vocab_reference(conn, fts_table)
    min_len = max(1, len(term) - max_edits)
    max_len = len(term) + max_edits
    rows = conn.execute(
        f"""
        SELECT term
        FROM {vocab}
        WHERE term <> ? AND length(term) BETWEEN ? AND ?
        ORDER BY term
        LIMIT ?
        """,
        (EMPTY_TERM_SENTINEL, min_len, max_len, max(512, max_expansions * 8)),
    ).fetchall()
    candidates = [str(row[0]) for row in rows]
    out: list[str] = []
    for candidate_term in candidates:
        if levenshtein_distance_with_udf(conn, term, candidate_term, max_edits) <= max_edits:
            out.append(candidate_term)
            if len(out) >= max_expansions:
                break
    return out


def is_escaped_char_reference(value: str, index: int) -> bool:
    backslashes = 0
    cursor = index - 1
    while cursor >= 0 and value[cursor] == '\\':
        backslashes += 1
        cursor -= 1
    return (backslashes % 2) == 1


def read_quoted_segment_reference(value: str, start: int) -> tuple[str, int]:
    index = start
    out: list[str] = []
    while index < len(value):
        ch = value[index]
        if ch == '"' and (not is_escaped_char_reference(value, index)):
            return ''.join(out), index + 1
        if ch == '\\' and index + 1 < len(value):
            out.append(value[index + 1])
            index += 2
            continue
        out.append(ch)
        index += 1
    raise SearchCompileError('unclosed quote in query')


def has_unescaped_char_reference(value: str, target: str) -> bool:
    index = 0
    while index < len(value):
        ch = value[index]
        if ch == '\\' and index + 1 < len(value):
            index += 2
            continue
        if ch == target:
            return True
        index += 1
    return False


def find_unescaped_char_from_reference(value: str, target: str, start: int) -> int:
    index = max(0, start)
    while index < len(value):
        ch = value[index]
        if ch == '\\' and index + 1 < len(value):
            index += 2
            continue
        if ch == target:
            return index
        index += 1
    return -1


def find_unescaped_char_reference(value: str, target: str) -> int:
    return find_unescaped_char_from_reference(value, target, 0)


def has_unescaped_wildcard_reference(value: str) -> bool:
    return has_unescaped_char_reference(value, '*') or has_unescaped_char_reference(value, '?')


def unescape_query_escapes_reference(value: str) -> str:
    index = 0
    out: list[str] = []
    while index < len(value):
        ch = value[index]
        if ch == '\\' and index + 1 < len(value):
            out.append(value[index + 1])
            index += 2
            continue
        out.append(ch)
        index += 1
    return ''.join(out)


def parse_boost_number_reference(value: str) -> float | None:
    if re.fullmatch(BOOST_NUMBER_RE, value) is None:
        return None
    return float(value)


def parse_scoped_token_reference(token: str) -> tuple[str | None, str]:
    split_at = find_unescaped_char_reference(token, ':')
    if split_at < 0:
        return None, token
    scope = token[:split_at]
    raw = token[split_at + 1:]
    scope_lc = scope.lower()
    if scope_lc in {'page', 'title'} and raw != '':
        return scope_lc, raw
    return None, token


def parse_fuzzy_token_reference(token: str) -> tuple[str, int] | None:
    split_at = -1
    for index in range(len(token) - 1, -1, -1):
        if token[index] == '~' and (not is_escaped_char_reference(token, index)):
            split_at = index
            break
    if split_at <= 0:
        return None
    base = token[:split_at]
    edits_raw = token[split_at + 1:]
    if edits_raw != '' and (not edits_raw.isdigit()):
        return None
    edits = 2 if edits_raw == '' else int(edits_raw)
    if edits < 0:
        edits = 0
    if edits > 2:
        edits = 2
    return base, edits


def strip_boost_reference(token: str) -> tuple[str, float | None]:
    split_at = -1
    for index in range(len(token) - 1, -1, -1):
        if token[index] == '^' and (not is_escaped_char_reference(token, index)):
            split_at = index
            break
    if split_at < 0:
        return token, None
    base = token[:split_at]
    boost_raw = token[split_at + 1:]
    if re.fullmatch(BOOST_NUMBER_RE, boost_raw) is None:
        return token, None
    boost = float(boost_raw)
    if base == '':
        raise SearchCompileError('invalid boosted token')
    return base, boost


def parse_group_boost_suffix_reference(query: str, start: int) -> tuple[float, int] | None:
    if start >= len(query) or query[start] != '^':
        return None
    index = start + 1
    boost_start = index
    while index < len(query) and (query[index].isdigit() or query[index] == '.'):
        index += 1
    if index == boost_start:
        raise SearchCompileError('invalid group boost')
    parsed_boost = parse_boost_number_reference(query[boost_start:index])
    if parsed_boost is None:
        raise SearchCompileError('invalid group boost')
    return parsed_boost, index


def parse_field_group_segment_reference(query: str, start: int) -> tuple[str, str, float | None, int] | None:
    field_sep = find_unescaped_char_from_reference(query, ':', start)
    if field_sep <= start:
        return None
    field_candidate = query[start:field_sep]
    scope = field_candidate.lower()
    if scope not in {'page', 'title'}:
        return None
    if field_sep + 1 >= len(query) or query[field_sep + 1] != '(':
        return None

    index = field_sep + 2
    depth = 1
    in_quote = False
    while index < len(query):
        ch = query[index]
        if ch == '"' and (not is_escaped_char_reference(query, index)):
            in_quote = not in_quote
            index += 1
            continue
        if not in_quote:
            if ch == '(':
                depth += 1
            elif ch == ')':
                depth -= 1
                if depth == 0:
                    break
        index += 1
    if depth != 0:
        raise SearchCompileError('unclosed field-group parentheses in query')

    inner_query = query[field_sep + 2:index]
    index += 1
    boost: float | None = None
    if index < len(query) and query[index] == '^':
        index += 1
        boost_start = index
        while index < len(query) and (query[index].isdigit() or query[index] == '.'):
            index += 1
        if index > boost_start:
            parsed_boost = parse_boost_number_reference(query[boost_start:index])
            if parsed_boost is None:
                raise SearchCompileError('invalid group boost')
            boost = parsed_boost

    return scope, inner_query, boost, index


def parse_top_level_group_boost(query: str) -> tuple[str, float] | None:
    raw = query.strip()
    if len(raw) < 4 or raw[0] != '(':
        return None
    depth = 0
    in_quote = False
    close_index = -1
    for idx, ch in enumerate(raw):
        if ch == '"' and (not is_escaped_char_reference(raw, idx)):
            in_quote = not in_quote
            continue
        if in_quote:
            continue
        if ch == '(':
            depth += 1
            continue
        if ch == ')':
            depth -= 1
            if depth == 0:
                close_index = idx
                break
            continue
    if close_index <= 0:
        return None
    if close_index + 2 > len(raw) or raw[close_index + 1] != '^':
        return None
    boost_raw = raw[close_index + 2:]
    if boost_raw == '' or re.fullmatch(BOOST_NUMBER_RE, boost_raw) is None:
        return None
    return raw[1:close_index], float(boost_raw)


def parse_whole_scoped_group(query: str) -> tuple[str, str, float | None] | None:
    raw = query.strip()
    if raw == '':
        return None
    try:
        parsed = parse_field_group_segment_reference(raw, 0)
    except SearchCompileError:
        return None
    if parsed is None:
        return None
    scope, inner_query, boost, next_index = parsed
    if raw[next_index:].strip() != '':
        return None
    return scope, inner_query, boost


def parse_boosted_token_clause_python(token: str, runtime_field: str) -> dict | None:
    scope, raw = parse_scoped_token_reference(token)
    runtime_field_key = runtime_field.strip().lower()
    if scope is not None and runtime_field_key != 'both' and scope != runtime_field_key:
        return None
    raw, maybe_boost = strip_boost_reference(raw)
    if maybe_boost is None or abs(maybe_boost - 1.0) < 1e-12:
        return None
    if raw == '':
        return None
    return {
        'scope': scope,
        'raw': raw,
        'boost': float(maybe_boost),
        'has_wildcard': has_unescaped_wildcard_reference(raw),
        'is_fuzzy': parse_fuzzy_token_reference(raw) is not None,
    }


def parse_simple_boolean_python(query: str, simple_query_re: re.Pattern[str]) -> tuple[str, str, float, str, float] | None:
    raw = query.strip()
    if '"' in raw or '(' in raw or ')' in raw:
        return None
    parts = raw.split()
    if len(parts) != 3:
        return None
    left, op, right = parts
    op_upper = op.upper()
    if op_upper not in {'AND', 'OR'}:
        return None
    left_boost = 1.0
    if simple_query_re.fullmatch(left) is None:
        left_base, maybe_boost = strip_boost_reference(left)
        if maybe_boost is None or simple_query_re.fullmatch(left_base) is None:
            return None
        left = left_base
        left_boost = maybe_boost
    right_boost = 1.0
    if simple_query_re.fullmatch(right) is None:
        right_base, maybe_boost = strip_boost_reference(right)
        if maybe_boost is None or simple_query_re.fullmatch(right_base) is None:
            return None
        right = right_base
        right_boost = maybe_boost
    return op_upper, left, left_boost, right, right_boost


def parse_simple_phrase_query_python(query: str) -> tuple[str, int] | None:
    match = re.fullmatch(rf'\s*"({QUOTED_QUERY_CONTENT_RE})"(?:~([0-9]+))?\s*', query)
    if match is None:
        return None
    phrase_text = unescape_query_escapes_reference(match.group(1))
    slop = 0
    if match.group(2) is not None:
        slop = max(0, int(match.group(2)))
    return phrase_text, slop


def parse_single_phrase_clause_python(query: str) -> tuple[str | None, str, int, float] | None:
    scope, scoped_raw = parse_scoped_token_reference(query.strip())
    phrase_raw, maybe_boost = strip_boost_reference(scoped_raw)
    parsed_phrase = parse_simple_phrase_query_python(phrase_raw)
    if parsed_phrase is None:
        return None
    phrase_text, slop = parsed_phrase
    boost = 1.0 if maybe_boost is None else float(maybe_boost)
    return scope, phrase_text, slop, boost


def parse_phrase_term_boolean_python(query: str) -> dict | None:
    raw = query.strip()
    if raw == '':
        return None

    phrase_then_phrase = re.fullmatch(
        rf'(?:(page|title):)?"({QUOTED_QUERY_CONTENT_RE})"(?:~([0-9]+))?(?:\^({BOOST_NUMBER_RE}))?\s+'
        rf'(AND|OR)\s+'
        rf'(?:(page|title):)?"({QUOTED_QUERY_CONTENT_RE})"(?:~([0-9]+))?(?:\^({BOOST_NUMBER_RE}))?',
        raw,
        flags=re.IGNORECASE,
    )
    if phrase_then_phrase is not None:
        return {
            'shape': 'phrase_phrase',
            'op': phrase_then_phrase.group(5).upper(),
            'left_scope': None if phrase_then_phrase.group(1) is None else phrase_then_phrase.group(1).lower(),
            'left_term': None,
            'left_phrase': unescape_query_escapes_reference(phrase_then_phrase.group(2)),
            'left_slop': 0 if phrase_then_phrase.group(3) is None else max(0, int(phrase_then_phrase.group(3))),
            'left_boost': 1.0 if phrase_then_phrase.group(4) is None else float(phrase_then_phrase.group(4)),
            'right_scope': None if phrase_then_phrase.group(6) is None else phrase_then_phrase.group(6).lower(),
            'right_term': None,
            'right_phrase': unescape_query_escapes_reference(phrase_then_phrase.group(7)),
            'right_slop': 0 if phrase_then_phrase.group(8) is None else max(0, int(phrase_then_phrase.group(8))),
            'right_boost': 1.0 if phrase_then_phrase.group(9) is None else float(phrase_then_phrase.group(9)),
        }

    phrase_then_term = re.fullmatch(
        rf'(?:(page|title):)?"({QUOTED_QUERY_CONTENT_RE})"(?:~([0-9]+))?(?:\^({BOOST_NUMBER_RE}))?\s+'
        rf'(AND|OR)\s+'
        rf'((?:page|title):)?([^\s]+?)(?:\^({BOOST_NUMBER_RE}))?',
        raw,
        flags=re.IGNORECASE,
    )
    if phrase_then_term is not None:
        right_scope_raw = phrase_then_term.group(6)
        return {
            'shape': 'phrase_term',
            'op': phrase_then_term.group(5).upper(),
            'left_scope': None if phrase_then_term.group(1) is None else phrase_then_term.group(1).lower(),
            'left_term': None,
            'left_phrase': unescape_query_escapes_reference(phrase_then_term.group(2)),
            'left_slop': 0 if phrase_then_term.group(3) is None else max(0, int(phrase_then_term.group(3))),
            'left_boost': 1.0 if phrase_then_term.group(4) is None else float(phrase_then_term.group(4)),
            'right_scope': None if right_scope_raw is None else right_scope_raw[:-1].lower(),
            'right_term': phrase_then_term.group(7),
            'right_phrase': None,
            'right_slop': 0,
            'right_boost': 1.0 if phrase_then_term.group(8) is None else float(phrase_then_term.group(8)),
        }

    term_then_phrase = re.fullmatch(
        rf'((?:page|title):)?([^\s]+?)(?:\^({BOOST_NUMBER_RE}))?\s+'
        rf'(AND|OR)\s+'
        rf'(?:(page|title):)?"({QUOTED_QUERY_CONTENT_RE})"(?:~([0-9]+))?(?:\^({BOOST_NUMBER_RE}))?',
        raw,
        flags=re.IGNORECASE,
    )
    if term_then_phrase is not None:
        left_scope_raw = term_then_phrase.group(1)
        return {
            'shape': 'term_phrase',
            'op': term_then_phrase.group(4).upper(),
            'left_scope': None if left_scope_raw is None else left_scope_raw[:-1].lower(),
            'left_term': term_then_phrase.group(2),
            'left_phrase': None,
            'left_slop': 0,
            'left_boost': 1.0 if term_then_phrase.group(3) is None else float(term_then_phrase.group(3)),
            'right_scope': None if term_then_phrase.group(5) is None else term_then_phrase.group(5).lower(),
            'right_term': None,
            'right_phrase': unescape_query_escapes_reference(term_then_phrase.group(6)),
            'right_slop': 0 if term_then_phrase.group(7) is None else max(0, int(term_then_phrase.group(7))),
            'right_boost': 1.0 if term_then_phrase.group(8) is None else float(term_then_phrase.group(8)),
        }

    return None


def levenshtein_distance(a: str, b: str, max_edits: int) -> int:
    if a == b:
        return 0
    if abs(len(a) - len(b)) > max_edits:
        return max_edits + 1

    previous = list(range(len(b) + 1))
    for i, ca in enumerate(a, start=1):
        current = [i]
        min_in_row = current[0]
        for j, cb in enumerate(b, start=1):
            cost = 0 if ca == cb else 1
            current.append(min(previous[j] + 1, current[j - 1] + 1, previous[j - 1] + cost))
            if current[j] < min_in_row:
                min_in_row = current[j]
        if min_in_row > max_edits:
            return max_edits + 1
        previous = current
    return previous[-1]


def extract_boosted_phrase_spans_python(query: str) -> list[tuple[str | None, str, int, float]]:
    out: list[tuple[str | None, str, int, float]] = []
    for match in BOOSTED_PHRASE_RE.finditer(query):
        phrase_value = unescape_query_escapes_reference(match.group('phrase')).strip()
        if phrase_value == '':
            continue
        boost = float(match.group('boost'))
        if abs(boost - 1.0) < 1e-12:
            continue
        slop_raw = match.group('slop')
        explicit_slop = 0 if slop_raw is None else max(0, int(slop_raw))
        scope_raw = match.group('scope')
        scope = None if scope_raw is None else scope_raw.lower()
        out.append((scope, phrase_value, explicit_slop, boost))
    return out


def extract_boosted_group_spans_python(query: str, runtime_field: str) -> list[tuple[str, float]]:
    raw = query.strip()
    out: list[tuple[str, float]] = []
    stack: list[int] = []
    in_quote = False
    index = 0
    while index < len(raw):
        ch = raw[index]
        if ch == '"':
            in_quote = not in_quote
            index += 1
            continue
        if in_quote:
            index += 1
            continue
        if ch == '(':
            stack.append(index)
            index += 1
            continue
        if ch == ')':
            open_index = stack.pop() if stack else -1
            close_index = index
            index += 1
            if index >= len(raw) or raw[index] != '^':
                continue
            index += 1
            boost_start = index
            while index < len(raw) and (raw[index].isdigit() or raw[index] == '.'):
                index += 1
            if index == boost_start:
                continue
            if open_index < 0:
                continue
            parsed_boost = parse_boost_number_reference(raw[boost_start:index])
            if parsed_boost is None:
                continue
            boost = parsed_boost
            if abs(boost - 1.0) < 1e-12:
                continue

            scope: str | None = None
            scope_start = open_index
            if open_index >= 6 and raw[open_index - 6:open_index].lower() == 'title:':
                scope = 'title'
                scope_start = open_index - 6
            elif open_index >= 5 and raw[open_index - 5:open_index].lower() == 'page:':
                scope = 'page'
                scope_start = open_index - 5

            if scope is not None and scope != runtime_field:
                continue
            if scope_start == 0 and index == len(raw):
                continue

            inner_query = raw[open_index + 1:close_index].strip()
            if inner_query == '':
                continue
            out.append((inner_query, boost))
            continue
        index += 1
    return out


def f32_mul_python(left: float, right: float) -> float:
    return to_float32_reference(to_float32_reference(left) * to_float32_reference(right))


def f32_add_python(left: float, right: float) -> float:
    return to_float32_reference(to_float32_reference(left) + to_float32_reference(right))


def long_to_int4(value: int) -> int:
    if value < 0:
        raise ValueError('long_to_int4 supports only non-negative values')
    num_bits = value.bit_length()
    if num_bits < 4:
        return value
    shift = num_bits - 4
    encoded = (value >> shift) & 0x07
    encoded |= (shift + 1) << 3
    return encoded


def int4_to_long(value: int) -> int:
    bits = value & 0x07
    shift = (value >> 3) - 1
    if shift == -1:
        return bits
    return (bits | 0x08) << shift


MAX_INT4 = long_to_int4((1 << 31) - 1)
NUM_FREE_VALUES = 255 - MAX_INT4


def int_to_byte4(value: int) -> int:
    if value < 0:
        raise ValueError('int_to_byte4 supports only non-negative values')
    if value < NUM_FREE_VALUES:
        return value
    return NUM_FREE_VALUES + long_to_int4(value - NUM_FREE_VALUES)


def byte4_to_int(value: int) -> int:
    if value < NUM_FREE_VALUES:
        return value
    return NUM_FREE_VALUES + int4_to_long(value - NUM_FREE_VALUES)


def build_norm_inverse_cache_python(k1: float, b: float, avgdl: float) -> list[float]:
    k1_f32 = to_float32_reference(k1)
    b_f32 = to_float32_reference(b)
    avgdl_f32 = to_float32_reference(avgdl)
    if avgdl_f32 <= 0.0:
        return [0.0] * 256
    out: list[float] = []
    one_minus_b = to_float32_reference(1.0 - b_f32)
    for idx in range(256):
        doc_len = to_float32_reference(float(byte4_to_int(idx)))
        scaled = to_float32_reference(to_float32_reference(b_f32 * doc_len) / avgdl_f32)
        norm = to_float32_reference(one_minus_b + scaled)
        denom = to_float32_reference(k1_f32 * norm)
        out.append(0.0 if denom <= 0.0 else to_float32_reference(1.0 / denom))
    return out


def lucene_term_score_python(weight: float, tf: float, doc_len: int, k1: float, b: float, avgdl: float) -> float:
    weight_f32 = to_float32_reference(weight)
    tf_f32 = to_float32_reference(tf)
    k1_f32 = to_float32_reference(k1)
    b_f32 = to_float32_reference(b)
    avgdl_f32 = to_float32_reference(avgdl)
    if weight_f32 == 0.0 or tf_f32 <= 0.0 or k1_f32 <= 0.0 or avgdl_f32 <= 0.0:
        return 0.0
    norm_byte = int_to_byte4(max(0, int(doc_len)))
    doc_len_f32 = to_float32_reference(float(byte4_to_int(norm_byte)))
    one_minus_b = to_float32_reference(1.0 - b_f32)
    scaled = to_float32_reference(to_float32_reference(b_f32 * doc_len_f32) / avgdl_f32)
    norm = to_float32_reference(one_minus_b + scaled)
    denom = to_float32_reference(k1_f32 * norm)
    if denom <= 0.0:
        return 0.0
    norm_inverse = to_float32_reference(1.0 / denom)
    inner = to_float32_reference(1.0 + to_float32_reference(tf_f32 * norm_inverse))
    if inner <= 0.0:
        return 0.0
    return to_float32_reference(weight_f32 - to_float32_reference(weight_f32 / inner))


def lucene_idf_python(doc_count: int, doc_freq: int) -> float:
    doc_count_f32 = to_float32_reference(float(doc_count))
    doc_freq_f32 = to_float32_reference(float(doc_freq))
    if doc_count_f32 <= 0.0 or doc_freq_f32 <= 0.0:
        return 0.0
    return to_float32_reference(math.log(1.0 + (doc_count_f32 - doc_freq_f32 + 0.5) / (doc_freq_f32 + 0.5)))


def split_query_reference(query: str) -> list[tuple[str, str]]:
    tokens: list[tuple[str, str]] = []
    i = 0
    while i < len(query):
        ch = query[i]
        if ch.isspace():
            i += 1
            continue
        if ch in '()' and (not is_escaped_char_reference(query, i)):
            tokens.append(('paren', ch))
            i += 1
            if ch == ')':
                parsed_group_boost = parse_group_boost_suffix_reference(query, i)
                if parsed_group_boost is not None:
                    _boost_value, i = parsed_group_boost
            continue
        if ch in {'+', '-'} and i + 1 < len(query):
            field_group = parse_field_group_segment_reference(query, i + 1)
            if field_group is not None:
                scope, inner_query, boost, next_index = field_group
                tokens.append(('token', ch))
                if boost is None:
                    tokens.append(('field_group', f'{scope}\t{inner_query}'))
                else:
                    tokens.append(('field_group_boost', f'{scope}\t{boost}\t{inner_query}'))
                i = next_index
                continue

            if query[i + 1] == '"':
                phrase, i = read_quoted_segment_reference(query, i + 2)
                slop: int | None = None
                if i < len(query) and query[i] == '~':
                    i += 1
                    slop_start = i
                    while i < len(query) and query[i].isdigit():
                        i += 1
                    if i > slop_start:
                        slop = int(query[slop_start:i])
                if i < len(query) and query[i] == '^':
                    i += 1
                    while i < len(query) and (query[i].isdigit() or query[i] == '.'):
                        i += 1
                tokens.append(('token', ch))
                if slop is None:
                    tokens.append(('phrase', phrase))
                else:
                    tokens.append(('phrase_slop', f'{slop}\t{phrase}'))
                continue

            field_sep = find_unescaped_char_from_reference(query, ':', i + 1)
            if field_sep > i + 1:
                field_candidate = query[i + 1:field_sep]
                if field_candidate.lower() in {'page', 'title'} and field_sep + 1 < len(query) and query[field_sep + 1] == '"':
                    phrase, i = read_quoted_segment_reference(query, field_sep + 2)
                    slop = None
                    if i < len(query) and query[i] == '~':
                        i += 1
                        slop_start = i
                        while i < len(query) and query[i].isdigit():
                            i += 1
                        if i > slop_start:
                            slop = int(query[slop_start:i])
                    if i < len(query) and query[i] == '^':
                        i += 1
                        while i < len(query) and (query[i].isdigit() or query[i] == '.'):
                            i += 1
                    tokens.append(('token', ch))
                    if slop is None:
                        tokens.append(('field_phrase', f'{field_candidate}:{phrase}'))
                    else:
                        tokens.append(('field_phrase_slop', f'{field_candidate}:{slop}\t{phrase}'))
                    continue
        if ch == '"':
            phrase, i = read_quoted_segment_reference(query, i + 1)
            slop: int | None = None
            if i < len(query) and query[i] == '~':
                i += 1
                slop_start = i
                while i < len(query) and query[i].isdigit():
                    i += 1
                if i > slop_start:
                    slop = int(query[slop_start:i])
            if i < len(query) and query[i] == '^':
                i += 1
                while i < len(query) and (query[i].isdigit() or query[i] == '.'):
                    i += 1
            if slop is None:
                tokens.append(('phrase', phrase))
            else:
                tokens.append(('phrase_slop', f'{slop}\t{phrase}'))
            continue

        field_group = parse_field_group_segment_reference(query, i)
        if field_group is not None:
            scope, inner_query, boost, next_index = field_group
            if boost is None:
                tokens.append(('field_group', f'{scope}\t{inner_query}'))
            else:
                tokens.append(('field_group_boost', f'{scope}\t{boost}\t{inner_query}'))
            i = next_index
            continue

        field_sep = find_unescaped_char_from_reference(query, ':', i)
        if field_sep > i:
            field_candidate = query[i:field_sep]
            if field_candidate.lower() in {'page', 'title'} and field_sep + 1 < len(query) and query[field_sep + 1] == '"':
                phrase, i = read_quoted_segment_reference(query, field_sep + 2)
                slop = None
                if i < len(query) and query[i] == '~':
                    i += 1
                    slop_start = i
                    while i < len(query) and query[i].isdigit():
                        i += 1
                    if i > slop_start:
                        slop = int(query[slop_start:i])
                if i < len(query) and query[i] == '^':
                    i += 1
                    while i < len(query) and (query[i].isdigit() or query[i] == '.'):
                        i += 1
                if slop is None:
                    tokens.append(('field_phrase', f'{field_candidate}:{phrase}'))
                else:
                    tokens.append(('field_phrase_slop', f'{field_candidate}:{slop}\t{phrase}'))
                continue

        j = i
        while j < len(query) and (not query[j].isspace()):
            if query[j] == '\\' and j + 1 < len(query):
                j += 2
                continue
            if query[j] in '()':
                break
            j += 1
        tokens.append(('token', query[i:j]))
        i = j
    return tokens


def render_query_tokens_reference(tokens: list[tuple[str, str]]) -> str:
    def escape_quoted(value: str) -> str:
        return value.replace('\\', '\\\\').replace('"', '\\"')

    parts: list[str] = []
    for kind, value in tokens:
        if kind == 'paren':
            parts.append(value)
            continue
        if kind == 'token':
            parts.append(value)
            continue
        if kind == 'phrase':
            parts.append(f'"{escape_quoted(value)}"')
            continue
        if kind == 'phrase_slop':
            slop_raw, phrase = value.split('\t', 1)
            parts.append(f'"{escape_quoted(phrase)}"~{slop_raw}')
            continue
        if kind == 'field_phrase':
            scope, phrase = parse_scoped_token_reference(value)
            if scope is None:
                raise SearchCompileError(f'invalid field_phrase token: {value}')
            parts.append(f'{scope}:"{escape_quoted(phrase)}"')
            continue
        if kind == 'field_phrase_slop':
            scope, payload = parse_scoped_token_reference(value)
            if scope is None:
                raise SearchCompileError(f'invalid field_phrase_slop token: {value}')
            slop_raw, phrase = payload.split('\t', 1)
            parts.append(f'{scope}:"{escape_quoted(phrase)}"~{slop_raw}')
            continue
        if kind == 'field_group':
            scope, inner_query = value.split('\t', 1)
            parts.append(f'{scope}:({inner_query})')
            continue
        if kind == 'field_group_boost':
            scope, boost_raw, inner_query = value.split('\t', 2)
            parts.append(f'{scope}:({inner_query})^{boost_raw}')
            continue
        raise SearchCompileError(f'unsupported token kind while rendering: {kind}')
    return ' '.join(parts)


def _apply_add_ast_clause_reference(
    clauses: list[QueryAstClause],
    conjunction: int,
    modifier: int,
    node: QueryAstLeaf | QueryAstGroup,
) -> None:
    if clauses and conjunction == 1:
        prev = clauses[-1]
        if prev.occur != 'MUST_NOT':
            clauses[-1] = QueryAstClause(node=prev.node, occur='MUST')

    prohibited = (modifier == 1)
    required = (modifier == 2)
    if conjunction == 1 and not prohibited:
        required = True

    if required and (not prohibited):
        clauses.append(QueryAstClause(node=node, occur='MUST'))
        return
    if (not required) and (not prohibited):
        clauses.append(QueryAstClause(node=node, occur='SHOULD'))
        return
    if prohibited and (not required):
        clauses.append(QueryAstClause(node=node, occur='MUST_NOT'))
        return
    raise SearchCompileError('clause cannot be both required and prohibited')


def _parse_query_ast_sequence_reference(
    sequence_tokens: list[tuple[str, str]],
    start_index: int,
    stop_on_rparen: bool,
) -> tuple[list[QueryAstClause], int]:
    CONJ_NONE = 0
    CONJ_AND = 1
    CONJ_OR = 2
    MOD_NONE = 0
    MOD_NOT = 1
    MOD_REQ = 2

    clauses: list[QueryAstClause] = []
    index = start_index
    pending_conj = CONJ_NONE
    saw_operand = False

    while index < len(sequence_tokens):
        kind, value = sequence_tokens[index]
        if kind == 'paren' and value == ')' and stop_on_rparen:
            break

        if kind == 'token':
            upper = value.upper()
            if upper == 'AND':
                if not saw_operand:
                    raise SearchCompileError('dangling boolean operator')
                if pending_conj != CONJ_NONE:
                    raise SearchCompileError('consecutive boolean operators')
                pending_conj = CONJ_AND
                index += 1
                continue
            if upper == 'OR':
                if not saw_operand:
                    raise SearchCompileError('dangling boolean operator')
                if pending_conj != CONJ_NONE:
                    raise SearchCompileError('consecutive boolean operators')
                pending_conj = CONJ_OR
                index += 1
                continue

        modifier = MOD_NONE
        operand_kind = kind
        operand_value = value
        if operand_kind == 'token':
            upper = operand_value.upper()
            if upper == 'NOT':
                modifier = MOD_NOT
                index += 1
                if index >= len(sequence_tokens):
                    raise SearchCompileError('dangling NOT modifier')
                operand_kind, operand_value = sequence_tokens[index]
            elif operand_value == '+':
                modifier = MOD_REQ
                index += 1
                if index >= len(sequence_tokens):
                    raise SearchCompileError('dangling + modifier')
                operand_kind, operand_value = sequence_tokens[index]
            elif operand_value == '-':
                modifier = MOD_NOT
                index += 1
                if index >= len(sequence_tokens):
                    raise SearchCompileError('dangling - modifier')
                operand_kind, operand_value = sequence_tokens[index]
            elif operand_value.startswith('+'):
                modifier = MOD_REQ
                operand_kind = 'token'
                operand_value = operand_value[1:]
            elif operand_value.startswith('-'):
                modifier = MOD_NOT
                operand_kind = 'token'
                operand_value = operand_value[1:]

        if modifier != MOD_NONE and operand_kind == 'token' and operand_value.upper() in BOOL_OPS:
            raise SearchCompileError('dangling clause modifier before boolean operator')
        if modifier != MOD_NONE and operand_kind == 'token' and operand_value == '':
            raise SearchCompileError('dangling clause modifier with empty operand')
        if modifier != MOD_NONE and operand_kind == 'paren' and operand_value == ')':
            raise SearchCompileError('dangling clause modifier before closing parenthesis')

        if operand_kind == 'paren':
            if operand_value == '(':
                group_clauses, next_index = _parse_query_ast_sequence_reference(sequence_tokens, index + 1, True)
                if next_index >= len(sequence_tokens) or sequence_tokens[next_index] != ('paren', ')'):
                    raise SearchCompileError('unclosed group in query')
                node: QueryAstLeaf | QueryAstGroup = QueryAstGroup(clauses=tuple(group_clauses))
                index = next_index + 1
            else:
                if stop_on_rparen:
                    break
                raise SearchCompileError('unmatched closing parenthesis')
        else:
            if operand_kind == 'token' and operand_value == '':
                raise SearchCompileError('empty clause token')
            node = QueryAstLeaf(kind=operand_kind, value=operand_value)
            index += 1

        _apply_add_ast_clause_reference(clauses, pending_conj, modifier, node)
        pending_conj = CONJ_NONE
        saw_operand = True

    if pending_conj != CONJ_NONE:
        raise SearchCompileError('dangling boolean operator')
    return clauses, index


@lru_cache(maxsize=4096)
def _parse_query_ast_cached_reference(query: str) -> QueryAst:
    clauses, _end = _parse_query_ast_sequence_reference(split_query_reference(query), 0, False)
    return QueryAst(clauses=tuple(clauses))


def parse_query_ast_reference(query: str) -> QueryAst:
    return _parse_query_ast_cached_reference(query)


def query_ast_to_debug_reference(ast: QueryAst) -> dict:
    def node_to_debug(node: QueryAstLeaf | QueryAstGroup) -> dict:
        if isinstance(node, QueryAstGroup):
            return {'kind': 'group', 'clauses': [clause_to_debug(clause) for clause in node.clauses]}
        return {'kind': node.kind, 'value': node.value}

    def clause_to_debug(clause: QueryAstClause) -> dict:
        return {'occur': clause.occur, 'node': node_to_debug(clause.node)}

    return {'clauses': [clause_to_debug(clause) for clause in ast.clauses]}


def query_ast_from_debug_reference(payload: dict) -> QueryAst:
    def parse_node(node: dict) -> QueryAstLeaf | QueryAstGroup:
        kind = str(node.get('kind', ''))
        if kind == '':
            raise SearchCompileError('invalid AST payload: node.kind is required')
        if kind == 'group':
            clauses_raw = node.get('clauses')
            if not isinstance(clauses_raw, list):
                raise SearchCompileError('invalid AST payload: group node requires clauses[]')
            clauses = tuple(parse_clause(item) for item in clauses_raw)
            return QueryAstGroup(clauses=clauses)
        value = node.get('value')
        if value is None:
            raise SearchCompileError('invalid AST payload: leaf node requires value')
        return QueryAstLeaf(kind=kind, value=str(value))

    def parse_clause(clause: dict) -> QueryAstClause:
        occur = str(clause.get('occur', ''))
        node_raw = clause.get('node')
        if occur == '' or (not isinstance(node_raw, dict)):
            raise SearchCompileError('invalid AST payload: clause requires occur and node')
        return QueryAstClause(node=parse_node(node_raw), occur=occur)

    clauses_raw = payload.get('clauses')
    if not isinstance(clauses_raw, list):
        raise SearchCompileError('invalid AST payload: root requires clauses[]')
    return QueryAst(clauses=tuple(parse_clause(item) for item in clauses_raw))


def compose_clause_occur_reference(parent_occur: str, clause_occur: str) -> str:
    if parent_occur == 'MUST_NOT':
        return 'MUST_NOT'
    if parent_occur == 'MUST':
        if clause_occur == 'MUST_NOT':
            return 'MUST_NOT'
        return clause_occur
    return clause_occur


@lru_cache(maxsize=4096)
def _iter_query_ast_leaves_cached_reference(query: str) -> tuple[tuple[QueryAstLeaf, str], ...]:
    ast = parse_query_ast_reference(query)
    out: list[tuple[QueryAstLeaf, str]] = []

    def walk_node(node: QueryAstLeaf | QueryAstGroup, occur: str) -> None:
        if isinstance(node, QueryAstGroup):
            for clause in node.clauses:
                walk_node(clause.node, compose_clause_occur_reference(occur, clause.occur))
            return
        out.append((node, occur))

    for clause in ast.clauses:
        walk_node(clause.node, clause.occur)
    return tuple(out)


def iter_query_ast_leaves_reference(query: str) -> list[tuple[QueryAstLeaf, str]]:
    return list(_iter_query_ast_leaves_cached_reference(query))
