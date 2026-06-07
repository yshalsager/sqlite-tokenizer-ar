#!/usr/bin/env python3
import argparse
from bisect import bisect_right
from collections import OrderedDict
import json
import re
import sqlite3
from dataclasses import dataclass
from pathlib import Path


BOOL_OPS = {'AND', 'OR', 'NOT'}
EMPTY_TERM_SENTINEL = '\ue000'
SOURCE_POSITIONS_CACHE_MAX = 50000
BOOST_NUMBER_RE = r'[0-9]+(?:\.[0-9]+)?'
QUOTED_QUERY_CONTENT_RE = r'(?:\\.|[^"\\])*'
BOOSTED_PHRASE_RE = re.compile(
    rf'(?:(?P<scope>page|title):)?"(?P<phrase>{QUOTED_QUERY_CONTENT_RE})"(?:~(?P<slop>[0-9]+))?(?<!\\)\^(?P<boost>{BOOST_NUMBER_RE})',
    flags=re.IGNORECASE,
)

class SearchCompileError(Exception):
    pass


@dataclass
class SearchOptions:
    allow_prefix_search: bool = True
    allow_suffix_search: bool = True
    allow_wildcard_search: bool = True
    ignore_diacritics: bool = True
    ignore_hamza_forms: bool = True
    ignore_letter_forms: bool | None = None
    ignore_digit_forms: bool = True
    suffix_max_expansions: int = 256
    wildcard_max_expansions: int = 256
    allow_fuzzy_search: bool = True
    fuzzy_max_expansions: int = 128
    lenient_parse_errors: bool = False

    def __post_init__(self) -> None:
        if self.ignore_letter_forms is None:
            self.ignore_letter_forms = self.ignore_hamza_forms


@dataclass
class StrictLiteral:
    text: str
    is_phrase: bool
    is_pattern: bool
    required: bool
    prohibited: bool


@dataclass(frozen=True)
class QueryAstLeaf:
    kind: str
    value: str


@dataclass(frozen=True)
class QueryAstGroup:
    clauses: tuple['QueryAstClause', ...]


@dataclass(frozen=True)
class QueryAstClause:
    node: QueryAstLeaf | QueryAstGroup
    occur: str


@dataclass(frozen=True)
class QueryAst:
    clauses: tuple[QueryAstClause, ...]


@dataclass(frozen=True)
class SqliteExecutionPlan:
    match_expression: str


def is_escaped_char(value: str, index: int) -> bool:
    from python_reference_helpers import is_escaped_char_reference

    return is_escaped_char_reference(value, index)


def read_quoted_segment(value: str, start: int) -> tuple[str, int]:
    from python_reference_helpers import read_quoted_segment_reference

    return read_quoted_segment_reference(value, start)


def has_unescaped_char(value: str, target: str) -> bool:
    from python_reference_helpers import has_unescaped_char_reference

    return has_unescaped_char_reference(value, target)


def find_unescaped_char_from(value: str, target: str, start: int) -> int:
    from python_reference_helpers import find_unescaped_char_from_reference

    return find_unescaped_char_from_reference(value, target, start)


def find_unescaped_char(value: str, target: str) -> int:
    from python_reference_helpers import find_unescaped_char_reference

    return find_unescaped_char_reference(value, target)


def has_unescaped_wildcard(value: str) -> bool:
    from python_reference_helpers import has_unescaped_wildcard_reference

    return has_unescaped_wildcard_reference(value)


def unescape_query_escapes(value: str) -> str:
    from python_reference_helpers import unescape_query_escapes_reference

    return unescape_query_escapes_reference(value)


def parse_boost_number(value: str) -> float | None:
    from python_reference_helpers import parse_boost_number_reference

    return parse_boost_number_reference(value)


def parse_group_boost_suffix(query: str, start: int) -> tuple[float, int] | None:
    from python_reference_helpers import parse_group_boost_suffix_reference

    return parse_group_boost_suffix_reference(query, start)


def parse_field_group_segment(query: str, start: int) -> tuple[str, str, float | None, int] | None:
    from python_reference_helpers import parse_field_group_segment_reference

    return parse_field_group_segment_reference(query, start)


def split_query(query: str) -> list[tuple[str, str]]:
    from python_reference_helpers import split_query_reference

    return split_query_reference(query)


def render_query_tokens(tokens: list[tuple[str, str]]) -> str:
    from python_reference_helpers import render_query_tokens_reference

    return render_query_tokens_reference(tokens)


def parse_query_ast(query: str) -> QueryAst:
    from python_reference_helpers import parse_query_ast_reference

    return parse_query_ast_reference(query)


def query_ast_to_debug(ast: QueryAst) -> dict:
    from python_reference_helpers import query_ast_to_debug_reference

    return query_ast_to_debug_reference(ast)


def parse_query_ast_c_backend(conn: sqlite3.Connection, query: str) -> dict:
    try:
        row = conn.execute('SELECT sqlite_tokenizer_ar_parse_query_ast_json(?)', (query,)).fetchone()
    except sqlite3.OperationalError as exc:
        message = str(exc)
        if 'no such function: sqlite_tokenizer_ar_parse_query_ast_json' in message:
            raise SearchCompileError(
                'C AST backend is not available: missing UDF sqlite_tokenizer_ar_parse_query_ast_json'
            ) from exc
        if message in {
            'dangling field scope',
            'unclosed quote in query',
            'unclosed group in query',
            'unclosed field-group parentheses in query',
            'unmatched closing parenthesis',
            'dangling boolean operator',
        }:
            raise SearchCompileError(message) from exc
        raise SearchCompileError(f'C AST backend execution failed: {message}') from exc
    if row is None or row[0] is None:
        raise SearchCompileError('C AST backend returned no result')
    try:
        payload = json.loads(str(row[0]))
    except json.JSONDecodeError as exc:
        raise SearchCompileError('C AST backend returned invalid JSON payload') from exc
    if not isinstance(payload, dict):
        raise SearchCompileError('C AST backend payload must be a JSON object')
    return payload


def parse_query_ast_backend(conn: sqlite3.Connection, query: str, backend: str) -> dict:
    backend_key = backend.strip().lower()
    if backend_key == 'python':
        return query_ast_to_debug(parse_query_ast(query))
    if backend_key == 'c':
        return parse_query_ast_c_backend(conn, query)
    raise SearchCompileError(f'unsupported backend: {backend}')


def query_ast_from_debug(payload: dict) -> QueryAst:
    from python_reference_helpers import query_ast_from_debug_reference

    return query_ast_from_debug_reference(payload)


def parse_query_ast_with_udf(conn: sqlite3.Connection, query: str) -> QueryAst:
    return query_ast_from_debug(parse_query_ast_c_backend(conn, query))


def compose_clause_occur(parent_occur: str, clause_occur: str) -> str:
    from python_reference_helpers import compose_clause_occur_reference

    return compose_clause_occur_reference(parent_occur, clause_occur)


def iter_query_ast_leaves(query: str) -> list[tuple[QueryAstLeaf, str]]:
    from python_reference_helpers import iter_query_ast_leaves_reference

    return iter_query_ast_leaves_reference(query)


def iter_query_ast_leaves_c_backend(conn: sqlite3.Connection, query: str) -> list[tuple[QueryAstLeaf, str]]:
    try:
        row = conn.execute('SELECT sqlite_tokenizer_ar_iter_query_ast_leaves_json(?)', (query,)).fetchone()
    except sqlite3.OperationalError as exc:
        message = str(exc)
        if 'no such function: sqlite_tokenizer_ar_iter_query_ast_leaves_json' in message:
            raise SearchCompileError(
                'C AST leaves backend is not available: missing UDF sqlite_tokenizer_ar_iter_query_ast_leaves_json'
            ) from exc
        raise SearchCompileError(f'C AST leaves backend execution failed: {message}') from exc
    if row is None or row[0] is None:
        raise SearchCompileError('C AST leaves backend returned no result')
    try:
        payload = json.loads(str(row[0]))
    except json.JSONDecodeError as exc:
        raise SearchCompileError('C AST leaves backend returned invalid JSON payload') from exc
    if not isinstance(payload, list):
        raise SearchCompileError('C AST leaves backend payload must be a JSON array')
    out: list[tuple[QueryAstLeaf, str]] = []
    for item in payload:
        if not isinstance(item, dict):
            raise SearchCompileError('C AST leaves backend payload items must be JSON objects')
        occur = str(item.get('occur', ''))
        kind = str(item.get('kind', ''))
        value = str(item.get('value', ''))
        if occur == '' or kind == '':
            raise SearchCompileError('C AST leaves backend payload item must include occur/kind/value')
        out.append((QueryAstLeaf(kind=kind, value=value), occur))
    return out


def iter_query_ast_leaves_with_udf(conn: sqlite3.Connection, query: str) -> list[tuple[QueryAstLeaf, str]]:
    return iter_query_ast_leaves_c_backend(conn, query)


def analyze_text_with_udf(conn: sqlite3.Connection, text: str) -> list[str]:
    raw = text.strip()
    if raw == '' or raw.upper() in BOOL_OPS:
        return []
    row = conn.execute('SELECT sqlite_tokenizer_ar_analyze_json(?)', (raw,)).fetchone()
    if row is None or row[0] in {None, ''}:
        return []
    payload = json.loads(str(row[0]))
    if not isinstance(payload, list):
        return []
    return [str(item) for item in payload]


def analyze_positions_with_udf(conn: sqlite3.Connection, text: str) -> list[tuple[str, int]]:
    raw = text.strip()
    if raw == '' or raw.upper() in BOOL_OPS:
        return []
    try:
        row = conn.execute('SELECT sqlite_tokenizer_ar_analyze_positions_json(?)', (raw,)).fetchone()
    except sqlite3.OperationalError:
        return [(term, idx) for idx, term in enumerate(analyze_text_with_udf(conn, raw))]
    if row is None or row[0] in {None, ''}:
        return []
    payload = json.loads(str(row[0]))
    if not isinstance(payload, list):
        return []
    out: list[tuple[str, int]] = []
    for item in payload:
        if not isinstance(item, dict):
            continue
        term = item.get('term')
        position = item.get('position')
        if term is None or position is None:
            continue
        try:
            out.append((str(term), int(position)))
        except (TypeError, ValueError):
            continue
    return out


def analyze_phrase_terms_with_positions(conn: sqlite3.Connection, phrase_text: str) -> tuple[list[str], list[int]]:
    from python_reference_helpers import analyze_phrase_terms_with_positions_reference

    return analyze_phrase_terms_with_positions_reference(conn, phrase_text)


def normalize_text_with_udf(
    conn: sqlite3.Connection,
    value: str,
    options: SearchOptions,
    *,
    lowercase: bool,
    keep_wildcards: bool,
    trim: bool,
) -> str:
    row = conn.execute(
        'SELECT sqlite_tokenizer_ar_normalize(?, ?, ?, ?, ?, ?, ?)',
        (
            value,
            1 if options.ignore_diacritics else 0,
            1 if options.ignore_hamza_forms else 0,
            1 if bool(options.ignore_letter_forms) else 0,
            1 if options.ignore_digit_forms else 0,
            1 if lowercase else 0,
            1 if keep_wildcards else 0,
        ),
    ).fetchone()
    normalized = '' if row is None or row[0] is None else str(row[0])
    return normalized.strip() if trim else normalized


def normalize_query_text(conn: sqlite3.Connection, value: str, options: SearchOptions) -> str:
    from python_reference_helpers import normalize_query_text_reference

    return normalize_query_text_reference(conn, value, options)


def parse_scoped_token(token: str) -> tuple[str | None, str]:
    from python_reference_helpers import parse_scoped_token_reference

    return parse_scoped_token_reference(token)


def parse_scoped_token_c_backend(conn: sqlite3.Connection, token: str) -> tuple[str | None, str]:
    try:
        row = conn.execute('SELECT sqlite_tokenizer_ar_parse_scoped_token_json(?)', (token,)).fetchone()
    except sqlite3.OperationalError as exc:
        message = str(exc)
        if 'no such function: sqlite_tokenizer_ar_parse_scoped_token_json' in message:
            raise SearchCompileError(
                'C token helper backend is not available: missing UDF sqlite_tokenizer_ar_parse_scoped_token_json'
            ) from exc
        raise SearchCompileError(f'C token helper backend execution failed: {message}') from exc
    if row is None or row[0] is None:
        raise SearchCompileError('C token helper backend returned no result for parse_scoped_token')
    try:
        payload = json.loads(str(row[0]))
    except json.JSONDecodeError as exc:
        raise SearchCompileError('C token helper backend returned invalid JSON payload for parse_scoped_token') from exc
    if not isinstance(payload, dict):
        raise SearchCompileError('C token helper payload must be a JSON object for parse_scoped_token')
    scope_raw = payload.get('scope')
    raw = payload.get('raw')
    if raw is None:
        raise SearchCompileError('C token helper payload must include raw for parse_scoped_token')
    scope = None if scope_raw is None else str(scope_raw)
    return scope, str(raw)


def parse_scoped_token_with_udf(conn: sqlite3.Connection, token: str) -> tuple[str | None, str]:
    return parse_scoped_token_c_backend(conn, token)


def parse_fuzzy_token(token: str) -> tuple[str, int] | None:
    from python_reference_helpers import parse_fuzzy_token_reference

    return parse_fuzzy_token_reference(token)


def parse_fuzzy_token_c_backend(conn: sqlite3.Connection, token: str) -> tuple[str, int] | None:
    try:
        row = conn.execute('SELECT sqlite_tokenizer_ar_parse_fuzzy_token_json(?)', (token,)).fetchone()
    except sqlite3.OperationalError as exc:
        message = str(exc)
        if 'no such function: sqlite_tokenizer_ar_parse_fuzzy_token_json' in message:
            raise SearchCompileError(
                'C token helper backend is not available: missing UDF sqlite_tokenizer_ar_parse_fuzzy_token_json'
            ) from exc
        raise SearchCompileError(f'C token helper backend execution failed: {message}') from exc
    if row is None or row[0] is None:
        return None
    try:
        payload = json.loads(str(row[0]))
    except json.JSONDecodeError as exc:
        raise SearchCompileError('C token helper backend returned invalid JSON payload for parse_fuzzy_token') from exc
    if not isinstance(payload, dict):
        raise SearchCompileError('C token helper payload must be a JSON object for parse_fuzzy_token')
    base = payload.get('base')
    edits = payload.get('edits')
    if base is None or edits is None:
        raise SearchCompileError('C token helper payload must include base/edits for parse_fuzzy_token')
    try:
        edits_value = int(edits)
    except (TypeError, ValueError) as exc:
        raise SearchCompileError('C token helper payload has invalid edits for parse_fuzzy_token') from exc
    return str(base), edits_value


def parse_fuzzy_token_with_udf(conn: sqlite3.Connection, token: str) -> tuple[str, int] | None:
    return parse_fuzzy_token_c_backend(conn, token)


def parse_top_level_group_boost_c_backend(conn: sqlite3.Connection, query: str) -> tuple[str, float] | None:
    try:
        row = conn.execute('SELECT sqlite_tokenizer_ar_parse_top_level_group_boost_json(?)', (query,)).fetchone()
    except sqlite3.OperationalError as exc:
        message = str(exc)
        if 'no such function: sqlite_tokenizer_ar_parse_top_level_group_boost_json' in message:
            raise SearchCompileError(
                'C token helper backend is not available: missing UDF sqlite_tokenizer_ar_parse_top_level_group_boost_json'
            ) from exc
        raise SearchCompileError(f'C token helper backend execution failed: {message}') from exc
    if row is None or row[0] is None:
        return None
    try:
        payload = json.loads(str(row[0]))
    except json.JSONDecodeError as exc:
        raise SearchCompileError(
            'C token helper backend returned invalid JSON payload for parse_top_level_group_boost'
        ) from exc
    if not isinstance(payload, dict):
        raise SearchCompileError('C token helper payload must be a JSON object for parse_top_level_group_boost')
    inner = payload.get('inner')
    boost = payload.get('boost')
    if inner is None or boost is None:
        raise SearchCompileError('C token helper payload must include inner/boost for parse_top_level_group_boost')
    try:
        boost_value = float(boost)
    except (TypeError, ValueError) as exc:
        raise SearchCompileError('C token helper payload has invalid boost for parse_top_level_group_boost') from exc
    return str(inner), boost_value


def parse_top_level_group_boost_with_udf(conn: sqlite3.Connection, query: str) -> tuple[str, float] | None:
    return parse_top_level_group_boost_c_backend(conn, query)


def parse_whole_scoped_group_c_backend(conn: sqlite3.Connection, query: str) -> tuple[str, str, float | None] | None:
    try:
        row = conn.execute('SELECT sqlite_tokenizer_ar_parse_whole_scoped_group_json(?)', (query,)).fetchone()
    except sqlite3.OperationalError as exc:
        message = str(exc)
        if 'no such function: sqlite_tokenizer_ar_parse_whole_scoped_group_json' in message:
            raise SearchCompileError(
                'C token helper backend is not available: missing UDF sqlite_tokenizer_ar_parse_whole_scoped_group_json'
            ) from exc
        raise SearchCompileError(f'C token helper backend execution failed: {message}') from exc
    if row is None or row[0] is None:
        return None
    try:
        payload = json.loads(str(row[0]))
    except json.JSONDecodeError as exc:
        raise SearchCompileError('C token helper backend returned invalid JSON payload for parse_whole_scoped_group') from exc
    if not isinstance(payload, dict):
        raise SearchCompileError('C token helper payload must be a JSON object for parse_whole_scoped_group')
    scope = payload.get('scope')
    inner = payload.get('inner')
    if scope is None or inner is None:
        raise SearchCompileError('C token helper payload must include scope/inner for parse_whole_scoped_group')
    boost_raw = payload.get('boost')
    if boost_raw is None:
        return str(scope), str(inner), None
    try:
        boost = float(boost_raw)
    except (TypeError, ValueError) as exc:
        raise SearchCompileError('C token helper payload has invalid boost for parse_whole_scoped_group') from exc
    return str(scope), str(inner), boost


def parse_whole_scoped_group_with_udf(conn: sqlite3.Connection, query: str) -> tuple[str, str, float | None] | None:
    return parse_whole_scoped_group_c_backend(conn, query)


def preprocess_rank_boost_c_backend(conn: sqlite3.Connection, query: str, runtime_field: str) -> tuple[float, str] | None:
    try:
        row = conn.execute('SELECT sqlite_tokenizer_ar_preprocess_rank_boost_json(?, ?)', (query, runtime_field)).fetchone()
    except sqlite3.OperationalError as exc:
        message = str(exc)
        if 'no such function: sqlite_tokenizer_ar_preprocess_rank_boost_json' in message:
            raise SearchCompileError(
                'C token helper backend is not available: missing UDF sqlite_tokenizer_ar_preprocess_rank_boost_json'
            ) from exc
        raise SearchCompileError(f'C token helper backend execution failed: {message}') from exc
    if row is None or row[0] is None:
        return None
    try:
        payload = json.loads(str(row[0]))
    except json.JSONDecodeError as exc:
        raise SearchCompileError('C token helper backend returned invalid JSON payload for preprocess_rank_boost') from exc
    if not isinstance(payload, dict):
        raise SearchCompileError('C token helper payload must be a JSON object for preprocess_rank_boost')
    boost_factor = payload.get('boost_factor')
    ranking_query = payload.get('ranking_query')
    if boost_factor is None or ranking_query is None:
        raise SearchCompileError('C token helper payload must include boost_factor/ranking_query for preprocess_rank_boost')
    try:
        boost_factor_value = float(boost_factor)
    except (TypeError, ValueError) as exc:
        raise SearchCompileError('C token helper payload has invalid boost_factor for preprocess_rank_boost') from exc
    return boost_factor_value, str(ranking_query)


def preprocess_rank_boost_with_udf(conn: sqlite3.Connection, query: str, runtime_field: str) -> tuple[float, str] | None:
    return preprocess_rank_boost_c_backend(conn, query, runtime_field)


def parse_boost_factor_for_field(conn: sqlite3.Connection, query: str, runtime_field: str) -> float:
    raw = query.strip()
    if raw == '':
        return 1.0

    preprocessed = preprocess_rank_boost_with_udf(conn, raw, runtime_field)
    if preprocessed is not None:
        boost_factor, _ranking_query = preprocessed
        return boost_factor

    whole_group = parse_whole_scoped_group_with_udf(conn, raw)
    if whole_group is not None:
        scope, _inner_query, boost = whole_group
        if boost is not None:
            return boost if scope == runtime_field else 1.0

    single_phrase = parse_single_phrase_clause_with_udf(conn, raw)
    if single_phrase is not None:
        scope, _phrase, _slop, boost = single_phrase
        if scope is None:
            return boost
        return boost if scope == runtime_field else 1.0

    if all((not ch.isspace()) for ch in raw):
        scope, scoped_raw = parse_scoped_token_with_udf(conn, raw)
        leaf_base, leaf_boost = strip_boost_with_udf(conn, scoped_raw)
        if leaf_boost is not None:
            if scope is None:
                return float(leaf_boost)
            return float(leaf_boost) if scope == runtime_field else 1.0

    top_group = parse_top_level_group_boost_with_udf(conn, raw)
    if top_group is not None:
        _inner, boost = top_group
        return boost

    return 1.0


def strip_boost_for_ranking(conn: sqlite3.Connection, query: str, runtime_field: str) -> str:
    raw = query.strip()
    if raw == '':
        return raw

    preprocessed = preprocess_rank_boost_with_udf(conn, raw, runtime_field)
    if preprocessed is not None:
        _boost_factor, ranking_query = preprocessed
        return ranking_query

    whole_group = parse_whole_scoped_group_with_udf(conn, raw)
    if whole_group is not None:
        scope, inner_query, _boost = whole_group
        return inner_query if scope == runtime_field else '__nohit__'

    single_phrase = parse_single_phrase_clause_with_udf(conn, raw)
    if single_phrase is not None:
        scope, phrase, slop, boost = single_phrase
        if abs(boost - 1.0) < 1e-12:
            return raw
        phrase_base = '"' + phrase.replace('\\', '\\\\').replace('"', '\\"') + '"'
        if slop > 0:
            phrase_base += f'~{slop}'
        if scope is None:
            return phrase_base
        if scope == runtime_field:
            return f'{scope}:{phrase_base}'
        return raw

    if all((not ch.isspace()) for ch in raw):
        scope, scoped_raw = parse_scoped_token_with_udf(conn, raw)
        leaf_base, leaf_boost = strip_boost_with_udf(conn, scoped_raw)
        if leaf_boost is not None:
            if scope is None:
                return leaf_base
            if scope == runtime_field:
                return f'{scope}:{leaf_base}'
            return raw

    top_group = parse_top_level_group_boost_with_udf(conn, raw)
    if top_group is not None:
        inner, _boost = top_group
        return strip_boost_for_ranking(conn, inner, runtime_field)

    return raw


def strip_boost(token: str) -> tuple[str, float | None]:
    from python_reference_helpers import strip_boost_reference

    return strip_boost_reference(token)


def strip_boost_c_backend(conn: sqlite3.Connection, token: str) -> tuple[str, float | None]:
    try:
        row = conn.execute('SELECT sqlite_tokenizer_ar_strip_boost_json(?)', (token,)).fetchone()
    except sqlite3.OperationalError as exc:
        message = str(exc)
        if 'no such function: sqlite_tokenizer_ar_strip_boost_json' in message:
            raise SearchCompileError(
                'C token helper backend is not available: missing UDF sqlite_tokenizer_ar_strip_boost_json'
            ) from exc
        raise SearchCompileError(f'C token helper backend execution failed: {message}') from exc
    if row is None or row[0] is None:
        raise SearchCompileError('C token helper backend returned no result for strip_boost')
    try:
        payload = json.loads(str(row[0]))
    except json.JSONDecodeError as exc:
        raise SearchCompileError('C token helper backend returned invalid JSON payload for strip_boost') from exc
    if not isinstance(payload, dict):
        raise SearchCompileError('C token helper payload must be a JSON object for strip_boost')
    base = payload.get('base')
    if base is None:
        raise SearchCompileError('C token helper payload must include base for strip_boost')
    boost_raw = payload.get('boost')
    if boost_raw is None:
        return str(base), None
    try:
        boost = float(boost_raw)
    except (TypeError, ValueError) as exc:
        raise SearchCompileError('C token helper payload has invalid boost for strip_boost') from exc
    return str(base), boost


def strip_boost_with_udf(conn: sqlite3.Connection, token: str) -> tuple[str, float | None]:
    return strip_boost_c_backend(conn, token)


def parse_boosted_token_clause_c_backend(conn: sqlite3.Connection, token: str, runtime_field: str) -> dict | None:
    try:
        row = conn.execute('SELECT sqlite_tokenizer_ar_parse_boosted_token_clause_json(?, ?)', (token, runtime_field)).fetchone()
    except sqlite3.OperationalError as exc:
        message = str(exc)
        if 'no such function: sqlite_tokenizer_ar_parse_boosted_token_clause_json' in message:
            raise SearchCompileError(
                'C token helper backend is not available: missing UDF sqlite_tokenizer_ar_parse_boosted_token_clause_json'
            ) from exc
        raise SearchCompileError(f'C token helper backend execution failed: {message}') from exc
    if row is None or row[0] is None:
        return None
    try:
        payload = json.loads(str(row[0]))
    except json.JSONDecodeError as exc:
        raise SearchCompileError('C token helper backend returned invalid JSON payload for parse_boosted_token_clause') from exc
    if not isinstance(payload, dict):
        raise SearchCompileError('C token helper payload must be a JSON object for parse_boosted_token_clause')
    scope = payload.get('scope')
    raw = payload.get('raw')
    boost_raw = payload.get('boost')
    has_wildcard = payload.get('has_wildcard')
    is_fuzzy = payload.get('is_fuzzy')
    if raw is None or boost_raw is None or has_wildcard is None or is_fuzzy is None:
        raise SearchCompileError(
            'C token helper payload must include raw/boost/has_wildcard/is_fuzzy for parse_boosted_token_clause'
        )
    if scope is not None and str(scope).lower() not in {'page', 'title'}:
        raise SearchCompileError('C token helper payload has invalid scope for parse_boosted_token_clause')
    try:
        boost = float(boost_raw)
    except (TypeError, ValueError) as exc:
        raise SearchCompileError('C token helper payload has invalid boost for parse_boosted_token_clause') from exc
    return {
        'scope': None if scope is None else str(scope).lower(),
        'raw': str(raw),
        'boost': boost,
        'has_wildcard': bool(has_wildcard),
        'is_fuzzy': bool(is_fuzzy),
    }


def parse_boosted_token_clause_with_udf(conn: sqlite3.Connection, token: str, runtime_field: str) -> dict | None:
    return parse_boosted_token_clause_c_backend(conn, token, runtime_field)


def parse_simple_boolean_c_backend(conn: sqlite3.Connection, query: str) -> tuple[str, str, float, str, float] | None:
    try:
        row = conn.execute('SELECT sqlite_tokenizer_ar_parse_simple_boolean_json(?)', (query,)).fetchone()
    except sqlite3.OperationalError as exc:
        message = str(exc)
        if 'no such function: sqlite_tokenizer_ar_parse_simple_boolean_json' in message:
            raise SearchCompileError(
                'C token helper backend is not available: missing UDF sqlite_tokenizer_ar_parse_simple_boolean_json'
            ) from exc
        raise SearchCompileError(f'C token helper backend execution failed: {message}') from exc
    if row is None or row[0] is None:
        return None
    try:
        payload = json.loads(str(row[0]))
    except json.JSONDecodeError as exc:
        raise SearchCompileError(
            'C token helper backend returned invalid JSON payload for parse_simple_boolean'
        ) from exc
    if not isinstance(payload, dict):
        raise SearchCompileError('C token helper payload must be a JSON object for parse_simple_boolean')
    op = payload.get('op')
    left = payload.get('left')
    right = payload.get('right')
    left_boost = payload.get('left_boost')
    right_boost = payload.get('right_boost')
    if op is None or left is None or right is None or left_boost is None or right_boost is None:
        raise SearchCompileError(
            'C token helper payload must include op/left/right/left_boost/right_boost for parse_simple_boolean'
        )
    op_upper = str(op).upper()
    if op_upper not in {'AND', 'OR'}:
        raise SearchCompileError('C token helper payload has invalid op for parse_simple_boolean')
    try:
        left_boost_value = float(left_boost)
        right_boost_value = float(right_boost)
    except (TypeError, ValueError) as exc:
        raise SearchCompileError('C token helper payload has invalid boost for parse_simple_boolean') from exc
    return op_upper, str(left), left_boost_value, str(right), right_boost_value


def parse_simple_boolean_with_udf(
    conn: sqlite3.Connection,
    query: str,
    simple_query_re: re.Pattern[str],
) -> tuple[str, str, float, str, float] | None:
    return parse_simple_boolean_c_backend(conn, query)


def parse_simple_phrase_query_c_backend(conn: sqlite3.Connection, query: str) -> tuple[str, int] | None:
    try:
        row = conn.execute('SELECT sqlite_tokenizer_ar_parse_phrase_query_json(?)', (query,)).fetchone()
    except sqlite3.OperationalError as exc:
        message = str(exc)
        if 'no such function: sqlite_tokenizer_ar_parse_phrase_query_json' in message:
            raise SearchCompileError(
                'C token helper backend is not available: missing UDF sqlite_tokenizer_ar_parse_phrase_query_json'
            ) from exc
        raise SearchCompileError(f'C token helper backend execution failed: {message}') from exc
    if row is None or row[0] is None:
        return None
    try:
        payload = json.loads(str(row[0]))
    except json.JSONDecodeError as exc:
        raise SearchCompileError(
            'C token helper backend returned invalid JSON payload for parse_simple_phrase_query'
        ) from exc
    if not isinstance(payload, dict):
        raise SearchCompileError('C token helper payload must be a JSON object for parse_simple_phrase_query')
    phrase = payload.get('phrase')
    slop = payload.get('slop')
    if phrase is None or slop is None:
        raise SearchCompileError('C token helper payload must include phrase/slop for parse_simple_phrase_query')
    try:
        slop_value = max(0, int(slop))
    except (TypeError, ValueError) as exc:
        raise SearchCompileError('C token helper payload has invalid slop for parse_simple_phrase_query') from exc
    return str(phrase), slop_value


def parse_simple_phrase_query_with_udf(conn: sqlite3.Connection, query: str) -> tuple[str, int] | None:
    return parse_simple_phrase_query_c_backend(conn, query)


def parse_single_phrase_clause_c_backend(conn: sqlite3.Connection, query: str) -> tuple[str | None, str, int, float] | None:
    try:
        row = conn.execute('SELECT sqlite_tokenizer_ar_parse_single_phrase_clause_json(?)', (query,)).fetchone()
    except sqlite3.OperationalError as exc:
        message = str(exc)
        if 'no such function: sqlite_tokenizer_ar_parse_single_phrase_clause_json' in message:
            raise SearchCompileError(
                'C token helper backend is not available: missing UDF sqlite_tokenizer_ar_parse_single_phrase_clause_json'
            ) from exc
        raise SearchCompileError(f'C token helper backend execution failed: {message}') from exc
    if row is None or row[0] is None:
        return None
    try:
        payload = json.loads(str(row[0]))
    except json.JSONDecodeError as exc:
        raise SearchCompileError(
            'C token helper backend returned invalid JSON payload for parse_single_phrase_clause'
        ) from exc
    if not isinstance(payload, dict):
        raise SearchCompileError('C token helper payload must be a JSON object for parse_single_phrase_clause')
    scope = payload.get('scope')
    phrase = payload.get('phrase')
    slop = payload.get('slop')
    boost = payload.get('boost')
    if phrase is None or slop is None or boost is None:
        raise SearchCompileError('C token helper payload must include phrase/slop/boost for parse_single_phrase_clause')
    scope_value: str | None
    if scope is None:
        scope_value = None
    else:
        scope_value = str(scope).lower()
        if scope_value not in {'page', 'title'}:
            raise SearchCompileError('C token helper payload has invalid scope for parse_single_phrase_clause')
    try:
        slop_value = max(0, int(slop))
        boost_value = float(boost)
    except (TypeError, ValueError) as exc:
        raise SearchCompileError('C token helper payload has invalid numeric fields for parse_single_phrase_clause') from exc
    return scope_value, str(phrase), slop_value, boost_value


def parse_single_phrase_clause_with_udf(conn: sqlite3.Connection, query: str) -> tuple[str | None, str, int, float] | None:
    return parse_single_phrase_clause_c_backend(conn, query)


def parse_phrase_term_boolean_c_backend(conn: sqlite3.Connection, query: str) -> dict | None:
    try:
        row = conn.execute('SELECT sqlite_tokenizer_ar_parse_phrase_term_boolean_json(?)', (query,)).fetchone()
    except sqlite3.OperationalError as exc:
        message = str(exc)
        if 'no such function: sqlite_tokenizer_ar_parse_phrase_term_boolean_json' in message:
            raise SearchCompileError(
                'C token helper backend is not available: missing UDF sqlite_tokenizer_ar_parse_phrase_term_boolean_json'
            ) from exc
        raise SearchCompileError(f'C token helper backend execution failed: {message}') from exc
    if row is None or row[0] is None:
        return None
    try:
        payload = json.loads(str(row[0]))
    except json.JSONDecodeError as exc:
        raise SearchCompileError(
            'C token helper backend returned invalid JSON payload for parse_phrase_term_boolean'
        ) from exc
    if not isinstance(payload, dict):
        raise SearchCompileError('C token helper payload must be a JSON object for parse_phrase_term_boolean')

    shape = payload.get('shape')
    op = payload.get('op')
    left_scope = payload.get('left_scope')
    left_term = payload.get('left_term')
    left_phrase = payload.get('left_phrase')
    left_slop = payload.get('left_slop')
    left_boost = payload.get('left_boost')
    right_scope = payload.get('right_scope')
    right_term = payload.get('right_term')
    right_phrase = payload.get('right_phrase')
    right_slop = payload.get('right_slop')
    right_boost = payload.get('right_boost')
    if (
        shape is None
        or op is None
        or left_slop is None
        or left_boost is None
        or right_slop is None
        or right_boost is None
    ):
        raise SearchCompileError(
            'C token helper payload missing required fields for parse_phrase_term_boolean'
        )
    shape_value = str(shape)
    if shape_value not in {'phrase_phrase', 'phrase_term', 'term_phrase'}:
        raise SearchCompileError('C token helper payload has invalid shape for parse_phrase_term_boolean')
    op_value = str(op).upper()
    if op_value not in {'AND', 'OR'}:
        raise SearchCompileError('C token helper payload has invalid op for parse_phrase_term_boolean')
    if left_scope is not None and str(left_scope).lower() not in {'page', 'title'}:
        raise SearchCompileError('C token helper payload has invalid left_scope for parse_phrase_term_boolean')
    if right_scope is not None and str(right_scope).lower() not in {'page', 'title'}:
        raise SearchCompileError('C token helper payload has invalid right_scope for parse_phrase_term_boolean')
    try:
        left_slop_value = max(0, int(left_slop))
        right_slop_value = max(0, int(right_slop))
        left_boost_value = float(left_boost)
        right_boost_value = float(right_boost)
    except (TypeError, ValueError) as exc:
        raise SearchCompileError('C token helper payload has invalid numeric fields for parse_phrase_term_boolean') from exc
    return {
        'shape': shape_value,
        'op': op_value,
        'left_scope': None if left_scope is None else str(left_scope).lower(),
        'left_term': None if left_term is None else str(left_term),
        'left_phrase': None if left_phrase is None else str(left_phrase),
        'left_slop': left_slop_value,
        'left_boost': left_boost_value,
        'right_scope': None if right_scope is None else str(right_scope).lower(),
        'right_term': None if right_term is None else str(right_term),
        'right_phrase': None if right_phrase is None else str(right_phrase),
        'right_slop': right_slop_value,
        'right_boost': right_boost_value,
    }


def parse_phrase_term_boolean_with_udf(conn: sqlite3.Connection, query: str) -> dict | None:
    return parse_phrase_term_boolean_c_backend(conn, query)


def levenshtein_distance_with_udf(conn: sqlite3.Connection, a: str, b: str, max_edits: int) -> int:
    row = conn.execute('SELECT sqlite_tokenizer_ar_levenshtein(?, ?, ?)', (a, b, max_edits)).fetchone()
    if row is None or row[0] is None:
        raise SearchCompileError('C scoring backend returned no result for levenshtein')
    return int(row[0])


def quote_match_term(value: str) -> str:
    from python_reference_helpers import quote_match_term_reference

    return quote_match_term_reference(value)


def apply_boost_factor(hits: list[dict], factor: float, conn: sqlite3.Connection) -> list[dict]:
    from python_reference_helpers import apply_boost_factor_reference

    return apply_boost_factor_reference(hits, factor, conn)


def extract_boosted_terms_for_field(query: str, runtime_field: str, ranker: 'LuceneRanker') -> list[tuple[str, float]]:
    from python_reference_helpers import extract_boosted_terms_for_field_reference

    return extract_boosted_terms_for_field_reference(query, runtime_field, ranker)


def extract_boosted_token_expressions_for_field(
    conn: sqlite3.Connection,
    fts_table: str,
    query: str,
    runtime_field: str,
    options: SearchOptions,
) -> list[tuple[str, float]]:
    from python_reference_helpers import extract_boosted_token_expressions_for_field_reference

    return extract_boosted_token_expressions_for_field_reference(conn, fts_table, query, runtime_field, options)


def hit_has_analyzed_term(conn: sqlite3.Connection, field: str, book_id: int, item_id: int, term: str) -> bool:
    from python_reference_helpers import hit_has_analyzed_term_reference

    return hit_has_analyzed_term_reference(conn, field, book_id, item_id, term)


def extract_boosted_phrase_spans_c_backend(conn: sqlite3.Connection, query: str) -> list[tuple[str | None, str, int, float]] | None:
    try:
        row = conn.execute('SELECT sqlite_tokenizer_ar_extract_boosted_phrase_spans_json(?)', (query,)).fetchone()
    except sqlite3.OperationalError as exc:
        message = str(exc)
        if 'no such function: sqlite_tokenizer_ar_extract_boosted_phrase_spans_json' in message:
            raise SearchCompileError(
                'C token helper backend is not available: missing UDF sqlite_tokenizer_ar_extract_boosted_phrase_spans_json'
            ) from exc
        raise SearchCompileError(f'C token helper backend execution failed: {message}') from exc
    if row is None or row[0] is None:
        return None
    try:
        payload = json.loads(str(row[0]))
    except json.JSONDecodeError as exc:
        raise SearchCompileError('C token helper backend returned invalid JSON payload for boosted_phrase_spans') from exc
    if not isinstance(payload, list):
        raise SearchCompileError('C token helper payload must be a JSON array for boosted_phrase_spans')

    out: list[tuple[str | None, str, int, float]] = []
    for entry in payload:
        if not isinstance(entry, dict):
            raise SearchCompileError('C token helper payload entries must be objects for boosted_phrase_spans')
        phrase = entry.get('phrase')
        boost = entry.get('boost')
        slop = entry.get('slop')
        if phrase is None or boost is None or slop is None:
            raise SearchCompileError('C token helper payload entries must include phrase/slop/boost for boosted_phrase_spans')
        scope_raw = entry.get('scope')
        scope = None if scope_raw is None else str(scope_raw).lower()
        try:
            slop_value = max(0, int(slop))
            boost_value = float(boost)
        except (TypeError, ValueError) as exc:
            raise SearchCompileError('C token helper payload has invalid slop/boost for boosted_phrase_spans') from exc
        out.append((scope, str(phrase), slop_value, boost_value))
    return out


def extract_boosted_phrase_spans_with_udf(conn: sqlite3.Connection, query: str) -> list[tuple[str | None, str, int, float]]:
    return extract_boosted_phrase_spans_c_backend(conn, query) or []


def extract_boosted_phrase_expressions_for_field(conn: sqlite3.Connection, query: str, runtime_field: str) -> list[tuple[str, float]]:
    from python_reference_helpers import extract_boosted_phrase_expressions_for_field_reference

    return extract_boosted_phrase_expressions_for_field_reference(conn, query, runtime_field)


def extract_boosted_group_spans_c_backend(
    conn: sqlite3.Connection,
    query: str,
    runtime_field: str,
) -> list[tuple[str, float]] | None:
    try:
        row = conn.execute('SELECT sqlite_tokenizer_ar_extract_boosted_group_spans_json(?, ?)', (query, runtime_field)).fetchone()
    except sqlite3.OperationalError as exc:
        message = str(exc)
        if 'no such function: sqlite_tokenizer_ar_extract_boosted_group_spans_json' in message:
            raise SearchCompileError(
                'C token helper backend is not available: missing UDF sqlite_tokenizer_ar_extract_boosted_group_spans_json'
            ) from exc
        raise SearchCompileError(f'C token helper backend execution failed: {message}') from exc
    if row is None or row[0] is None:
        return None
    try:
        payload = json.loads(str(row[0]))
    except json.JSONDecodeError as exc:
        raise SearchCompileError('C token helper backend returned invalid JSON payload for boosted_group_spans') from exc
    if not isinstance(payload, list):
        raise SearchCompileError('C token helper payload must be a JSON array for boosted_group_spans')

    out: list[tuple[str, float]] = []
    for entry in payload:
        if not isinstance(entry, dict):
            raise SearchCompileError('C token helper payload entries must be objects for boosted_group_spans')
        inner_query = entry.get('inner')
        boost = entry.get('boost')
        if inner_query is None or boost is None:
            raise SearchCompileError('C token helper payload entries must include inner/boost for boosted_group_spans')
        try:
            boost_value = float(boost)
        except (TypeError, ValueError) as exc:
            raise SearchCompileError('C token helper payload has invalid boost for boosted_group_spans') from exc
        out.append((str(inner_query), boost_value))
    return out


def extract_boosted_group_spans_with_udf(
    conn: sqlite3.Connection,
    query: str,
    runtime_field: str,
) -> list[tuple[str, float]]:
    return extract_boosted_group_spans_c_backend(conn, query, runtime_field) or []


def extract_boosted_group_expressions_for_field(
    conn: sqlite3.Connection,
    fts_table: str,
    query: str,
    runtime_field: str,
    options: SearchOptions,
) -> list[tuple[str, float]]:
    from python_reference_helpers import extract_boosted_group_expressions_for_field_reference

    return extract_boosted_group_expressions_for_field_reference(conn, fts_table, query, runtime_field, options)


def hit_matches_clause_expression(conn: sqlite3.Connection, field: str, book_id: int, item_id: int, expression: str) -> bool:
    from python_reference_helpers import hit_matches_clause_expression_reference

    return hit_matches_clause_expression_reference(conn, field, book_id, item_id, expression)


def apply_clause_term_boosts(
    conn: sqlite3.Connection,
    hits: list[dict],
    query: str,
    runtime_field: str,
    ranker: 'LuceneRanker',
    fts_table: str,
    options: SearchOptions,
) -> list[dict]:
    from python_reference_helpers import apply_clause_term_boosts_reference

    return apply_clause_term_boosts_reference(conn, hits, query, runtime_field, ranker, fts_table, options)


def vocab_table_name(fts_table: str) -> str:
    from python_reference_helpers import vocab_table_name_reference

    return vocab_table_name_reference(fts_table)


def table_exists(conn: sqlite3.Connection, table_name: str) -> bool:
    from python_reference_helpers import table_exists_reference

    return table_exists_reference(conn, table_name)


def ensure_vocab(conn: sqlite3.Connection, fts_table: str) -> str:
    from python_reference_helpers import ensure_vocab_reference

    return ensure_vocab_reference(conn, fts_table)


def to_float32(value: float) -> float:
    from python_reference_helpers import to_float32_reference

    return to_float32_reference(value)


def f32_mul_with_udf(conn: sqlite3.Connection, left: float, right: float) -> float:
    try:
        row = conn.execute('SELECT sqlite_tokenizer_ar_f32_mul(?, ?)', (left, right)).fetchone()
    except sqlite3.OperationalError as exc:
        raise SearchCompileError(f'C scoring backend execution failed: {exc}') from exc
    if row is None or row[0] is None:
        raise SearchCompileError('C scoring backend returned no result for f32_mul')
    return to_float32(float(row[0]))


def f32_add_with_udf(conn: sqlite3.Connection, left: float, right: float) -> float:
    try:
        row = conn.execute('SELECT sqlite_tokenizer_ar_f32_add(?, ?)', (left, right)).fetchone()
    except sqlite3.OperationalError as exc:
        raise SearchCompileError(f'C scoring backend execution failed: {exc}') from exc
    if row is None or row[0] is None:
        raise SearchCompileError('C scoring backend returned no result for f32_add')
    return to_float32(float(row[0]))


def lucene_idf_with_udf(conn: sqlite3.Connection, doc_count: int, doc_freq: int) -> float:
    row = conn.execute(
        'SELECT sqlite_tokenizer_ar_lucene_idf(?, ?)',
        (doc_count, doc_freq),
    ).fetchone()
    if row is None or row[0] is None:
        raise SearchCompileError('C scoring backend returned no result for lucene_idf')
    return to_float32(float(row[0]))


def lucene_term_score_with_udf(conn: sqlite3.Connection, weight: float, tf: float, doc_len: int, k1: float, b: float, avgdl: float) -> float:
    row = conn.execute(
        'SELECT sqlite_tokenizer_ar_lucene_term_score(?, ?, ?, ?, ?, ?)',
        (weight, tf, doc_len, k1, b, avgdl),
    ).fetchone()
    if row is None or row[0] is None:
        raise SearchCompileError('C scoring backend returned no result for lucene_term_score')
    return to_float32(float(row[0]))


def normalize_text_for_strict(conn: sqlite3.Connection, value: str, options: SearchOptions) -> str:
    from python_reference_helpers import normalize_text_for_strict_reference

    return normalize_text_for_strict_reference(conn, value, options)


def literal_needs_strict_check(conn: sqlite3.Connection, text: str, options: SearchOptions) -> bool:
    from python_reference_helpers import literal_needs_strict_check_reference

    return literal_needs_strict_check_reference(conn, text, options)


def parse_strict_literals(conn: sqlite3.Connection, query: str, runtime_field: str) -> list[StrictLiteral]:
    from python_reference_helpers import parse_strict_literals_reference

    return parse_strict_literals_reference(conn, query, runtime_field)


def strict_literal_matches(conn: sqlite3.Connection, raw_text: str, literal: StrictLiteral, options: SearchOptions) -> bool:
    from python_reference_helpers import strict_literal_matches_reference

    return strict_literal_matches_reference(conn, raw_text, literal, options)


def literal_presence_matches(conn: sqlite3.Connection, raw_text: str, literal: StrictLiteral, options: SearchOptions) -> bool:
    from python_reference_helpers import literal_presence_matches_reference

    return literal_presence_matches_reference(conn, raw_text, literal, options)


def wildcard_matches_text(normalized_text: str, normalized_pattern: str) -> bool:
    from python_reference_helpers import wildcard_matches_text_reference

    return wildcard_matches_text_reference(normalized_text, normalized_pattern)


def wildcard_matches_text_with_udf(conn: sqlite3.Connection, normalized_text: str, normalized_pattern: str) -> bool:
    try:
        row = conn.execute('SELECT sqlite_tokenizer_ar_wildcard_match(?, ?)', (normalized_text, normalized_pattern)).fetchone()
    except sqlite3.OperationalError as exc:
        raise SearchCompileError(f'C wildcard backend execution failed: {exc}') from exc
    if row is None or row[0] is None:
        raise SearchCompileError('C wildcard backend returned no result')
    return bool(int(row[0]) != 0)


def load_hit_raw_text(conn: sqlite3.Connection, hit: dict) -> str:
    from python_reference_helpers import load_hit_raw_text_reference

    return load_hit_raw_text_reference(conn, hit)


def filter_hits_for_strict_modes(
    conn: sqlite3.Connection,
    query: str,
    runtime_field: str,
    hits: list[dict],
    options: SearchOptions,
) -> list[dict]:
    from python_reference_helpers import filter_hits_for_strict_modes_reference

    return filter_hits_for_strict_modes_reference(conn, query, runtime_field, hits, options)


class LuceneRanker:
    SIMPLE_QUERY_RE = re.compile(r'^[^\s"():*?~^+\-]+$')

    def __init__(self, conn: sqlite3.Connection) -> None:
        self.conn = conn
        self.term_score_cache: dict[tuple[str, str], list[dict]] = {}
        self.phrase_score_cache: dict[tuple[str, tuple[str, ...], tuple[int, ...], int], list[dict]] = {}
        self.source_positions_cache: OrderedDict[tuple[str, int], dict[str, list[int]]] = OrderedDict()
        self._ensure_aux_tables()
        self.field_stats: dict[str, dict] = {}
        self._load_field_stats('page')
        self._load_field_stats('title')

    def _ensure_aux_tables(self) -> None:
        self.conn.execute("CREATE VIRTUAL TABLE IF NOT EXISTS qcv_page_vocab USING fts5vocab(page_fts, 'row')")
        self.conn.execute("CREATE VIRTUAL TABLE IF NOT EXISTS qcv_title_vocab USING fts5vocab(title_fts, 'row')")
        self.conn.execute("CREATE VIRTUAL TABLE IF NOT EXISTS qcv_page_vocab_inst USING fts5vocab(page_fts, 'instance')")
        self.conn.execute("CREATE VIRTUAL TABLE IF NOT EXISTS qcv_title_vocab_inst USING fts5vocab(title_fts, 'instance')")

    def _field_config(self, field: str) -> tuple[str, str, str, str]:
        if field == 'page':
            return ('page_doc_map', 'page_id', 'qcv_page_vocab', 'qcv_page_vocab_inst')
        if field == 'title':
            return ('title_doc_map', 'title_id', 'qcv_title_vocab', 'qcv_title_vocab_inst')
        raise SearchCompileError(f'unsupported field for ranker: {field}')

    def _load_field_stats(self, field: str) -> None:
        map_table, _item_col, _vocab_row, vocab_inst = self._field_config(field)
        doc_lengths: dict[int, int] = {}
        for book_id, token_count in self.conn.execute(
            f"""
            SELECT m.book_id, COUNT(*)
            FROM {vocab_inst} v
            JOIN {map_table} m ON m.rowid = v.doc
            GROUP BY m.book_id
            """
        ):
            doc_lengths[int(book_id)] = int(token_count)
        item_count_row = self.conn.execute(f'SELECT COUNT(*) FROM {map_table}').fetchone()
        item_count = int(item_count_row[0]) if item_count_row is not None else 0
        doc_count = len(doc_lengths)
        total_tokens = sum(doc_lengths.values())
        avgdl = (total_tokens / doc_count) if doc_count > 0 else 0.0
        self.field_stats[field] = {
            'doc_count': doc_count,
            'item_count': item_count,
            'avgdl': avgdl,
            'doc_lengths': doc_lengths,
        }

    def _source_positions_cache_get(self, key: tuple[str, int]) -> dict[str, list[int]] | None:
        value = self.source_positions_cache.get(key)
        if value is not None:
            self.source_positions_cache.move_to_end(key)
        return value

    def _source_positions_cache_set(self, key: tuple[str, int], value: dict[str, list[int]]) -> None:
        self.source_positions_cache[key] = value
        self.source_positions_cache.move_to_end(key)
        while len(self.source_positions_cache) > SOURCE_POSITIONS_CACHE_MAX:
            self.source_positions_cache.popitem(last=False)

    def _build_term_offsets_map(self, analyzed_positions: list[tuple[str, int]]) -> dict[str, list[int]]:
        term_offsets: dict[str, list[int]] = {}
        for term, position in analyzed_positions:
            term_offsets.setdefault(term, []).append(int(position))
        return term_offsets

    def analyze_text(self, text: str) -> list[str]:
        return analyze_text_with_udf(self.conn, text)

    def tokenize_simple_query(self, query: str) -> str | None:
        raw = query.strip()
        if self.SIMPLE_QUERY_RE.fullmatch(raw) is None:
            return None
        terms = self.analyze_text(raw)
        if len(terms) != 1:
            return None
        return terms[0]

    def parse_simple_boolean(self, query: str) -> tuple[str, str, float, str, float] | None:
        return parse_simple_boolean_with_udf(self.conn, query, self.SIMPLE_QUERY_RE)

    def parse_phrase_terms(self, query: str) -> tuple[list[str], list[int], int] | None:
        parsed = parse_simple_phrase_query_with_udf(self.conn, query)
        if parsed is None:
            return None
        phrase_text, slop = parsed
        terms, positions = analyze_phrase_terms_with_positions(self.conn, phrase_text)
        return terms, positions, slop

    def score_simple_term(self, field: str, term: str, limit: int) -> list[dict]:
        if term == '':
            return []
        cache_key = (field, term)
        cached = self.term_score_cache.get(cache_key)
        if cached is not None:
            return cached[:limit]

        map_table, item_col, vocab_row, vocab_inst = self._field_config(field)
        stats = self.field_stats[field]
        doc_count = int(stats['doc_count'])
        avgdl = float(stats['avgdl'])
        doc_lengths = stats['doc_lengths']
        if doc_count == 0 or avgdl <= 0.0:
            return []

        row = self.conn.execute(f'SELECT doc FROM {vocab_row} WHERE term = ?', (term,)).fetchone()
        if row is None:
            return []
        doc_freq = int(row[0])
        if doc_freq <= 0:
            return []

        weight = lucene_idf_with_udf(self.conn, doc_count, doc_freq)

        tf_rows = self.conn.execute(
            f"""
            SELECT m.book_id, m.{item_col}, COUNT(*) AS tf
            FROM {vocab_inst} v
            JOIN {map_table} m ON m.rowid = v.doc
            WHERE v.term = ?
            GROUP BY m.book_id, m.{item_col}
            """,
            (term,),
        ).fetchall()

        scored: list[dict] = []
        for book_id, item_id, term_freq in tf_rows:
            bid = int(book_id)
            tf = to_float32(float(term_freq))
            raw_doc_len = int(doc_lengths.get(bid, 0))
            lucene_score = lucene_term_score_with_udf(self.conn, weight, tf, raw_doc_len, 1.2, 0.75, avgdl)
            scored.append({'field': field, 'book_id': bid, 'item_id': int(item_id), 'score': -float(lucene_score)})

        scored.sort(key=lambda row: (row['score'], row['book_id'], row['item_id']))
        self.term_score_cache[cache_key] = scored
        return scored[:limit]

    def score_simple_boolean(self, field: str, query: str, limit: int) -> list[dict] | None:
        parsed = self.parse_simple_boolean(query)
        if parsed is None:
            return None
        op, left_raw, left_boost, right_raw, right_boost = parsed
        left_terms = self.analyze_text(left_raw)
        right_terms = self.analyze_text(right_raw)

        if len(left_terms) != 1 and len(right_terms) != 1:
            return []
        if len(left_terms) != 1:
            return apply_boost_factor(self.score_simple_term(field, right_terms[0], limit), right_boost, self.conn)
        if len(right_terms) != 1:
            return apply_boost_factor(self.score_simple_term(field, left_terms[0], limit), left_boost, self.conn)

        left_map = self._term_score_map(field, left_terms[0])
        right_map = self._term_score_map(field, right_terms[0])
        if op == 'AND':
            candidate_ids = set(left_map.keys()) & set(right_map.keys())
        else:
            candidate_ids = set(left_map.keys()) | set(right_map.keys())
        merged: list[dict] = []
        for key in candidate_ids:
            book_id, item_id = key
            left_score = to_float32(float(left_map.get(key, 0.0)))
            right_score = to_float32(float(right_map.get(key, 0.0)))
            left_component = f32_mul_with_udf(self.conn, left_score, left_boost)
            right_component = f32_mul_with_udf(self.conn, right_score, right_boost)
            merged.append(
                {
                    'field': field,
                    'book_id': book_id,
                    'item_id': item_id,
                    'score': float(f32_add_with_udf(self.conn, left_component, right_component)),
                }
            )
        merged.sort(key=lambda row: (row['score'], row['book_id'], row['item_id']))
        return merged[:limit]

    def score_simple_phrase(self, field: str, query: str, limit: int) -> list[dict] | None:
        parsed = self.parse_phrase_terms(query)
        if parsed is None:
            return None
        terms, positions, slop = parsed
        if len(terms) == 0:
            return []
        if len(terms) == 1:
            return self.score_simple_term(field, terms[0], limit)
        return self.score_phrase_terms(field, terms, positions, slop, limit)

    def score_phrase_pair(self, field: str, term_a: str, term_b: str, limit: int) -> list[dict]:
        return self.score_phrase_terms(field, [term_a, term_b], [0, 1], 0, limit)

    def _phrase_weight(self, vocab_row: str, doc_count: int, terms: list[str]) -> float | None:
        if doc_count <= 0:
            return None
        weight = to_float32(0.0)
        doc_freq_cache: dict[str, int] = {}
        for term in terms:
            if term == '':
                return None
            doc_freq = doc_freq_cache.get(term)
            if doc_freq is None:
                row = self.conn.execute(f'SELECT doc FROM {vocab_row} WHERE term = ?', (term,)).fetchone()
                if row is None:
                    return None
                doc_freq = int(row[0])
                doc_freq_cache[term] = doc_freq
            if doc_freq <= 0:
                return None
            idf = lucene_idf_with_udf(self.conn, doc_count, doc_freq)
            weight = f32_add_with_udf(self.conn, weight, idf)
        return float(weight)

    def _count_phrase_frequency(
        self,
        term_offsets: dict[str, list[int]],
        phrase_terms: list[str],
        phrase_positions: list[int],
        slop: int,
    ) -> int:
        if len(phrase_terms) < 2:
            return 0
        if len(phrase_positions) != len(phrase_terms):
            return 0
        for term in phrase_terms:
            if term not in term_offsets or not term_offsets[term]:
                return 0
        span_limit = phrase_positions[-1]

        def dfs(term_index: int, first_offset: int | None, prev_offset: int | None) -> int:
            if term_index >= len(phrase_terms):
                if first_offset is None or prev_offset is None:
                    return 0
                extra = (prev_offset - first_offset) - span_limit
                return 1 if extra <= slop else 0

            offsets = term_offsets[phrase_terms[term_index]]
            start_pos = 0 if prev_offset is None else bisect_right(offsets, prev_offset)
            count = 0
            for offset in offsets[start_pos:]:
                if first_offset is None:
                    count += dfs(term_index + 1, offset, offset)
                    continue
                minimum_extra = (offset - first_offset) - phrase_positions[term_index]
                if minimum_extra > slop:
                    break
                count += dfs(term_index + 1, first_offset, offset)
            return count

        return dfs(0, None, None)

    def _phrase_frequency_rows(
        self,
        field: str,
        map_table: str,
        item_col: str,
        vocab_inst: str,
        phrase_terms: list[str],
        phrase_positions: list[int],
        slop: int,
    ) -> list[tuple[int, int, int]]:
        if len(phrase_terms) == 2 and slop == 0 and phrase_positions == [0, 1]:
            term_a, term_b = phrase_terms
            candidate_rows = [
                (int(rowid), int(book_id), int(item_id), int(freq))
                for rowid, book_id, item_id, freq in self.conn.execute(
                    f"""
                    SELECT m.rowid, m.book_id, m.{item_col}, COUNT(*) AS phrase_freq
                    FROM {vocab_inst} v1
                    JOIN {vocab_inst} v2
                      ON v2.doc = v1.doc
                     AND v2.offset = v1.offset + 1
                    JOIN {map_table} m ON m.rowid = v1.doc
                    WHERE v1.term = ? AND v2.term = ?
                    GROUP BY m.book_id, m.{item_col}
                    """,
                    (term_a, term_b),
                ).fetchall()
            ]
            return self._phrase_frequency_rows_from_source(field, candidate_rows, phrase_terms, phrase_positions, slop)

        unique_terms = sorted(set(phrase_terms))
        placeholders = ', '.join('?' for _ in unique_terms)
        rows = self.conn.execute(
            f"""
            SELECT m.rowid, m.book_id, m.{item_col}, v.term, v.offset
            FROM {vocab_inst} v
            JOIN {map_table} m ON m.rowid = v.doc
            WHERE v.term IN ({placeholders})
            ORDER BY m.rowid, v.offset
            """,
            tuple(unique_terms),
        ).fetchall()

        per_doc: dict[tuple[int, int, int], dict[str, list[int]]] = {}
        for rowid, book_id, item_id, term, offset in rows:
            key = (int(rowid), int(book_id), int(item_id))
            term_map = per_doc.setdefault(key, {})
            term_map.setdefault(str(term), []).append(int(offset))

        candidate_rows: list[tuple[int, int, int, int]] = []
        for (rowid, book_id, item_id), term_map in per_doc.items():
            freq = self._count_phrase_frequency(term_map, phrase_terms, phrase_positions, slop)
            if freq > 0:
                candidate_rows.append((rowid, book_id, item_id, int(freq)))
        return self._phrase_frequency_rows_from_source(field, candidate_rows, phrase_terms, phrase_positions, slop)

    def _load_source_positions_for_rowids(self, field: str, rowids: list[int]) -> None:
        if not rowids:
            return
        if field == 'page':
            table_name = 'page_content_store'
            column_name = 'body'
        elif field == 'title':
            table_name = 'title_content_store'
            column_name = 'title'
        else:
            return
        chunk_size = 500
        for start in range(0, len(rowids), chunk_size):
            chunk = rowids[start : start + chunk_size]
            placeholders = ', '.join('?' for _ in chunk)
            rows = self.conn.execute(
                f"SELECT rowid, {column_name} FROM {table_name} WHERE rowid IN ({placeholders})",
                tuple(chunk),
            ).fetchall()
            seen_rowids: set[int] = set()
            for rowid, raw_text in rows:
                key = (field, int(rowid))
                seen_rowids.add(int(rowid))
                if raw_text is None:
                    self._source_positions_cache_set(key, {})
                    continue
                analyzed_positions = analyze_positions_with_udf(self.conn, str(raw_text))
                self._source_positions_cache_set(key, self._build_term_offsets_map(analyzed_positions))
            for rowid in chunk:
                if rowid not in seen_rowids:
                    self._source_positions_cache_set((field, int(rowid)), {})

    def _phrase_frequency_rows_from_source(
        self,
        field: str,
        candidate_rows: list[tuple[int, int, int, int]],
        phrase_terms: list[str],
        phrase_positions: list[int],
        slop: int,
    ) -> list[tuple[int, int, int]]:
        if not candidate_rows:
            return []
        missing_rowids = [
            int(rowid)
            for rowid, _book_id, _item_id, _freq in candidate_rows
            if (field, int(rowid)) not in self.source_positions_cache
        ]
        self._load_source_positions_for_rowids(field, missing_rowids)
        out: list[tuple[int, int, int]] = []
        for rowid, book_id, item_id, _freq in candidate_rows:
            source_offsets = self._source_positions_cache_get((field, int(rowid)))
            if not source_offsets:
                continue
            term_offsets = {term: source_offsets[term] for term in phrase_terms if term in source_offsets}
            freq = self._count_phrase_frequency(term_offsets, phrase_terms, phrase_positions, slop)
            if freq > 0:
                out.append((int(book_id), int(item_id), int(freq)))
        return out

    def score_phrase_terms(self, field: str, terms: list[str], positions: list[int], slop: int, limit: int) -> list[dict]:
        if len(terms) < 2:
            return []
        phrase_terms = [str(term) for term in terms]
        if any(term == '' for term in phrase_terms):
            return []
        if len(positions) != len(phrase_terms):
            return []
        phrase_positions = [max(0, int(position)) for position in positions]
        if phrase_positions and phrase_positions[0] != 0:
            base = phrase_positions[0]
            phrase_positions = [position - base for position in phrase_positions]
        slop_value = max(0, int(slop))
        cache_key = (field, tuple(phrase_terms), tuple(phrase_positions), slop_value)
        cached = self.phrase_score_cache.get(cache_key)
        if cached is not None:
            return cached[:limit]

        map_table, item_col, vocab_row, vocab_inst = self._field_config(field)
        stats = self.field_stats[field]
        doc_count = int(stats['doc_count'])
        avgdl = float(stats['avgdl'])
        doc_lengths = stats['doc_lengths']
        if doc_count == 0 or avgdl <= 0.0:
            return []

        weight = self._phrase_weight(vocab_row, doc_count, phrase_terms)
        if weight is None:
            return []

        rows = self._phrase_frequency_rows(field, map_table, item_col, vocab_inst, phrase_terms, phrase_positions, slop_value)

        scored: list[dict] = []
        for book_id, item_id, phrase_freq in rows:
            bid = int(book_id)
            tf = to_float32(float(phrase_freq))
            raw_doc_len = int(doc_lengths.get(bid, 0))
            lucene_score = lucene_term_score_with_udf(self.conn, weight, tf, raw_doc_len, 1.2, 0.75, avgdl)
            scored.append({'field': field, 'book_id': bid, 'item_id': int(item_id), 'score': -float(lucene_score)})

        scored.sort(key=lambda row: (row['score'], row['book_id'], row['item_id']))
        self.phrase_score_cache[cache_key] = scored
        return scored[:limit]

    def _term_score_map(self, field: str, term: str) -> dict[tuple[int, int], float]:
        doc_limit = max(1, int(self.field_stats[field]['item_count']))
        out: dict[tuple[int, int], float] = {}
        for hit in self.score_simple_term(field, term, doc_limit):
            key = (int(hit['book_id']), int(hit['item_id']))
            out[key] = float(hit['score'])
        return out

    def _phrase_score_map(self, field: str, phrase_text: str, slop: int, limit: int) -> dict[tuple[int, int], float] | None:
        terms, positions = analyze_phrase_terms_with_positions(self.conn, phrase_text)
        if len(terms) == 0:
            return {}
        if len(terms) == 1:
            return self._term_score_map(field, terms[0])
        doc_limit = max(limit, int(self.field_stats[field]['item_count']), 1)
        out: dict[tuple[int, int], float] = {}
        for hit in self.score_phrase_terms(field, terms, positions, slop, doc_limit):
            key = (int(hit['book_id']), int(hit['item_id']))
            out[key] = float(hit['score'])
        return out

    def score_phrase_term_boolean(self, field: str, query: str, limit: int) -> list[dict] | None:
        raw = query.strip()
        if raw == '':
            return None

        def score_phrase_clause(scope: str | None, phrase_text: str, slop: int) -> dict[tuple[int, int], float] | None:
            if scope is not None and scope != field:
                return {}
            return self._phrase_score_map(field, phrase_text, slop, max(limit, 1))

        def score_term_clause(scope: str | None, raw_term: str) -> dict[tuple[int, int], float] | None:
            if scope is not None and scope != field:
                return {}
            terms = self.analyze_text(raw_term)
            if len(terms) != 1:
                return None
            return self._term_score_map(field, terms[0])

        def apply_map_boost(values: dict[tuple[int, int], float], boost: float) -> dict[tuple[int, int], float]:
            return {key: float(f32_mul_with_udf(self.conn, score, boost)) for key, score in values.items()}

        def merge_maps(
            op: str,
            left_map: dict[tuple[int, int], float],
            left_boost: float,
            right_map: dict[tuple[int, int], float],
            right_boost: float,
        ) -> list[dict]:
            boosted_left = apply_map_boost(left_map, left_boost)
            boosted_right = apply_map_boost(right_map, right_boost)
            if op == 'AND':
                candidate_keys = set(boosted_left.keys()) & set(boosted_right.keys())
            else:
                candidate_keys = set(boosted_left.keys()) | set(boosted_right.keys())
            rows: list[dict] = []
            for key in candidate_keys:
                book_id, item_id = key
                score = float(f32_add_with_udf(self.conn, float(boosted_left.get(key, 0.0)), float(boosted_right.get(key, 0.0))))
                rows.append({'field': field, 'book_id': int(book_id), 'item_id': int(item_id), 'score': score})
            rows.sort(key=lambda row: (row['score'], row['book_id'], row['item_id']))
            return rows[: max(limit, 1)]

        parsed = parse_phrase_term_boolean_with_udf(self.conn, raw)
        if parsed is None:
            return None

        shape = str(parsed['shape'])
        op = str(parsed['op']).upper()
        left_scope = parsed['left_scope']
        right_scope = parsed['right_scope']
        left_boost = float(parsed['left_boost'])
        right_boost = float(parsed['right_boost'])
        if shape == 'phrase_phrase':
            left_map = score_phrase_clause(left_scope, str(parsed['left_phrase']), int(parsed['left_slop']))
            right_map = score_phrase_clause(right_scope, str(parsed['right_phrase']), int(parsed['right_slop']))
        elif shape == 'phrase_term':
            left_map = score_phrase_clause(left_scope, str(parsed['left_phrase']), int(parsed['left_slop']))
            right_map = score_term_clause(right_scope, str(parsed['right_term']))
        elif shape == 'term_phrase':
            left_map = score_term_clause(left_scope, str(parsed['left_term']))
            right_map = score_phrase_clause(right_scope, str(parsed['right_phrase']), int(parsed['right_slop']))
        else:
            return None
        if left_map is None or right_map is None:
            return None
        return merge_maps(op, left_map, left_boost, right_map, right_boost)

        return None


def expand_suffix_terms(
    conn: sqlite3.Connection,
    fts_table: str,
    suffix: str,
    max_expansions: int,
) -> list[str]:
    from python_reference_helpers import expand_suffix_terms_reference

    return expand_suffix_terms_reference(conn, fts_table, suffix, max_expansions)


def normalize_wildcard_pattern(conn: sqlite3.Connection, pattern: str) -> str:
    from python_reference_helpers import normalize_wildcard_pattern_reference

    return normalize_wildcard_pattern_reference(conn, pattern)


def wildcard_has_literal(pattern: str) -> bool:
    from python_reference_helpers import wildcard_has_literal_reference

    return wildcard_has_literal_reference(pattern)


def expand_wildcard_terms(
    conn: sqlite3.Connection,
    fts_table: str,
    wildcard_pattern: str,
    max_expansions: int,
) -> list[str]:
    from python_reference_helpers import expand_wildcard_terms_reference

    return expand_wildcard_terms_reference(conn, fts_table, wildcard_pattern, max_expansions)


def expand_fuzzy_terms(
    conn: sqlite3.Connection,
    fts_table: str,
    term: str,
    max_edits: int,
    max_expansions: int,
) -> list[str]:
    from python_reference_helpers import expand_fuzzy_terms_reference

    return expand_fuzzy_terms_reference(conn, fts_table, term, max_edits, max_expansions)


def compile_token_expression(
    conn: sqlite3.Connection,
    fts_table: str,
    runtime_field: str,
    token: str,
    options: SearchOptions,
) -> str | None:
    from python_reference_helpers import compile_token_expression_reference

    return compile_token_expression_reference(conn, fts_table, runtime_field, token, options)


def build_execution_plan(
    conn: sqlite3.Connection,
    fts_table: str,
    runtime_field: str,
    query: str,
    options: SearchOptions,
) -> SqliteExecutionPlan:
    from python_reference_helpers import build_execution_plan_reference

    return build_execution_plan_reference(conn, fts_table, runtime_field, query, options)


def compile_match_expression(
    conn: sqlite3.Connection,
    fts_table: str,
    runtime_field: str,
    query: str,
    options: SearchOptions,
) -> str:
    from python_reference_helpers import compile_match_expression_reference

    return compile_match_expression_reference(conn, fts_table, runtime_field, query, options)


def search_field(
    conn: sqlite3.Connection,
    field: str,
    query: str,
    options: SearchOptions,
    limit: int,
    ranker: LuceneRanker,
) -> tuple[str, list[dict]]:
    from python_reference_helpers import search_field_reference

    return search_field_reference(conn, field, query, options, limit, ranker)


def run_search(
    conn: sqlite3.Connection,
    query: str,
    field: str,
    options: SearchOptions,
    limit: int,
    ranker: LuceneRanker | None = None,
) -> dict:
    from python_reference_helpers import run_search_reference

    return run_search_reference(conn, query, field, options, limit, ranker)


def search_options_payload(options: SearchOptions) -> dict:
    from python_reference_helpers import search_options_payload_reference

    return search_options_payload_reference(options)


def run_search_c_backend(
    conn: sqlite3.Connection,
    query: str,
    field: str,
    options: SearchOptions,
    limit: int,
) -> dict:
    from python_reference_helpers import run_search_c_backend_reference

    return run_search_c_backend_reference(conn, query, field, options, limit)


def run_search_backend(
    conn: sqlite3.Connection,
    query: str,
    field: str,
    options: SearchOptions,
    limit: int,
    backend: str,
    ranker: LuceneRanker | None = None,
) -> dict:
    from python_reference_helpers import run_search_backend_reference

    return run_search_backend_reference(conn, query, field, options, limit, backend, ranker)


def parse_options(args: argparse.Namespace) -> SearchOptions:
    from python_reference_helpers import parse_options_reference

    return parse_options_reference(args)


def build_cli_parser() -> argparse.ArgumentParser:
    from python_reference_helpers import build_cli_parser_reference

    return build_cli_parser_reference()


def main() -> None:
    from python_reference_helpers import main_reference

    return main_reference()


if __name__ == '__main__':
    main()
