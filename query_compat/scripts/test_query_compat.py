#!/usr/bin/env python3
import sqlite3
import tempfile
from pathlib import Path

from sqlite_query_compat import LuceneRanker, SearchCompileError, SearchOptions, build_execution_plan, compile_match_expression, run_search


def expect_compile_error(conn: sqlite3.Connection, query: str, options: SearchOptions, label: str) -> None:
    try:
        run_search(conn, query=query, field='page', options=options, limit=20)
    except SearchCompileError:
        return
    raise SystemExit(f'error: expected compile error for {label}')


def hit_ids(result: dict, field: str) -> list[int]:
    return [int(row['item_id']) for row in result['hits'] if row['field'] == field]


def hit_score(result: dict, field: str, item_id: int) -> float:
    for row in result['hits']:
        if row['field'] == field and int(row['item_id']) == item_id:
            return float(row['score'])
    raise SystemExit(f'error: missing hit score for field={field} item_id={item_id}: {result["hits"]}')


def main() -> None:
    query_compat_dir = Path(__file__).resolve().parents[1]
    schema_path = (query_compat_dir.parent / 'ingester' / 'sql' / '001_canonical_schema.sql').resolve()
    extension_path = (query_compat_dir.parent / 'tokenizer' / 'build' / 'sqlite_tokenizer_ar.so').resolve()
    if not schema_path.exists():
        raise SystemExit(f'error: schema not found: {schema_path}')
    if not extension_path.exists():
        raise SystemExit(f'error: tokenizer extension not found: {extension_path}')

    with tempfile.TemporaryDirectory(prefix='query_compat_test_') as tmp_dir:
        db_path = Path(tmp_dir) / 'search.sqlite3'
        conn = sqlite3.connect(str(db_path))
        try:
            conn.enable_load_extension(True)
            conn.load_extension(str(extension_path))
            conn.executescript(schema_path.read_text(encoding='utf-8'))

            page_docs = [
                (1, 10, 11, 'المنصور باحث في اللغة'),
                (2, 10, 12, 'المصور يكتب المقال'),
                (3, 10, 13, 'قُرْآن وإيمان ١٢٣'),
                (4, 10, 14, 'والكتاب مفيد'),
                (5, 10, 15, 'إلتي نافعة'),
                (6, 10, 16, 'قران وايمان 123'),
                (7, 10, 17, 'مشروبات نافعة'),
                (8, 10, 18, 'كاتب امريكي'),
                (9, 10, 19, 'الكاتب بارع'),
                (10, 10, 20, 'الشاعر بارع'),
                (11, 10, 21, 'طريق العلم نافع'),
                (12, 10, 22, 'طريق الادب نافع'),
                (13, 10, 23, 'مدرسة متقدمة'),
                (14, 10, 24, 'مدرسه متقدمه'),
                (15, 10, 25, 'قال "العلم نور" للطلاب'),
            ]
            for rowid, book_id, page_id, body in page_docs:
                conn.execute(
                    """
                    INSERT INTO page_doc_map(rowid, book_id, page_id, page_no, source_deleted, source_sha1)
                    VALUES(?, ?, ?, ?, 0, '')
                    """,
                    (rowid, book_id, page_id, page_id),
                )
                conn.execute('INSERT INTO page_fts(rowid, body) VALUES(?, ?)', (rowid, body))
                conn.execute('INSERT INTO page_content_store(rowid, body) VALUES(?, ?)', (rowid, body))

            title_docs = [
                (1, 10, 21, 'باب المنصور'),
                (2, 10, 22, 'باب المصور'),
            ]
            for rowid, book_id, title_id, title in title_docs:
                conn.execute(
                    """
                    INSERT INTO title_doc_map(rowid, book_id, title_id, title_level, title_parent_id, source_deleted, source_sha1)
                    VALUES(?, ?, ?, 1, NULL, 0, '')
                    """,
                    (rowid, book_id, title_id),
                )
                conn.execute('INSERT INTO title_fts(rowid, title) VALUES(?, ?)', (rowid, title))
                conn.execute('INSERT INTO title_content_store(rowid, title) VALUES(?, ?)', (rowid, title))
            conn.commit()

            default_options = SearchOptions()
            plan = build_execution_plan(conn, 'page_fts', 'page', 'المنصور AND المصور', default_options)
            expression = compile_match_expression(conn, 'page_fts', 'page', 'المنصور AND المصور', default_options)
            if plan.match_expression != expression:
                raise SystemExit(
                    'error: build_execution_plan output should match compile_match_expression output '
                    f'plan={plan.match_expression!r} expression={expression!r}'
                )
            shared_ranker = LuceneRanker(conn)
            baseline_search = run_search(conn, query='المنصور', field='page', options=default_options, limit=20)
            shared_ranker_search = run_search(
                conn,
                query='المنصور',
                field='page',
                options=default_options,
                limit=20,
                ranker=shared_ranker,
            )
            if baseline_search != shared_ranker_search:
                raise SystemExit('error: shared-ranker run_search result differs from default per-call ranker')
            expect_compile_error(conn, 'منص*', SearchOptions(allow_prefix_search=False), 'prefix disabled policy')
            expect_compile_error(
                conn,
                '*صور',
                SearchOptions(allow_suffix_search=False),
                'suffix disabled policy',
            )
            expect_compile_error(
                conn,
                'منصور~1',
                SearchOptions(allow_fuzzy_search=False),
                'fuzzy disabled policy',
            )
            expect_compile_error(
                conn,
                '*',
                default_options,
                'wildcard with no literal',
            )
            expect_compile_error(
                conn,
                'م*ور',
                SearchOptions(allow_wildcard_search=False),
                'wildcard disabled policy',
            )
            expect_compile_error(
                conn,
                '?صور',
                SearchOptions(allow_wildcard_search=False),
                'single-char wildcard disabled policy',
            )
            expect_compile_error(
                conn,
                '+ AND المصور',
                default_options,
                'dangling clause modifier before boolean operator',
            )
            expect_compile_error(
                conn,
                'NOT',
                default_options,
                'dangling NOT modifier',
            )
            expect_compile_error(
                conn,
                'AND المصور',
                default_options,
                'dangling leading boolean operator',
            )
            expect_compile_error(
                conn,
                'المنصور AND',
                default_options,
                'dangling trailing boolean operator',
            )
            expect_compile_error(
                conn,
                'المنصور AND OR المصور',
                default_options,
                'consecutive boolean operators',
            )
            expect_compile_error(
                conn,
                'title:',
                default_options,
                'dangling field scope',
            )
            expect_compile_error(
                conn,
                ') المصور',
                default_options,
                'unmatched closing parenthesis',
            )
            expect_compile_error(
                conn,
                'المنصور ) المصور',
                default_options,
                'unmatched closing parenthesis in middle',
            )
            expect_compile_error(
                conn,
                'title:(المنصور)^1.2.3',
                default_options,
                'invalid field-group boost',
            )
            expect_compile_error(
                conn,
                '(المنصور OR المصور)^1.2.3',
                default_options,
                'invalid unscoped group boost',
            )
            expect_compile_error(
                conn,
                '(المنصور OR المصور)^',
                default_options,
                'dangling unscoped group boost',
            )
            lenient_options = SearchOptions(lenient_parse_errors=True)
            malformed_lenient = run_search(conn, query='title:', field='both', options=lenient_options, limit=20)
            if malformed_lenient['hits'] != [] or malformed_lenient['compiled'] != {'page': '"__nohit__"', 'title': '"__nohit__"'}:
                raise SystemExit(
                    'error: lenient mode should convert malformed field scope into nohit+empty hits: '
                    f'{malformed_lenient}'
                )
            malformed_unclosed_quote = run_search(conn, query='"غير مغلق', field='page', options=lenient_options, limit=20)
            if malformed_unclosed_quote['hits'] != [] or malformed_unclosed_quote['compiled'] != {'page': '"__nohit__"'}:
                raise SystemExit(
                    'error: lenient mode should convert malformed quote into nohit+empty hits: '
                    f'{malformed_unclosed_quote}'
                )
            escaped_field_scope_literal = run_search(
                conn,
                query='title\\:المصور',
                field='page',
                options=default_options,
                limit=20,
            )
            if escaped_field_scope_literal['compiled']['page'] == '"__nohit__"':
                raise SystemExit(
                    'error: escaped field separator should be treated as literal text, not title scope: '
                    f'{escaped_field_scope_literal["compiled"]}'
                )
            escaped_paren_literal = run_search(
                conn,
                query='\\(المنصور\\)',
                field='page',
                options=default_options,
                limit=20,
            )
            if escaped_paren_literal['compiled']['page'] == '"__nohit__"':
                raise SystemExit(
                    'error: escaped parentheses should be treated as literal text, not grouping tokens: '
                    f'{escaped_paren_literal["compiled"]}'
                )
            escaped_space_literal = run_search(
                conn,
                query='المنصور\\ باحث',
                field='page',
                options=default_options,
                limit=20,
            )
            if ' OR ' in escaped_space_literal['compiled']['page']:
                raise SystemExit(
                    'error: escaped whitespace should keep a single token (no implicit OR split): '
                    f'{escaped_space_literal["compiled"]}'
                )
            escaped_bracket_literal = run_search(
                conn,
                query='\\[abc\\]',
                field='page',
                options=default_options,
                limit=20,
            )
            if escaped_bracket_literal['compiled']['page'] == '"__nohit__"':
                raise SystemExit(
                    'error: escaped brackets should be treated as literal text, not unsupported wildcard syntax: '
                    f'{escaped_bracket_literal["compiled"]}'
                )

            prefix_result = run_search(conn, query='منص*', field='page', options=default_options, limit=20)
            if hit_ids(prefix_result, 'page') != [11]:
                raise SystemExit(f'error: unexpected prefix hits: {prefix_result["hits"]}')

            suffix_result = run_search(conn, query='*صور', field='page', options=default_options, limit=20)
            suffix_ids = sorted(hit_ids(suffix_result, 'page'))
            if suffix_ids != [11, 12]:
                raise SystemExit(f'error: unexpected suffix hits: {suffix_result["hits"]}')

            infix_wildcard = run_search(conn, query='م*ور', field='page', options=default_options, limit=20)
            if sorted(hit_ids(infix_wildcard, 'page')) != [11, 12]:
                raise SystemExit(f'error: unexpected infix wildcard hits: {infix_wildcard["hits"]}')
            capped_infix_wildcard = run_search(
                conn,
                query='م*ور',
                field='page',
                options=SearchOptions(wildcard_max_expansions=1),
                limit=20,
            )
            if hit_ids(capped_infix_wildcard, 'page') != [12]:
                raise SystemExit(
                    'error: wildcard expansion cap should deterministically limit infix wildcard hits to first term expansion: '
                    f'{capped_infix_wildcard["hits"]}'
                )

            single_char_wildcard = run_search(conn, query='?صور', field='page', options=default_options, limit=20)
            if hit_ids(single_char_wildcard, 'page') != [12]:
                raise SystemExit(f'error: unexpected single-char wildcard hits: {single_char_wildcard["hits"]}')
            escaped_wildcard_literal = run_search(conn, query='م\\*ور', field='page', options=default_options, limit=20)
            if hit_ids(escaped_wildcard_literal, 'page'):
                raise SystemExit(
                    'error: escaped wildcard should be treated as a literal token, not wildcard expansion: '
                    f'{escaped_wildcard_literal["hits"]}'
                )

            contains_wildcard = run_search(conn, query='*صور*', field='both', options=default_options, limit=20)
            if sorted(hit_ids(contains_wildcard, 'page')) != [11, 12]:
                raise SystemExit(f'error: wildcard contains query should hit both page docs: {contains_wildcard["hits"]}')
            if sorted(hit_ids(contains_wildcard, 'title')) != [21, 22]:
                raise SystemExit(f'error: wildcard contains query should hit both title docs: {contains_wildcard["hits"]}')

            mixed_result = run_search(conn, query='قران AND ايمان AND 123', field='page', options=default_options, limit=20)
            if sorted(hit_ids(mixed_result, 'page')) != [13, 16]:
                raise SystemExit(f'error: unexpected normalization hits: {mixed_result["hits"]}')

            hamza_stopword_variant = run_search(conn, query='إلتي', field='page', options=default_options, limit=20)
            if hit_ids(hamza_stopword_variant, 'page') != [15]:
                raise SystemExit(f'error: query-time pre-normalization should not erase hamza stopword variant: {hamza_stopword_variant["hits"]}')

            plain_stopword = run_search(conn, query='التي', field='page', options=default_options, limit=20)
            if hit_ids(plain_stopword, 'page'):
                raise SystemExit(f'error: plain stopword query should not produce hits: {plain_stopword["hits"]}')

            and_right_stopword = run_search(conn, query='المنصور AND هذا', field='page', options=default_options, limit=20)
            if hit_ids(and_right_stopword, 'page') != [11]:
                raise SystemExit(f'error: trailing stopword in AND should collapse to other term: {and_right_stopword["hits"]}')

            and_left_stopword = run_search(conn, query='هذا AND المنصور', field='page', options=default_options, limit=20)
            if hit_ids(and_left_stopword, 'page') != [11]:
                raise SystemExit(f'error: leading stopword in AND should collapse to other term: {and_left_stopword["hits"]}')
            and_both_stopwords = run_search(conn, query='عليه AND هذا', field='both', options=default_options, limit=20)
            if and_both_stopwords['hits']:
                raise SystemExit(
                    'error: boolean query with only stopwords should not be treated as a single term query: '
                    f'{and_both_stopwords["hits"]}'
                )

            implicit_or_result = run_search(conn, query='المنصور المصور', field='page', options=default_options, limit=20)
            implicit_or_ids = sorted(hit_ids(implicit_or_result, 'page'))
            if implicit_or_ids != [11, 12]:
                raise SystemExit(f'error: implicit OR mismatch: {implicit_or_result["hits"]}')
            if ' OR ' not in implicit_or_result['compiled']['page']:
                raise SystemExit(f'error: implicit OR should be explicit in compiled query: {implicit_or_result["compiled"]}')
            lucene_classic_or_and = run_search(
                conn,
                query='المنصور OR المصور AND باحث',
                field='page',
                options=default_options,
                limit=20,
            )
            if hit_ids(lucene_classic_or_and, 'page'):
                raise SystemExit(
                    'error: Lucene-classic conjunction handling should not treat OR/AND with precedence here '
                    f'(expected no hits): {lucene_classic_or_and["hits"]}'
                )
            lucene_classic_and_or = run_search(
                conn,
                query='المنصور AND المصور OR باحث',
                field='page',
                options=default_options,
                limit=20,
            )
            if hit_ids(lucene_classic_and_or, 'page'):
                raise SystemExit(
                    'error: Lucene-classic conjunction handling should keep AND-marked clauses required '
                    f'(expected no hits): {lucene_classic_and_or["hits"]}'
                )

            required_only = run_search(conn, query='+المنصور', field='page', options=default_options, limit=20)
            if hit_ids(required_only, 'page') != [11]:
                raise SystemExit(f'error: required clause should keep matching term hits: {required_only["hits"]}')
            required_phrase = run_search(conn, query='+ "المنصور باحث"', field='page', options=default_options, limit=20)
            if hit_ids(required_phrase, 'page') != [11]:
                raise SystemExit(f'error: required phrase clause should keep matching phrase hits: {required_phrase["hits"]}')
            required_inline_phrase = run_search(conn, query='+"المنصور باحث"', field='page', options=default_options, limit=20)
            if hit_ids(required_inline_phrase, 'page') != [11]:
                raise SystemExit(
                    f'error: inline required phrase clause should keep matching phrase hits: {required_inline_phrase["hits"]}'
                )
            required_field_phrase = run_search(
                conn,
                query='+ title:"باب المصور"',
                field='both',
                options=default_options,
                limit=20,
            )
            if hit_ids(required_field_phrase, 'page'):
                raise SystemExit(f'error: required field phrase should not leak to page field: {required_field_phrase["hits"]}')
            if hit_ids(required_field_phrase, 'title') != [22]:
                raise SystemExit(
                    f'error: required field phrase should keep title hit id=22: {required_field_phrase["hits"]}'
                )
            required_inline_field_phrase = run_search(
                conn,
                query='+title:"باب المصور"',
                field='both',
                options=default_options,
                limit=20,
            )
            if hit_ids(required_inline_field_phrase, 'page'):
                raise SystemExit(
                    f'error: inline required field phrase should not leak to page field: {required_inline_field_phrase["hits"]}'
                )
            if hit_ids(required_inline_field_phrase, 'title') != [22]:
                raise SystemExit(
                    f'error: inline required field phrase should keep title hit id=22: {required_inline_field_phrase["hits"]}'
                )

            prohibited_only = run_search(conn, query='-المصور', field='page', options=default_options, limit=20)
            if hit_ids(prohibited_only, 'page'):
                raise SystemExit(f'error: pure prohibited query should return no hits: {prohibited_only["hits"]}')

            mixed_required_optional = run_search(conn, query='+المنصور المصور', field='page', options=default_options, limit=20)
            if hit_ids(mixed_required_optional, 'page') != [11]:
                raise SystemExit(
                    f'error: optional terms should not widen required-clause matches: {mixed_required_optional["hits"]}'
                )
            mixed_required_and = run_search(conn, query='+المنصور AND المصور', field='page', options=default_options, limit=20)
            if hit_ids(mixed_required_and, 'page'):
                raise SystemExit(
                    f'error: required+AND query should return no hits on disjoint docs: {mixed_required_and["hits"]}'
                )
            mixed_required_group = run_search(
                conn,
                query='(المنصور OR المصور) AND +باحث',
                field='page',
                options=default_options,
                limit=20,
            )
            if hit_ids(mixed_required_group, 'page') != [11]:
                raise SystemExit(
                    'error: grouped OR with required clause should keep docs matching group plus required term: '
                    f'{mixed_required_group["hits"]}'
                )
            mixed_prohibited_or = run_search(conn, query='المنصور OR المصور -المصور', field='page', options=default_options, limit=20)
            if hit_ids(mixed_prohibited_or, 'page') != [11]:
                raise SystemExit(
                    f'error: OR query with prohibited clause should exclude prohibited matches: {mixed_prohibited_or["hits"]}'
                )
            mixed_required_group_operand = run_search(
                conn,
                query='+(المنصور OR المصور) AND باحث',
                field='page',
                options=default_options,
                limit=20,
            )
            if hit_ids(mixed_required_group_operand, 'page') != [11]:
                raise SystemExit(
                    'error: required grouped operand should combine with boolean clauses: '
                    f'{mixed_required_group_operand["hits"]}'
                )
            mixed_prohibited_group_operand = run_search(
                conn,
                query='-(المنصور OR المصور) باحث',
                field='page',
                options=default_options,
                limit=20,
            )
            if hit_ids(mixed_prohibited_group_operand, 'page'):
                raise SystemExit(
                    'error: prohibited grouped operand should exclude matching hits even with trailing terms: '
                    f'{mixed_prohibited_group_operand["hits"]}'
                )

            prohibited_filter = run_search(conn, query='*صور -المصور', field='page', options=default_options, limit=20)
            if hit_ids(prohibited_filter, 'page') != [11]:
                raise SystemExit(f'error: prohibited clause should filter suffix hits: {prohibited_filter["hits"]}')

            relaxed_diacritics = run_search(conn, query='قُرْآن', field='page', options=default_options, limit=20)
            if sorted(hit_ids(relaxed_diacritics, 'page')) != [13, 16]:
                raise SystemExit(f'error: relaxed diacritics should match folded variants: {relaxed_diacritics["hits"]}')
            strict_diacritics = run_search(
                conn,
                query='قُرْآن',
                field='page',
                options=SearchOptions(ignore_diacritics=False),
                limit=20,
            )
            if hit_ids(strict_diacritics, 'page') != [13]:
                raise SystemExit(f'error: strict diacritics should keep only exact-form hit: {strict_diacritics["hits"]}')

            relaxed_hamza = run_search(conn, query='إيمان', field='page', options=default_options, limit=20)
            if sorted(hit_ids(relaxed_hamza, 'page')) != [13, 16]:
                raise SystemExit(f'error: relaxed hamza should match folded variants: {relaxed_hamza["hits"]}')
            strict_hamza = run_search(
                conn,
                query='إيمان',
                field='page',
                options=SearchOptions(ignore_hamza_forms=False),
                limit=20,
            )
            if hit_ids(strict_hamza, 'page') != [13]:
                raise SystemExit(f'error: strict hamza should keep only exact-form hit: {strict_hamza["hits"]}')
            strict_hamza_or = run_search(
                conn,
                query='إيمان OR المصور',
                field='page',
                options=SearchOptions(ignore_hamza_forms=False),
                limit=20,
            )
            if sorted(hit_ids(strict_hamza_or, 'page')) != [12, 13]:
                raise SystemExit(
                    f'error: strict hamza OR should keep exact hamza hits plus non-sensitive alternatives: {strict_hamza_or["hits"]}'
                )
            relaxed_hamza_wildcard = run_search(conn, query='إيم*', field='page', options=default_options, limit=20)
            if sorted(hit_ids(relaxed_hamza_wildcard, 'page')) != [13, 16]:
                raise SystemExit(
                    f'error: relaxed hamza wildcard should match folded variants: {relaxed_hamza_wildcard["hits"]}'
                )
            strict_hamza_wildcard = run_search(
                conn,
                query='إيم*',
                field='page',
                options=SearchOptions(ignore_hamza_forms=False),
                limit=20,
            )
            if hit_ids(strict_hamza_wildcard, 'page') != [13]:
                raise SystemExit(
                    f'error: strict hamza wildcard should keep only exact-form hit: {strict_hamza_wildcard["hits"]}'
                )
            relaxed_hamza_qmark = run_search(conn, query='إي?', field='page', options=default_options, limit=20)
            if sorted(hit_ids(relaxed_hamza_qmark, 'page')) != [13, 16]:
                raise SystemExit(
                    f'error: relaxed hamza ? wildcard should match folded variants: {relaxed_hamza_qmark["hits"]}'
                )
            strict_hamza_qmark = run_search(
                conn,
                query='إي?',
                field='page',
                options=SearchOptions(ignore_hamza_forms=False),
                limit=20,
            )
            if hit_ids(strict_hamza_qmark, 'page') != [13]:
                raise SystemExit(
                    f'error: strict hamza ? wildcard should keep only exact-form hit: {strict_hamza_qmark["hits"]}'
                )
            if strict_hamza_qmark['compiled']['page'] == '("__nohit__")':
                raise SystemExit(
                    'error: strict hamza ? wildcard should expand against relaxed index vocab and not compile to __nohit__'
                )
            relaxed_letter_fuzzy = run_search(conn, query='مدرسة~1', field='page', options=default_options, limit=20)
            if sorted(hit_ids(relaxed_letter_fuzzy, 'page')) != [23, 24]:
                raise SystemExit(
                    f'error: relaxed letter fuzzy should match taa-marbuta/heh variants: {relaxed_letter_fuzzy["hits"]}'
                )
            strict_letter_fuzzy = run_search(
                conn,
                query='مدرسة~1',
                field='page',
                options=SearchOptions(ignore_hamza_forms=True, ignore_letter_forms=False),
                limit=20,
            )
            if hit_ids(strict_letter_fuzzy, 'page') != [23]:
                raise SystemExit(
                    f'error: strict letter fuzzy should keep only taa-marbuta exact hits: {strict_letter_fuzzy["hits"]}'
                )
            relaxed_letter_forms = run_search(conn, query='مدرسة', field='page', options=default_options, limit=20)
            if sorted(hit_ids(relaxed_letter_forms, 'page')) != [23, 24]:
                raise SystemExit(
                    'error: relaxed letter forms should match taa-marbuta/heh variants: '
                    f'{relaxed_letter_forms["hits"]}'
                )
            strict_letter_forms_legacy = run_search(
                conn,
                query='مدرسة',
                field='page',
                options=SearchOptions(ignore_hamza_forms=False),
                limit=20,
            )
            if hit_ids(strict_letter_forms_legacy, 'page') != [23]:
                raise SystemExit(
                    'error: legacy strict hamza behavior should still enforce letter-form exactness by default: '
                    f'{strict_letter_forms_legacy["hits"]}'
                )
            strict_letter_only = run_search(
                conn,
                query='مدرسة',
                field='page',
                options=SearchOptions(ignore_hamza_forms=True, ignore_letter_forms=False),
                limit=20,
            )
            if hit_ids(strict_letter_only, 'page') != [23]:
                raise SystemExit(
                    f'error: strict letter forms should keep only taa-marbuta exact hits: {strict_letter_only["hits"]}'
                )
            strict_hamza_only = run_search(
                conn,
                query='مدرسة',
                field='page',
                options=SearchOptions(ignore_hamza_forms=False, ignore_letter_forms=True),
                limit=20,
            )
            if sorted(hit_ids(strict_hamza_only, 'page')) != [23, 24]:
                raise SystemExit(
                    'error: strict hamza-only mode should keep letter-form variants when letter folding is enabled: '
                    f'{strict_hamza_only["hits"]}'
                )

            relaxed_digits = run_search(conn, query='١٢٣', field='page', options=default_options, limit=20)
            if sorted(hit_ids(relaxed_digits, 'page')) != [13, 16]:
                raise SystemExit(f'error: relaxed digit forms should match folded variants: {relaxed_digits["hits"]}')
            strict_digits = run_search(
                conn,
                query='١٢٣',
                field='page',
                options=SearchOptions(ignore_digit_forms=False),
                limit=20,
            )
            if hit_ids(strict_digits, 'page') != [13]:
                raise SystemExit(f'error: strict digit forms should keep only exact-form hit: {strict_digits["hits"]}')
            strict_field_group = run_search(
                conn,
                query='+page:(قُرْآن)',
                field='page',
                options=SearchOptions(ignore_diacritics=False),
                limit=20,
            )
            if hit_ids(strict_field_group, 'page') != [13]:
                raise SystemExit(
                    'error: strict field-group literal should keep only exact-form hit under strict diacritics: '
                    f'{strict_field_group["hits"]}'
                )
            strict_required_or_group = run_search(
                conn,
                query='+page:(قُرْآن OR قران)',
                field='page',
                options=SearchOptions(ignore_diacritics=False),
                limit=20,
            )
            if sorted(hit_ids(strict_required_or_group, 'page')) != [13, 16]:
                raise SystemExit(
                    'error: strict required OR field-group should keep either exact branch under strict diacritics: '
                    f'{strict_required_or_group["hits"]}'
                )
            strict_required_and_group = run_search(
                conn,
                query='+page:(قُرْآن AND إيمان)',
                field='page',
                options=SearchOptions(ignore_diacritics=False),
                limit=20,
            )
            if hit_ids(strict_required_and_group, 'page') != [13]:
                raise SystemExit(
                    'error: strict required AND field-group should keep only docs matching all required strict branches: '
                    f'{strict_required_and_group["hits"]}'
                )

            stem_result = run_search(conn, query='كتاب', field='page', options=default_options, limit=20)
            if 14 not in hit_ids(stem_result, 'page'):
                raise SystemExit(f'error: expected stemmed hit for كتاب: {stem_result["hits"]}')
            stem_result_mashrub = run_search(conn, query='مشروب', field='page', options=default_options, limit=20)
            if 17 not in hit_ids(stem_result_mashrub, 'page'):
                raise SystemExit(f'error: expected stemmed hit for مشروب→مشروبات: {stem_result_mashrub["hits"]}')
            stem_result_amerikiyin = run_search(conn, query='امريكيين', field='page', options=default_options, limit=20)
            if 18 not in hit_ids(stem_result_amerikiyin, 'page'):
                raise SystemExit(f'error: expected stemmed hit for امريكيين→امريكي: {stem_result_amerikiyin["hits"]}')

            fuzzy_result = run_search(conn, query='منسور~1', field='page', options=default_options, limit=20)
            if 11 not in hit_ids(fuzzy_result, 'page'):
                raise SystemExit(f'error: expected fuzzy hit for منسور~1: {fuzzy_result["hits"]}')
            escaped_fuzzy_literal = run_search(conn, query='منسور\\~1', field='page', options=default_options, limit=20)
            if hit_ids(escaped_fuzzy_literal, 'page'):
                raise SystemExit(
                    'error: escaped fuzzy suffix should be treated as literal text, not fuzzy expansion: '
                    f'{escaped_fuzzy_literal["hits"]}'
                )
            boosted_fuzzy_result = run_search(conn, query='منسور~1^2', field='page', options=default_options, limit=20)
            if sorted(hit_ids(boosted_fuzzy_result, 'page')) != sorted(hit_ids(fuzzy_result, 'page')):
                raise SystemExit(
                    'error: boosted fuzzy query should keep the same hit set as unboosted fuzzy query: '
                    f'{boosted_fuzzy_result["hits"]}'
                )
            fuzzy_base_11 = hit_score(fuzzy_result, 'page', 11)
            fuzzy_boosted_11 = hit_score(boosted_fuzzy_result, 'page', 11)
            if abs((fuzzy_base_11 * 2.0) - fuzzy_boosted_11) > 1e-6:
                raise SystemExit(
                    f'error: boosted fuzzy score should scale by factor 2 for doc 11: base={fuzzy_base_11} boosted={fuzzy_boosted_11}'
                )

            wildcard_result = run_search(conn, query='م*ور', field='page', options=default_options, limit=20)
            boosted_wildcard_result = run_search(conn, query='م*ور^3', field='page', options=default_options, limit=20)
            escaped_boosted_wildcard_literal = run_search(
                conn,
                query='م\\*ور^3',
                field='page',
                options=default_options,
                limit=20,
            )
            if ' OR ' in escaped_boosted_wildcard_literal['compiled']['page']:
                raise SystemExit(
                    'error: escaped wildcard should not be expanded as wildcard in boosted queries: '
                    f'{escaped_boosted_wildcard_literal["compiled"]}'
                )
            if sorted(hit_ids(boosted_wildcard_result, 'page')) != sorted(hit_ids(wildcard_result, 'page')):
                raise SystemExit(
                    'error: boosted wildcard query should keep the same hit set as unboosted wildcard query: '
                    f'{boosted_wildcard_result["hits"]}'
                )
            wildcard_base_11 = hit_score(wildcard_result, 'page', 11)
            wildcard_boosted_11 = hit_score(boosted_wildcard_result, 'page', 11)
            if abs((wildcard_base_11 * 3.0) - wildcard_boosted_11) > 1e-6:
                raise SystemExit(
                    'error: boosted wildcard score should scale by factor 3 for doc 11 '
                    f'base={wildcard_base_11} boosted={wildcard_boosted_11}'
                )
            wildcard_base_12 = hit_score(wildcard_result, 'page', 12)
            wildcard_boosted_12 = hit_score(boosted_wildcard_result, 'page', 12)
            if abs((wildcard_base_12 * 3.0) - wildcard_boosted_12) > 1e-6:
                raise SystemExit(
                    'error: boosted wildcard score should scale by factor 3 for doc 12 '
                    f'base={wildcard_base_12} boosted={wildcard_boosted_12}'
                )

            boosted_term = run_search(conn, query='المنصور^2', field='page', options=default_options, limit=20)
            if hit_ids(boosted_term, 'page') != [11]:
                raise SystemExit(f'error: boosted term should parse to same hit set: {boosted_term["hits"]}')
            unboosted_term = run_search(conn, query='المنصور', field='page', options=default_options, limit=20)
            escaped_boost_term = run_search(conn, query='المنصور\\^2', field='page', options=default_options, limit=20)
            if escaped_boost_term['compiled']['page'] == boosted_term['compiled']['page']:
                raise SystemExit(
                    'error: escaped caret should not be parsed as boost syntax in compiled expression: '
                    f'escaped={escaped_boost_term["compiled"]} boosted={boosted_term["compiled"]}'
                )
            unboosted_term_score = hit_score(unboosted_term, 'page', 11)
            boosted_term_score = hit_score(boosted_term, 'page', 11)
            if abs((unboosted_term_score * 2.0) - boosted_term_score) > 1e-6:
                raise SystemExit(
                    f'error: boosted term score should scale by factor 2: base={unboosted_term_score} boosted={boosted_term_score}'
                )
            if hit_ids(escaped_boost_term, 'page') == [11]:
                escaped_boost_term_score = hit_score(escaped_boost_term, 'page', 11)
                if abs(unboosted_term_score - escaped_boost_term_score) > 1e-6:
                    raise SystemExit(
                        'error: escaped caret should not trigger boost scaling '
                        f'(base={unboosted_term_score} escaped={escaped_boost_term_score})'
                    )
            base_musawwir = run_search(conn, query='المصور', field='page', options=default_options, limit=20)
            base_musawwir_score = hit_score(base_musawwir, 'page', 12)
            boosted_or = run_search(conn, query='المنصور^2 OR المصور', field='page', options=default_options, limit=20)
            if sorted(hit_ids(boosted_or, 'page')) != [11, 12]:
                raise SystemExit(f'error: boosted OR should keep expected hit set: {boosted_or["hits"]}')
            boosted_or_mansur_score = hit_score(boosted_or, 'page', 11)
            boosted_or_musawwir_score = hit_score(boosted_or, 'page', 12)
            if abs((unboosted_term_score * 2.0) - boosted_or_mansur_score) > 1e-6:
                raise SystemExit(
                    'error: boosted OR should scale left term contribution by factor 2 '
                    f'base={unboosted_term_score} boosted={boosted_or_mansur_score}'
                )
            if abs(base_musawwir_score - boosted_or_musawwir_score) > 1e-6:
                raise SystemExit(
                    'error: boosted OR should keep unboosted right term contribution '
                    f'base={base_musawwir_score} boosted={boosted_or_musawwir_score}'
                )
            grouped_unboosted = run_search(
                conn,
                query='المنصور OR المصور',
                field='page',
                options=default_options,
                limit=20,
            )
            grouped_boosted = run_search(
                conn,
                query='(المنصور OR المصور)^2',
                field='page',
                options=default_options,
                limit=20,
            )
            if sorted(hit_ids(grouped_unboosted, 'page')) != [11, 12]:
                raise SystemExit(f'error: grouped unboosted query should keep expected hit set: {grouped_unboosted["hits"]}')
            if sorted(hit_ids(grouped_boosted, 'page')) != [11, 12]:
                raise SystemExit(f'error: grouped boosted query should keep expected hit set: {grouped_boosted["hits"]}')
            grouped_unboosted_11 = hit_score(grouped_unboosted, 'page', 11)
            grouped_boosted_11 = hit_score(grouped_boosted, 'page', 11)
            if abs((grouped_unboosted_11 * 2.0) - grouped_boosted_11) > 1e-6:
                raise SystemExit(
                    'error: grouped top-level boost should scale doc 11 score by factor 2 '
                    f'base={grouped_unboosted_11} boosted={grouped_boosted_11}'
                )
            grouped_unboosted_12 = hit_score(grouped_unboosted, 'page', 12)
            grouped_boosted_12 = hit_score(grouped_boosted, 'page', 12)
            if abs((grouped_unboosted_12 * 2.0) - grouped_boosted_12) > 1e-6:
                raise SystemExit(
                    'error: grouped top-level boost should scale doc 12 score by factor 2 '
                    f'base={grouped_unboosted_12} boosted={grouped_boosted_12}'
                )
            nested_group_unboosted = run_search(
                conn,
                query='(المنصور OR المصور) AND باحث',
                field='page',
                options=default_options,
                limit=20,
            )
            nested_group_boosted = run_search(
                conn,
                query='(المنصور OR المصور)^2 AND باحث',
                field='page',
                options=default_options,
                limit=20,
            )
            if hit_ids(nested_group_unboosted, 'page') != [11]:
                raise SystemExit(f'error: nested group unboosted query should match page 11: {nested_group_unboosted["hits"]}')
            if hit_ids(nested_group_boosted, 'page') != [11]:
                raise SystemExit(f'error: nested group boosted query should match page 11: {nested_group_boosted["hits"]}')
            nested_group_unboosted_score = hit_score(nested_group_unboosted, 'page', 11)
            nested_group_boosted_score = hit_score(nested_group_boosted, 'page', 11)
            if abs((nested_group_unboosted_score * 2.0) - nested_group_boosted_score) > 1e-6:
                raise SystemExit(
                    'error: nested group boost should scale matching clause score by factor 2 '
                    f'base={nested_group_unboosted_score} boosted={nested_group_boosted_score}'
                )
            nested_not_group_unboosted = run_search(
                conn,
                query='((المنصور OR المصور) AND NOT (مزور OR باب))',
                field='page',
                options=default_options,
                limit=20,
            )
            nested_not_group_boosted = run_search(
                conn,
                query='((المنصور OR المصور)^2 AND NOT (مزور OR باب))',
                field='page',
                options=default_options,
                limit=20,
            )
            if sorted(hit_ids(nested_not_group_unboosted, 'page')) != [11, 12]:
                raise SystemExit(
                    'error: nested NOT group unboosted query should match docs 11/12: '
                    f'{nested_not_group_unboosted["hits"]}'
                )
            if sorted(hit_ids(nested_not_group_boosted, 'page')) != [11, 12]:
                raise SystemExit(
                    'error: nested NOT group boosted query should match docs 11/12: '
                    f'{nested_not_group_boosted["hits"]}'
                )
            for item_id in [11, 12]:
                base_score = hit_score(nested_not_group_unboosted, 'page', item_id)
                boosted_score = hit_score(nested_not_group_boosted, 'page', item_id)
                if abs((base_score * 2.0) - boosted_score) > 1e-6:
                    raise SystemExit(
                        'error: nested NOT group boost should scale matching branch by factor 2 '
                        f'item_id={item_id} base={base_score} boosted={boosted_score}'
                    )
            complex_unboosted = run_search(
                conn,
                query='(الكاتب OR الشاعر) AND بارع',
                field='page',
                options=default_options,
                limit=20,
            )
            if sorted(hit_ids(complex_unboosted, 'page')) != [19, 20]:
                raise SystemExit(f'error: complex unboosted query should match both writers: {complex_unboosted["hits"]}')
            complex_boosted = run_search(
                conn,
                query='(الكاتب^3 OR الشاعر) AND بارع',
                field='page',
                options=default_options,
                limit=20,
            )
            if sorted(hit_ids(complex_boosted, 'page')) != [19, 20]:
                raise SystemExit(f'error: complex boosted query should keep same hit set: {complex_boosted["hits"]}')
            complex_unboosted_katib = hit_score(complex_unboosted, 'page', 19)
            complex_boosted_katib = hit_score(complex_boosted, 'page', 19)
            if abs((complex_unboosted_katib * 3.0) - complex_boosted_katib) > 1e-6:
                raise SystemExit(
                    'error: complex grouped boost should scale boosted term hit by factor 3 '
                    f'base={complex_unboosted_katib} boosted={complex_boosted_katib}'
                )
            complex_unboosted_shaer = hit_score(complex_unboosted, 'page', 20)
            complex_boosted_shaer = hit_score(complex_boosted, 'page', 20)
            if abs(complex_unboosted_shaer - complex_boosted_shaer) > 1e-6:
                raise SystemExit(
                    'error: complex grouped boost should keep unboosted branch unchanged '
                    f'base={complex_unboosted_shaer} boosted={complex_boosted_shaer}'
                )
            complex_phrase_unboosted = run_search(
                conn,
                query='("طريق العلم" OR "طريق الادب") AND نافع',
                field='page',
                options=default_options,
                limit=20,
            )
            if sorted(hit_ids(complex_phrase_unboosted, 'page')) != [21, 22]:
                raise SystemExit(
                    'error: complex phrase unboosted query should match both phrase branches: '
                    f'{complex_phrase_unboosted["hits"]}'
                )
            complex_phrase_boosted = run_search(
                conn,
                query='("طريق العلم"^4 OR "طريق الادب") AND نافع',
                field='page',
                options=default_options,
                limit=20,
            )
            if sorted(hit_ids(complex_phrase_boosted, 'page')) != [21, 22]:
                raise SystemExit(
                    'error: complex phrase boosted query should keep same hit set: '
                    f'{complex_phrase_boosted["hits"]}'
                )
            phrase_unboosted_alilm = hit_score(complex_phrase_unboosted, 'page', 21)
            phrase_boosted_alilm = hit_score(complex_phrase_boosted, 'page', 21)
            if abs((phrase_unboosted_alilm * 4.0) - phrase_boosted_alilm) > 1e-6:
                raise SystemExit(
                    'error: complex grouped phrase boost should scale boosted phrase branch by factor 4 '
                    f'base={phrase_unboosted_alilm} boosted={phrase_boosted_alilm}'
                )
            phrase_unboosted_adab = hit_score(complex_phrase_unboosted, 'page', 22)
            phrase_boosted_adab = hit_score(complex_phrase_boosted, 'page', 22)
            if abs(phrase_unboosted_adab - phrase_boosted_adab) > 1e-6:
                raise SystemExit(
                    'error: complex grouped phrase boost should keep unboosted phrase branch unchanged '
                    f'base={phrase_unboosted_adab} boosted={phrase_boosted_adab}'
                )
            complex_boosted_with_top = run_search(
                conn,
                query='((الكاتب^3 OR الشاعر) AND بارع)^2',
                field='page',
                options=default_options,
                limit=20,
            )
            if sorted(hit_ids(complex_boosted_with_top, 'page')) != [19, 20]:
                raise SystemExit(
                    'error: top-level boost over boosted complex group should keep same hit set: '
                    f'{complex_boosted_with_top["hits"]}'
                )
            complex_boosted_with_top_katib = hit_score(complex_boosted_with_top, 'page', 19)
            if abs((complex_boosted_katib * 2.0) - complex_boosted_with_top_katib) > 1e-6:
                raise SystemExit(
                    'error: top-level group boost should preserve inner boosted branch and scale doc 19 by factor 2 '
                    f'base={complex_boosted_katib} boosted={complex_boosted_with_top_katib}'
                )
            complex_boosted_with_top_shaer = hit_score(complex_boosted_with_top, 'page', 20)
            if abs((complex_boosted_shaer * 2.0) - complex_boosted_with_top_shaer) > 1e-6:
                raise SystemExit(
                    'error: top-level group boost should preserve inner branch and scale doc 20 by factor 2 '
                    f'base={complex_boosted_shaer} boosted={complex_boosted_with_top_shaer}'
                )

            title_scoped = run_search(conn, query='title:المصور', field='both', options=default_options, limit=20)
            if hit_ids(title_scoped, 'page'):
                raise SystemExit(f'error: field-scoped title query leaked page hits: {title_scoped["hits"]}')
            if hit_ids(title_scoped, 'title') != [22]:
                raise SystemExit(f'error: expected title scoped hit id=22: {title_scoped["hits"]}')
            title_group = run_search(
                conn,
                query='title:(المنصور OR المصور)',
                field='both',
                options=default_options,
                limit=20,
            )
            if hit_ids(title_group, 'page'):
                raise SystemExit(f'error: title grouped query leaked page hits: {title_group["hits"]}')
            if sorted(hit_ids(title_group, 'title')) != [21, 22]:
                raise SystemExit(f'error: title grouped query should hit both title docs: {title_group["hits"]}')
            boosted_title_group = run_search(
                conn,
                query='title:(المنصور OR المصور)^2',
                field='both',
                options=default_options,
                limit=20,
            )
            if hit_ids(boosted_title_group, 'page'):
                raise SystemExit(f'error: boosted title grouped query leaked page hits: {boosted_title_group["hits"]}')
            if sorted(hit_ids(boosted_title_group, 'title')) != [21, 22]:
                raise SystemExit(f'error: boosted title grouped query should hit both title docs: {boosted_title_group["hits"]}')
            title_group_21 = hit_score(title_group, 'title', 21)
            boosted_title_group_21 = hit_score(boosted_title_group, 'title', 21)
            if abs((title_group_21 * 2.0) - boosted_title_group_21) > 1e-6:
                raise SystemExit(
                    'error: boosted title grouped query should scale title 21 score by factor 2 '
                    f'base={title_group_21} boosted={boosted_title_group_21}'
                )
            title_group_22 = hit_score(title_group, 'title', 22)
            boosted_title_group_22 = hit_score(boosted_title_group, 'title', 22)
            if abs((title_group_22 * 2.0) - boosted_title_group_22) > 1e-6:
                raise SystemExit(
                    'error: boosted title grouped query should scale title 22 score by factor 2 '
                    f'base={title_group_22} boosted={boosted_title_group_22}'
                )
            required_title_group = run_search(
                conn,
                query='+title:(المنصور OR المصور)',
                field='both',
                options=default_options,
                limit=20,
            )
            boosted_required_title_group = run_search(
                conn,
                query='+title:(المنصور OR المصور)^2',
                field='both',
                options=default_options,
                limit=20,
            )
            if sorted(hit_ids(required_title_group, 'title')) != [21, 22]:
                raise SystemExit(
                    'error: required title grouped query should keep title ids 21/22: '
                    f'{required_title_group["hits"]}'
                )
            if sorted(hit_ids(boosted_required_title_group, 'title')) != [21, 22]:
                raise SystemExit(
                    'error: boosted required title grouped query should keep title ids 21/22: '
                    f'{boosted_required_title_group["hits"]}'
                )
            req_title_group_21 = hit_score(required_title_group, 'title', 21)
            boosted_req_title_group_21 = hit_score(boosted_required_title_group, 'title', 21)
            if abs((req_title_group_21 * 2.0) - boosted_req_title_group_21) > 1e-6:
                raise SystemExit(
                    'error: boosted required title grouped query should scale title 21 by factor 2 '
                    f'base={req_title_group_21} boosted={boosted_req_title_group_21}'
                )
            req_title_group_22 = hit_score(required_title_group, 'title', 22)
            boosted_req_title_group_22 = hit_score(boosted_required_title_group, 'title', 22)
            if abs((req_title_group_22 * 2.0) - boosted_req_title_group_22) > 1e-6:
                raise SystemExit(
                    'error: boosted required title grouped query should scale title 22 by factor 2 '
                    f'base={req_title_group_22} boosted={boosted_req_title_group_22}'
                )
            inner_boosted_title_group = run_search(
                conn,
                query='title:(المنصور^3 OR المصور)',
                field='both',
                options=default_options,
                limit=20,
            )
            if hit_ids(inner_boosted_title_group, 'page'):
                raise SystemExit(
                    'error: inner boosted title grouped query should not leak page hits: '
                    f'{inner_boosted_title_group["hits"]}'
                )
            if sorted(hit_ids(inner_boosted_title_group, 'title')) != [21, 22]:
                raise SystemExit(
                    'error: inner boosted title grouped query should keep title ids 21/22: '
                    f'{inner_boosted_title_group["hits"]}'
                )
            inner_boost_title_21 = hit_score(inner_boosted_title_group, 'title', 21)
            inner_boost_title_22 = hit_score(inner_boosted_title_group, 'title', 22)
            if abs((title_group_21 * 3.0) - inner_boost_title_21) > 1e-6:
                raise SystemExit(
                    'error: inner boosted title branch should scale title 21 score by factor 3 '
                    f'base={title_group_21} boosted={inner_boost_title_21}'
                )
            if abs(title_group_22 - inner_boost_title_22) > 1e-6:
                raise SystemExit(
                    'error: inner boosted title branch should keep unboosted title 22 score '
                    f'base={title_group_22} boosted={inner_boost_title_22}'
                )
            composed_title_boost = run_search(
                conn,
                query='(title:(المنصور^3 OR المصور))^2',
                field='both',
                options=default_options,
                limit=20,
            )
            if hit_ids(composed_title_boost, 'page'):
                raise SystemExit(
                    'error: composed top+inner title boost query should not leak page hits: '
                    f'{composed_title_boost["hits"]}'
                )
            if sorted(hit_ids(composed_title_boost, 'title')) != [21, 22]:
                raise SystemExit(
                    'error: composed top+inner title boost query should keep title ids 21/22: '
                    f'{composed_title_boost["hits"]}'
                )
            composed_title_21 = hit_score(composed_title_boost, 'title', 21)
            composed_title_22 = hit_score(composed_title_boost, 'title', 22)
            if abs((inner_boost_title_21 * 2.0) - composed_title_21) > 1e-6:
                raise SystemExit(
                    'error: top-level group boost should compose with inner title boost for title 21 '
                    f'base={inner_boost_title_21} boosted={composed_title_21}'
                )
            if abs((inner_boost_title_22 * 2.0) - composed_title_22) > 1e-6:
                raise SystemExit(
                    'error: top-level group boost should scale unboosted inner branch for title 22 by factor 2 '
                    f'base={inner_boost_title_22} boosted={composed_title_22}'
                )
            grouped_with_tail_unboosted = run_search(
                conn,
                query='title:(المنصور OR المصور) AND باب',
                field='both',
                options=default_options,
                limit=20,
            )
            grouped_with_tail_boosted = run_search(
                conn,
                query='title:(المنصور^3 OR المصور) AND باب',
                field='both',
                options=default_options,
                limit=20,
            )
            if sorted(hit_ids(grouped_with_tail_unboosted, 'title')) != [21, 22]:
                raise SystemExit(
                    'error: grouped-with-tail unboosted query should keep title ids 21/22: '
                    f'{grouped_with_tail_unboosted["hits"]}'
                )
            if sorted(hit_ids(grouped_with_tail_boosted, 'title')) != [21, 22]:
                raise SystemExit(
                    'error: grouped-with-tail boosted query should keep title ids 21/22: '
                    f'{grouped_with_tail_boosted["hits"]}'
                )
            tail_unboosted_21 = hit_score(grouped_with_tail_unboosted, 'title', 21)
            tail_boosted_21 = hit_score(grouped_with_tail_boosted, 'title', 21)
            if abs((tail_unboosted_21 * 3.0) - tail_boosted_21) > 1e-6:
                raise SystemExit(
                    'error: inner term boost inside scoped field-group with trailing clause should scale title 21 by factor 3 '
                    f'base={tail_unboosted_21} boosted={tail_boosted_21}'
                )
            tail_unboosted_22 = hit_score(grouped_with_tail_unboosted, 'title', 22)
            tail_boosted_22 = hit_score(grouped_with_tail_boosted, 'title', 22)
            if abs(tail_unboosted_22 - tail_boosted_22) > 1e-6:
                raise SystemExit(
                    'error: inner term boost inside scoped field-group with trailing clause should keep unboosted title 22 '
                    f'base={tail_unboosted_22} boosted={tail_boosted_22}'
                )
            mixed_field_group = run_search(
                conn,
                query='المنصور OR title:(المنصور OR المصور)',
                field='both',
                options=default_options,
                limit=20,
            )
            if hit_ids(mixed_field_group, 'page') != [11]:
                raise SystemExit(f'error: mixed field-group query should keep page id=11 only: {mixed_field_group["hits"]}')
            if sorted(hit_ids(mixed_field_group, 'title')) != [21, 22]:
                raise SystemExit(f'error: mixed field-group query should keep title ids 21/22: {mixed_field_group["hits"]}')
            mixed_scoped_group_boost = run_search(
                conn,
                query='title:(المنصور OR المصور)^2 OR page:(بارع OR باب)^3',
                field='both',
                options=default_options,
                limit=20,
            )
            if mixed_scoped_group_boost['compiled']['page'] == '"__nohit__"':
                raise SystemExit(
                    'error: mixed scoped boosted groups should not collapse page compiled expression to __nohit__: '
                    f'{mixed_scoped_group_boost["compiled"]}'
                )
            if sorted(hit_ids(mixed_scoped_group_boost, 'page')) != [19, 20]:
                raise SystemExit(
                    'error: mixed scoped boosted groups should keep page hits ids 19/20: '
                    f'{mixed_scoped_group_boost["hits"]}'
                )
            if sorted(hit_ids(mixed_scoped_group_boost, 'title')) != [21, 22]:
                raise SystemExit(
                    'error: mixed scoped boosted groups should keep title hits ids 21/22: '
                    f'{mixed_scoped_group_boost["hits"]}'
                )
            scoped_phrase_group_unboosted = run_search(
                conn,
                query='title:("طريق العلم" OR المصور) OR نافع',
                field='both',
                options=default_options,
                limit=20,
            )
            scoped_phrase_group_boosted = run_search(
                conn,
                query='title:("طريق العلم"^2 OR المصور) OR نافع',
                field='both',
                options=default_options,
                limit=20,
            )
            scoped_phrase_page_ids = sorted(hit_ids(scoped_phrase_group_unboosted, 'page'))
            scoped_phrase_page_ids_boosted = sorted(hit_ids(scoped_phrase_group_boosted, 'page'))
            if scoped_phrase_page_ids != scoped_phrase_page_ids_boosted:
                raise SystemExit(
                    'error: scoped phrase-group boost should not change page hit set from trailing branch: '
                    f'baseline={scoped_phrase_page_ids} boosted={scoped_phrase_page_ids_boosted}'
                )
            if (21 not in scoped_phrase_page_ids) or (22 not in scoped_phrase_page_ids):
                raise SystemExit(
                    'error: scoped phrase-group query should include page ids 21/22 from trailing branch: '
                    f'{scoped_phrase_group_unboosted["hits"]}'
                )
            if hit_ids(scoped_phrase_group_unboosted, 'title') != [22]:
                raise SystemExit(
                    'error: scoped phrase-group baseline should keep title id=22 only: '
                    f'{scoped_phrase_group_unboosted["hits"]}'
                )
            if hit_ids(scoped_phrase_group_boosted, 'title') != [22]:
                raise SystemExit(
                    'error: scoped phrase-group boosted query should keep title id=22 only: '
                    f'{scoped_phrase_group_boosted["hits"]}'
                )
            for item_id in [21, 22]:
                page_base = hit_score(scoped_phrase_group_unboosted, 'page', item_id)
                page_boosted = hit_score(scoped_phrase_group_boosted, 'page', item_id)
                if abs(page_base - page_boosted) > 1e-6:
                    raise SystemExit(
                        'error: title-scoped phrase boost should not affect page scoring branch '
                        f'item_id={item_id} base={page_base} boosted={page_boosted}'
                    )
            title_base = hit_score(scoped_phrase_group_unboosted, 'title', 22)
            title_boosted = hit_score(scoped_phrase_group_boosted, 'title', 22)
            if abs(title_base - title_boosted) > 1e-6:
                raise SystemExit(
                    'error: non-matching scoped phrase boost should keep title branch score unchanged '
                    f'base={title_base} boosted={title_boosted}'
                )
            required_field_group = run_search(
                conn,
                query='+title:(المنصور OR المصور)',
                field='both',
                options=default_options,
                limit=20,
            )
            if hit_ids(required_field_group, 'page'):
                raise SystemExit(f'error: required field-group query should not leak page hits: {required_field_group["hits"]}')
            if sorted(hit_ids(required_field_group, 'title')) != [21, 22]:
                raise SystemExit(
                    f'error: required field-group query should keep title ids 21/22: {required_field_group["hits"]}'
                )
            scoped_negative_with_required_page = run_search(
                conn,
                query='-title:(المصور OR مزور) AND page:منصور',
                field='both',
                options=default_options,
                limit=20,
            )
            if hit_ids(scoped_negative_with_required_page, 'title'):
                raise SystemExit(
                    'error: scoped negative with required page clause should not leak title hits: '
                    f'{scoped_negative_with_required_page["hits"]}'
                )
            if hit_ids(scoped_negative_with_required_page, 'page') != [11]:
                raise SystemExit(
                    'error: scoped negative with required page clause should keep page id=11: '
                    f'{scoped_negative_with_required_page["hits"]}'
                )
            boosted_title_scoped = run_search(conn, query='title:المصور^3', field='both', options=default_options, limit=20)
            if hit_ids(boosted_title_scoped, 'page'):
                raise SystemExit(f'error: boosted field-scoped title query leaked page hits: {boosted_title_scoped["hits"]}')
            if hit_ids(boosted_title_scoped, 'title') != [22]:
                raise SystemExit(f'error: expected boosted title scoped hit id=22: {boosted_title_scoped["hits"]}')
            title_scoped_score = hit_score(title_scoped, 'title', 22)
            boosted_title_scoped_score = hit_score(boosted_title_scoped, 'title', 22)
            if abs((title_scoped_score * 3.0) - boosted_title_scoped_score) > 1e-6:
                raise SystemExit(
                    f'error: boosted field-scoped title score should scale by factor 3: base={title_scoped_score} boosted={boosted_title_scoped_score}'
                )

            title_scoped_phrase = run_search(
                conn,
                query='title:"باب المصور"',
                field='both',
                options=default_options,
                limit=20,
            )
            if hit_ids(title_scoped_phrase, 'page'):
                raise SystemExit(f'error: field-scoped phrase leaked page hits: {title_scoped_phrase["hits"]}')
            if hit_ids(title_scoped_phrase, 'title') != [22]:
                raise SystemExit(f'error: expected field phrase hit id=22: {title_scoped_phrase["hits"]}')

            phrase_slop = run_search(conn, query='"المنصور باحث"~1', field='page', options=default_options, limit=20)
            if hit_ids(phrase_slop, 'page') != [11]:
                raise SystemExit(f'error: expected phrase-slop parsed hit id=11: {phrase_slop["hits"]}')
            if 'NEAR(' not in phrase_slop['compiled']['page']:
                raise SystemExit(f'error: phrase slop should compile to NEAR expression: {phrase_slop["compiled"]}')
            escaped_quote_phrase = run_search(
                conn,
                query='"\\\"العلم نور\\\""',
                field='page',
                options=default_options,
                limit=20,
            )
            if hit_ids(escaped_quote_phrase, 'page') != [25]:
                raise SystemExit(
                    'error: escaped quote phrase should compile and match quoted text phrase in source: '
                    f'{escaped_quote_phrase["hits"]}'
                )
            plain_quote_phrase = run_search(
                conn,
                query='"العلم نور"',
                field='page',
                options=default_options,
                limit=20,
            )
            if hit_ids(plain_quote_phrase, 'page') != [25]:
                raise SystemExit(
                    'error: plain quote phrase should match quoted text phrase in source: '
                    f'{plain_quote_phrase["hits"]}'
                )
            escaped_quote_phrase_score = hit_score(escaped_quote_phrase, 'page', 25)
            plain_quote_phrase_score = hit_score(plain_quote_phrase, 'page', 25)
            if abs(escaped_quote_phrase_score - plain_quote_phrase_score) > 1e-6:
                raise SystemExit(
                    'error: escaped quote phrase should use the same phrase scorer as equivalent unescaped phrase '
                    f'escaped={escaped_quote_phrase_score} plain={plain_quote_phrase_score}'
                )
            escaped_quote_phrase_boosted = run_search(
                conn,
                query='"\\\"العلم نور\\\""^2',
                field='page',
                options=default_options,
                limit=20,
            )
            if hit_ids(escaped_quote_phrase_boosted, 'page') != [25]:
                raise SystemExit(
                    'error: boosted escaped quote phrase should preserve hit set for quoted phrase doc id=25: '
                    f'{escaped_quote_phrase_boosted["hits"]}'
                )
            escaped_quote_phrase_boosted_score = hit_score(escaped_quote_phrase_boosted, 'page', 25)
            if abs((escaped_quote_phrase_score * 2.0) - escaped_quote_phrase_boosted_score) > 1e-6:
                raise SystemExit(
                    'error: boosted escaped quote phrase should scale by factor 2 '
                    f'base={escaped_quote_phrase_score} boosted={escaped_quote_phrase_boosted_score}'
                )

            phrase_stopword_gap_no_slop = run_search(conn, query='"باحث اللغة"', field='page', options=default_options, limit=20)
            if hit_ids(phrase_stopword_gap_no_slop, 'page'):
                raise SystemExit(
                    'error: phrase without explicit stopword should not match doc stopword gap without slop allowance: '
                    f'{phrase_stopword_gap_no_slop["hits"]}'
                )
            phrase_stopword_gap_slop = run_search(conn, query='"باحث اللغة"~1', field='page', options=default_options, limit=20)
            if hit_ids(phrase_stopword_gap_slop, 'page') != [11]:
                raise SystemExit(
                    'error: phrase should match doc stopword gap when slop=1: '
                    f'{phrase_stopword_gap_slop["hits"]}'
                )
            phrase_with_stopword = run_search(conn, query='"باحث في اللغة"', field='page', options=default_options, limit=20)
            if hit_ids(phrase_with_stopword, 'page') != [11]:
                raise SystemExit(
                    'error: phrase containing stopword should preserve Lucene position gap semantics and match id=11: '
                    f'{phrase_with_stopword["hits"]}'
                )
            if 'NEAR(' not in phrase_with_stopword['compiled']['page']:
                raise SystemExit(
                    'error: phrase containing removed stopword should compile with implicit slop-aware NEAR expression: '
                    f'{phrase_with_stopword["compiled"]}'
                )

            phrase_gap_no_slop = run_search(conn, query='"المنصور اللغة"', field='page', options=default_options, limit=20)
            if hit_ids(phrase_gap_no_slop, 'page'):
                raise SystemExit(
                    'error: phrase with token gap should not match without slop allowance: '
                    f'{phrase_gap_no_slop["hits"]}'
                )
            phrase_gap_slop = run_search(conn, query='"المنصور اللغة"~2', field='page', options=default_options, limit=20)
            if hit_ids(phrase_gap_slop, 'page') != [11]:
                raise SystemExit(
                    'error: phrase with two-position gap should match when slop=2: '
                    f'{phrase_gap_slop["hits"]}'
                )

            three_term_phrase = run_search(conn, query='"المنصور باحث اللغة"~1', field='page', options=default_options, limit=20)
            if hit_ids(three_term_phrase, 'page') != [11]:
                raise SystemExit(
                    'error: three-term phrase with stopword position gap should match id=11 when slop=1: '
                    f'{three_term_phrase["hits"]}'
                )
            three_term_phrase_boosted = run_search(
                conn,
                query='"المنصور باحث اللغة"~1^3',
                field='page',
                options=default_options,
                limit=20,
            )
            if hit_ids(three_term_phrase_boosted, 'page') != [11]:
                raise SystemExit(
                    'error: boosted three-term phrase should keep same hit set: '
                    f'{three_term_phrase_boosted["hits"]}'
                )
            three_term_phrase_score = hit_score(three_term_phrase, 'page', 11)
            three_term_phrase_boosted_score = hit_score(three_term_phrase_boosted, 'page', 11)
            if abs((three_term_phrase_score * 3.0) - three_term_phrase_boosted_score) > 1e-6:
                raise SystemExit(
                    'error: boosted three-term phrase score should scale by factor 3 '
                    f'base={three_term_phrase_score} boosted={three_term_phrase_boosted_score}'
                )

            boosted_phrase = run_search(conn, query='"المنصور باحث"~1^2', field='page', options=default_options, limit=20)
            if hit_ids(boosted_phrase, 'page') != [11]:
                raise SystemExit(f'error: boosted phrase/slop should parse to same hit set: {boosted_phrase["hits"]}')
            if 'NEAR(' not in boosted_phrase['compiled']['page']:
                raise SystemExit(f'error: boosted phrase slop should compile to NEAR expression: {boosted_phrase["compiled"]}')
            unboosted_phrase = run_search(conn, query='"المنصور باحث"~1', field='page', options=default_options, limit=20)
            unboosted_phrase_score = hit_score(unboosted_phrase, 'page', 11)
            boosted_phrase_score = hit_score(boosted_phrase, 'page', 11)
            if abs((unboosted_phrase_score * 2.0) - boosted_phrase_score) > 1e-6:
                raise SystemExit(
                    f'error: boosted phrase score should scale by factor 2: base={unboosted_phrase_score} boosted={boosted_phrase_score}'
                )
            escaped_quote_phrase = run_search(
                conn,
                query='"المنصور \\"باحث\\""',
                field='page',
                options=default_options,
                limit=20,
            )
            escaped_quote_phrase_boosted = run_search(
                conn,
                query='"المنصور \\"باحث\\""^2',
                field='page',
                options=default_options,
                limit=20,
            )
            if hit_ids(escaped_quote_phrase, 'page') != [11] or hit_ids(escaped_quote_phrase_boosted, 'page') != [11]:
                raise SystemExit(
                    'error: escaped-quote phrase boosts should preserve hit set for page id=11: '
                    f'unboosted={escaped_quote_phrase["hits"]} boosted={escaped_quote_phrase_boosted["hits"]}'
                )
            escaped_quote_phrase_base_score = hit_score(escaped_quote_phrase, 'page', 11)
            escaped_quote_phrase_boosted_score = hit_score(escaped_quote_phrase_boosted, 'page', 11)
            if abs((escaped_quote_phrase_base_score * 2.0) - escaped_quote_phrase_boosted_score) > 1e-6:
                raise SystemExit(
                    'error: escaped-quote phrase boost should scale by factor 2 on page field '
                    f'base={escaped_quote_phrase_base_score} boosted={escaped_quote_phrase_boosted_score}'
                )
            escaped_quote_title_phrase = run_search(
                conn,
                query='title:"باب \\"المصور\\""',
                field='title',
                options=default_options,
                limit=20,
            )
            escaped_quote_title_phrase_boosted = run_search(
                conn,
                query='title:"باب \\"المصور\\""^2',
                field='title',
                options=default_options,
                limit=20,
            )
            if hit_ids(escaped_quote_title_phrase, 'title') != [22] or hit_ids(escaped_quote_title_phrase_boosted, 'title') != [22]:
                raise SystemExit(
                    'error: escaped-quote field phrase boosts should preserve title hit set for id=22: '
                    f'unboosted={escaped_quote_title_phrase["hits"]} boosted={escaped_quote_title_phrase_boosted["hits"]}'
                )
            escaped_quote_title_base_score = hit_score(escaped_quote_title_phrase, 'title', 22)
            escaped_quote_title_boosted_score = hit_score(escaped_quote_title_phrase_boosted, 'title', 22)
            if abs((escaped_quote_title_base_score * 2.0) - escaped_quote_title_boosted_score) > 1e-6:
                raise SystemExit(
                    'error: escaped-quote field phrase boost should scale by factor 2 on title field '
                    f'base={escaped_quote_title_base_score} boosted={escaped_quote_title_boosted_score}'
                )

            phrase_or_phrase = run_search(
                conn,
                query='"طريق العلم" OR "طريق الادب"',
                field='page',
                options=default_options,
                limit=20,
            )
            if sorted(hit_ids(phrase_or_phrase, 'page')) != [21, 22]:
                raise SystemExit(
                    'error: phrase+phrase OR should keep both phrase branch hits (ids 21/22): '
                    f'{phrase_or_phrase["hits"]}'
                )
            phrase_or_phrase_boosted = run_search(
                conn,
                query='"طريق العلم"^4 OR "طريق الادب"',
                field='page',
                options=default_options,
                limit=20,
            )
            if sorted(hit_ids(phrase_or_phrase_boosted, 'page')) != [21, 22]:
                raise SystemExit(
                    'error: boosted phrase+phrase OR should keep same hit set (ids 21/22): '
                    f'{phrase_or_phrase_boosted["hits"]}'
                )
            phrase_or_21 = hit_score(phrase_or_phrase, 'page', 21)
            phrase_or_22 = hit_score(phrase_or_phrase, 'page', 22)
            phrase_or_boosted_21 = hit_score(phrase_or_phrase_boosted, 'page', 21)
            phrase_or_boosted_22 = hit_score(phrase_or_phrase_boosted, 'page', 22)
            if abs((phrase_or_21 * 4.0) - phrase_or_boosted_21) > 1e-6:
                raise SystemExit(
                    'error: boosted phrase+phrase OR should scale boosted phrase branch (doc 21) by factor 4 '
                    f'base={phrase_or_21} boosted={phrase_or_boosted_21}'
                )
            if abs(phrase_or_22 - phrase_or_boosted_22) > 1e-6:
                raise SystemExit(
                    'error: boosted phrase+phrase OR should keep unboosted phrase branch (doc 22) unchanged '
                    f'base={phrase_or_22} boosted={phrase_or_boosted_22}'
                )

            phrase_and_phrase = run_search(
                conn,
                query='"طريق العلم" AND "طريق الادب"',
                field='page',
                options=default_options,
                limit=20,
            )
            if hit_ids(phrase_and_phrase, 'page'):
                raise SystemExit(
                    'error: disjoint phrase+phrase AND should return no hits: '
                    f'{phrase_and_phrase["hits"]}'
                )

            mixed_phrase_slop_and_term = run_search(
                conn,
                query='"المنصور اللغة"~2 AND باحث',
                field='page',
                options=default_options,
                limit=20,
            )
            if hit_ids(mixed_phrase_slop_and_term, 'page') != [11]:
                raise SystemExit(
                    'error: mixed phrase-slop+term AND query should match id=11: '
                    f'{mixed_phrase_slop_and_term["hits"]}'
                )
            mixed_phrase_slop_and_term_boosted = run_search(
                conn,
                query='"المنصور اللغة"~2^2 AND باحث',
                field='page',
                options=default_options,
                limit=20,
            )
            if hit_ids(mixed_phrase_slop_and_term_boosted, 'page') != [11]:
                raise SystemExit(
                    'error: boosted mixed phrase-slop+term AND query should keep same hit set: '
                    f'{mixed_phrase_slop_and_term_boosted["hits"]}'
                )
            phrase_slop_component = hit_score(phrase_gap_slop, 'page', 11)
            baheth_term_component = hit_score(run_search(conn, query='باحث', field='page', options=default_options, limit=20), 'page', 11)
            mixed_phrase_slop_boosted_score = hit_score(mixed_phrase_slop_and_term_boosted, 'page', 11)
            expected_mixed_phrase_slop_boosted = (phrase_slop_component * 2.0) + baheth_term_component
            if abs(expected_mixed_phrase_slop_boosted - mixed_phrase_slop_boosted_score) > 1e-6:
                raise SystemExit(
                    'error: boosted mixed phrase-slop+term AND score should equal boosted phrase + term contribution '
                    f'expected={expected_mixed_phrase_slop_boosted} actual={mixed_phrase_slop_boosted_score}'
                )

            mixed_phrase_and_term = run_search(conn, query='"الكاتب" AND بارع', field='page', options=default_options, limit=20)
            if hit_ids(mixed_phrase_and_term, 'page') != [19]:
                raise SystemExit(
                    f'error: mixed phrase+term AND query should keep only الكاتب+بارع hit id=19: {mixed_phrase_and_term["hits"]}'
                )
            mixed_phrase_and_term_boosted = run_search(
                conn,
                query='"الكاتب"^3 AND بارع',
                field='page',
                options=default_options,
                limit=20,
            )
            if hit_ids(mixed_phrase_and_term_boosted, 'page') != [19]:
                raise SystemExit(
                    'error: boosted mixed phrase+term AND query should keep same hit set: '
                    f'{mixed_phrase_and_term_boosted["hits"]}'
                )
            katib_phrase_score = hit_score(run_search(conn, query='"الكاتب"', field='page', options=default_options, limit=20), 'page', 19)
            bar3_term_score = hit_score(run_search(conn, query='بارع', field='page', options=default_options, limit=20), 'page', 19)
            mixed_boosted_score = hit_score(mixed_phrase_and_term_boosted, 'page', 19)
            expected_mixed_boosted_score = (katib_phrase_score * 3.0) + bar3_term_score
            if abs(expected_mixed_boosted_score - mixed_boosted_score) > 1e-6:
                raise SystemExit(
                    'error: boosted mixed phrase+term AND score should equal boosted phrase contribution + term contribution '
                    f'expected={expected_mixed_boosted_score} actual={mixed_boosted_score}'
                )
            mixed_term_and_phrase = run_search(conn, query='بارع AND "الكاتب"', field='page', options=default_options, limit=20)
            if hit_ids(mixed_term_and_phrase, 'page') != [19]:
                raise SystemExit(
                    'error: mixed term+phrase AND query should match same doc as phrase+term order '
                    f'{mixed_term_and_phrase["hits"]}'
                )

            mixed_phrase_or_term = run_search(conn, query='"طريق العلم" OR نافع', field='page', options=default_options, limit=20)
            mixed_phrase_or_ids = sorted(hit_ids(mixed_phrase_or_term, 'page'))
            if mixed_phrase_or_ids != [15, 17, 21, 22]:
                raise SystemExit(
                    'error: mixed phrase+term OR query should include term-only docs plus phrase doc '
                    f'(ids 15/17/21/22): {mixed_phrase_or_term["hits"]}'
                )
            tariq_phrase_score = hit_score(run_search(conn, query='"طريق العلم"', field='page', options=default_options, limit=20), 'page', 21)
            nafi3_scores = run_search(conn, query='نافع', field='page', options=default_options, limit=20)
            nafi3_17 = hit_score(nafi3_scores, 'page', 17)
            nafi3_21 = hit_score(nafi3_scores, 'page', 21)
            nafi3_22 = hit_score(nafi3_scores, 'page', 22)
            mixed_or_17 = hit_score(mixed_phrase_or_term, 'page', 17)
            mixed_or_21 = hit_score(mixed_phrase_or_term, 'page', 21)
            mixed_or_22 = hit_score(mixed_phrase_or_term, 'page', 22)
            if abs(nafi3_17 - mixed_or_17) > 1e-6:
                raise SystemExit(
                    'error: mixed phrase+term OR should keep term-only doc 17 contribution unchanged '
                    f'base={nafi3_17} mixed={mixed_or_17}'
                )
            if abs((tariq_phrase_score + nafi3_21) - mixed_or_21) > 1e-6:
                raise SystemExit(
                    'error: mixed phrase+term OR should sum phrase+term contributions for doc 21 '
                    f'phrase={tariq_phrase_score} term={nafi3_21} mixed={mixed_or_21}'
                )
            if abs(nafi3_22 - mixed_or_22) > 1e-6:
                raise SystemExit(
                    'error: mixed phrase+term OR should keep term-only doc 22 contribution unchanged '
                    f'base={nafi3_22} mixed={mixed_or_22}'
                )
            mixed_term_or_phrase = run_search(conn, query='نافع OR "طريق العلم"', field='page', options=default_options, limit=20)
            if sorted(hit_ids(mixed_term_or_phrase, 'page')) != [15, 17, 21, 22]:
                raise SystemExit(
                    'error: mixed term+phrase OR query should match same id set as phrase+term order '
                    f'{mixed_term_or_phrase["hits"]}'
                )

            scoped_phrase_or_term_page = run_search(
                conn,
                query='title:"باب المصور" OR page:المصور',
                field='page',
                options=default_options,
                limit=20,
            )
            if hit_ids(scoped_phrase_or_term_page, 'page') != [12]:
                raise SystemExit(
                    'error: scoped mixed OR query on page field should keep only page-scoped branch hit id=12: '
                    f'{scoped_phrase_or_term_page["hits"]}'
                )
            scoped_phrase_or_term_title = run_search(
                conn,
                query='title:"باب المصور" OR page:المصور',
                field='title',
                options=default_options,
                limit=20,
            )
            if hit_ids(scoped_phrase_or_term_title, 'title') != [22]:
                raise SystemExit(
                    'error: scoped mixed OR query on title field should keep only title-scoped branch hit id=22: '
                    f'{scoped_phrase_or_term_title["hits"]}'
                )
            scoped_phrase_or_term_both = run_search(
                conn,
                query='title:"باب المصور" OR page:المصور',
                field='both',
                options=default_options,
                limit=20,
            )
            if hit_ids(scoped_phrase_or_term_both, 'page') != [12]:
                raise SystemExit(
                    'error: scoped mixed OR query on both field should keep page id=12 from page-scoped branch: '
                    f'{scoped_phrase_or_term_both["hits"]}'
                )
            if hit_ids(scoped_phrase_or_term_both, 'title') != [22]:
                raise SystemExit(
                    'error: scoped mixed OR query on both field should keep title id=22 from title-scoped branch: '
                    f'{scoped_phrase_or_term_both["hits"]}'
                )
            scoped_phrase_or_term_both_boosted = run_search(
                conn,
                query='title:"باب المصور"^2 OR page:المصور',
                field='both',
                options=default_options,
                limit=20,
            )
            scoped_base_page_12 = hit_score(scoped_phrase_or_term_both, 'page', 12)
            scoped_base_title_22 = hit_score(scoped_phrase_or_term_both, 'title', 22)
            scoped_boosted_page_12 = hit_score(scoped_phrase_or_term_both_boosted, 'page', 12)
            scoped_boosted_title_22 = hit_score(scoped_phrase_or_term_both_boosted, 'title', 22)
            if abs(scoped_base_page_12 - scoped_boosted_page_12) > 1e-6:
                raise SystemExit(
                    'error: scoped mixed OR boost on title branch should not change page branch score '
                    f'base={scoped_base_page_12} boosted={scoped_boosted_page_12}'
                )
            if abs((scoped_base_title_22 * 2.0) - scoped_boosted_title_22) > 1e-6:
                raise SystemExit(
                    'error: scoped mixed OR boost on title branch should scale title score by factor 2 '
                    f'base={scoped_base_title_22} boosted={scoped_boosted_title_22}'
                )

            page_scoped = run_search(conn, query='page:المنصور', field='both', options=default_options, limit=20)
            if hit_ids(page_scoped, 'title'):
                raise SystemExit(f'error: field-scoped page query leaked title hits: {page_scoped["hits"]}')
            if hit_ids(page_scoped, 'page') != [11]:
                raise SystemExit(f'error: expected page scoped hit id=11: {page_scoped["hits"]}')

            both_result = run_search(conn, query='*صور', field='both', options=default_options, limit=20)
            both_page_ids = sorted(hit_ids(both_result, 'page'))
            both_title_ids = sorted(hit_ids(both_result, 'title'))
            if both_page_ids != [11, 12] or both_title_ids != [21, 22]:
                raise SystemExit(f'error: unexpected both-field hits: {both_result["hits"]}')

            print('ok: query_compat_smoke')
        finally:
            conn.close()


if __name__ == '__main__':
    main()
