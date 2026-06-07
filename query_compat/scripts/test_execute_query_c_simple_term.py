#!/usr/bin/env python3
import sqlite3
import tempfile
from pathlib import Path

from generate_compile_baseline import setup_fixture_db
from sqlite_query_compat import SearchCompileError, SearchOptions, run_search_backend


def main() -> None:
    query_compat_dir = Path(__file__).resolve().parents[1]
    root = query_compat_dir.parent
    schema_path = (root / 'ingester' / 'sql' / '001_canonical_schema.sql').resolve()
    extension_path = (root / 'tokenizer' / 'build' / 'sqlite_tokenizer_ar.so').resolve()
    if not schema_path.exists():
        raise SystemExit(f'error: schema not found: {schema_path}')
    if not extension_path.exists():
        raise SystemExit(f'error: tokenizer extension not found: {extension_path}')

    with tempfile.TemporaryDirectory(prefix='execute_query_c_simple_term_') as tmp_dir:
        db_path = Path(tmp_dir) / 'execute_query.sqlite3'
        conn = sqlite3.connect(str(db_path))
        try:
            setup_fixture_db(conn, schema_path, extension_path)

            supported_cases = [
                {'query': 'المنصور', 'field': 'page', 'lenient': False},
                {'query': 'المنصور', 'field': 'title', 'lenient': False},
                {'query': 'المنصور', 'field': 'both', 'lenient': False},
            ]
            for case in supported_cases:
                options = SearchOptions(lenient_parse_errors=bool(case['lenient']))
                py_result = run_search_backend(
                    conn,
                    query=str(case['query']),
                    field=str(case['field']),
                    options=options,
                    limit=20,
                    backend='python',
                )
                c_result = run_search_backend(
                    conn,
                    query=str(case['query']),
                    field=str(case['field']),
                    options=options,
                    limit=20,
                    backend='c',
                )
                if py_result != c_result:
                    raise SystemExit(
                        'error: c execute simple-term parity mismatch '
                        f'case={case!r} python={py_result!r} c={c_result!r}'
                    )

            boolean_cases = [
                {'query': 'المنصور OR المصور', 'field': 'page'},
                {'query': 'المنصور OR المصور', 'field': 'title'},
                {'query': 'المنصور OR المصور', 'field': 'both'},
                {'query': 'المنصور AND المصور', 'field': 'page'},
                {'query': 'المنصور AND المصور', 'field': 'both'},
                {'query': 'المنصور^2 OR اللغة', 'field': 'page'},
                {'query': 'المنصور^2 OR اللغة', 'field': 'both'},
                {'query': '(المنصور OR المصور)^2', 'field': 'both'},
                {'query': 'title:(المنصور OR المصور)', 'field': 'both'},
                {'query': 'title:(المنصور OR المصور)^2', 'field': 'both'},
                {'query': '(title:(المنصور OR المصور))^2', 'field': 'both'},
                {'query': '+title:(المنصور OR المصور)^2', 'field': 'both'},
                {'query': 'المنصور NOT اللغة', 'field': 'page'},
                {'query': 'المنصور AND NOT اللغة', 'field': 'both'},
                {'query': '+المنصور -اللغة', 'field': 'page'},
            ]
            for case in boolean_cases:
                options = SearchOptions()
                py_result = run_search_backend(
                    conn,
                    query=str(case['query']),
                    field=str(case['field']),
                    options=options,
                    limit=20,
                    backend='python',
                )
                c_result = run_search_backend(
                    conn,
                    query=str(case['query']),
                    field=str(case['field']),
                    options=options,
                    limit=20,
                    backend='c',
                )
                if py_result != c_result:
                    raise SystemExit(
                        'error: c execute simple-boolean parity mismatch '
                        f'case={case!r} python={py_result!r} c={c_result!r}'
                    )

            scoped_boolean_cases = [
                {'query': 'title:باب OR title:المنصور', 'field': 'both'},
                {'query': 'title:باب AND title:المنصور', 'field': 'both'},
                {'query': 'title:باب OR title:المنصور', 'field': 'title'},
                {'query': 'title:باب OR title:المنصور', 'field': 'page'},
                {'query': 'page:المنصور OR page:اللغة', 'field': 'both'},
                {'query': 'page:المنصور AND page:اللغة', 'field': 'both'},
                {'query': 'page:المنصور OR page:اللغة', 'field': 'page'},
                {'query': 'page:المنصور OR page:اللغة', 'field': 'title'},
                {'query': 'title:باب OR اللغة', 'field': 'both'},
                {'query': 'page:المنصور OR اللغة', 'field': 'both'},
                {'query': 'title:باب OR page:اللغة', 'field': 'both'},
                {'query': 'title:باب AND page:اللغة', 'field': 'both'},
                {'query': 'title:باب OR page:اللغة', 'field': 'title'},
                {'query': 'title:باب OR page:اللغة', 'field': 'page'},
                {'query': 'title:باب^2 OR title:المنصور', 'field': 'both'},
                {'query': 'page:المنصور^2 OR اللغة', 'field': 'both'},
                {'query': 'title:باب^2 OR page:اللغة^3', 'field': 'both'},
                {'query': 'title:باب^2 AND page:اللغة^3', 'field': 'both'},
            ]
            for case in scoped_boolean_cases:
                options = SearchOptions()
                py_result = run_search_backend(
                    conn,
                    query=str(case['query']),
                    field=str(case['field']),
                    options=options,
                    limit=20,
                    backend='python',
                )
                c_result = run_search_backend(
                    conn,
                    query=str(case['query']),
                    field=str(case['field']),
                    options=options,
                    limit=20,
                    backend='c',
                )
                if py_result != c_result:
                    raise SystemExit(
                        'error: c execute scoped-simple-boolean parity mismatch '
                        f'case={case!r} python={py_result!r} c={c_result!r}'
                    )

            phrase_single_term_cases = [
                {'query': '"المنصور"', 'field': 'page'},
                {'query': '"المنصور"', 'field': 'title'},
                {'query': '"المنصور"', 'field': 'both'},
            ]
            for case in phrase_single_term_cases:
                options = SearchOptions()
                py_result = run_search_backend(
                    conn,
                    query=str(case['query']),
                    field=str(case['field']),
                    options=options,
                    limit=20,
                    backend='python',
                )
                c_result = run_search_backend(
                    conn,
                    query=str(case['query']),
                    field=str(case['field']),
                    options=options,
                    limit=20,
                    backend='c',
                )
                if py_result != c_result:
                    raise SystemExit(
                        'error: c execute single-term phrase parity mismatch '
                        f'case={case!r} python={py_result!r} c={c_result!r}'
                    )

            phrase_multi_term_cases = [
                {'query': '"المنصور باحث"', 'field': 'page'},
                {'query': '"باب المنصور"', 'field': 'title'},
                {'query': '"قران وايمان"', 'field': 'both'},
                {'query': '"المنصور اللغة"~2', 'field': 'page'},
                {'query': 'title:"باب المنصور"', 'field': 'both'},
                {'query': 'page:"المنصور باحث"', 'field': 'both'},
                {'query': 'title:"باب المنصور"~1', 'field': 'both'},
                {'query': 'title:"باب المنصور"^2', 'field': 'both'},
                {'query': 'title:"\\"باب المصور\\""^2', 'field': 'both'},
            ]
            for case in phrase_multi_term_cases:
                options = SearchOptions()
                py_result = run_search_backend(
                    conn,
                    query=str(case['query']),
                    field=str(case['field']),
                    options=options,
                    limit=20,
                    backend='python',
                )
                c_result = run_search_backend(
                    conn,
                    query=str(case['query']),
                    field=str(case['field']),
                    options=options,
                    limit=20,
                    backend='c',
                )
                if py_result != c_result:
                    raise SystemExit(
                        'error: c execute multi-term phrase parity mismatch '
                        f'case={case!r} python={py_result!r} c={c_result!r}'
                    )

            phrase_boolean_cases = [
                {'query': '"المنصور باحث" OR اللغة', 'field': 'page'},
                {'query': '"المنصور باحث" AND اللغة', 'field': 'page'},
                {'query': 'اللغة OR "باب المنصور"', 'field': 'both'},
                {'query': '"المنصور باحث" OR "قران وايمان"', 'field': 'both'},
                {'query': 'title:"باب المنصور" OR title:المنصور', 'field': 'both'},
                {'query': 'page:"المنصور باحث" OR page:اللغة', 'field': 'both'},
                {'query': 'title:"باب المنصور" OR page:اللغة', 'field': 'both'},
                {'query': 'title:"باب المنصور" AND page:اللغة', 'field': 'both'},
                {'query': 'title:"باب المنصور"^2 OR title:المنصور', 'field': 'both'},
                {'query': 'title:"باب المنصور"^2 OR page:اللغة^3', 'field': 'both'},
                {'query': '"المنصور باحث" NOT اللغة', 'field': 'page'},
                {'query': 'title:"باب المنصور" NOT page:اللغة', 'field': 'both'},
            ]
            for case in phrase_boolean_cases:
                options = SearchOptions()
                py_result = run_search_backend(
                    conn,
                    query=str(case['query']),
                    field=str(case['field']),
                    options=options,
                    limit=20,
                    backend='python',
                )
                c_result = run_search_backend(
                    conn,
                    query=str(case['query']),
                    field=str(case['field']),
                    options=options,
                    limit=20,
                    backend='c',
                )
                if py_result != c_result:
                    raise SystemExit(
                        'error: c execute phrase-boolean parity mismatch '
                        f'case={case!r} python={py_result!r} c={c_result!r}'
                    )

            options_strict = SearchOptions(lenient_parse_errors=False)
            try:
                run_search_backend(conn, query='title:', field='both', options=options_strict, limit=20, backend='python')
            except SearchCompileError as exc:
                py_error = str(exc)
            else:
                raise SystemExit('error: expected python backend to fail for dangling field scope')
            try:
                run_search_backend(conn, query='title:', field='both', options=options_strict, limit=20, backend='c')
            except SearchCompileError as exc:
                c_error = str(exc)
            else:
                raise SystemExit('error: expected c backend to fail for dangling field scope')
            if py_error != c_error:
                raise SystemExit(
                    'error: dangling field scope error mismatch '
                    f'python={py_error!r} c={c_error!r}'
                )

            options_lenient = SearchOptions(lenient_parse_errors=True)
            py_lenient = run_search_backend(conn, query='title:', field='both', options=options_lenient, limit=20, backend='python')
            c_lenient = run_search_backend(conn, query='title:', field='both', options=options_lenient, limit=20, backend='c')
            if py_lenient != c_lenient:
                raise SystemExit(
                    'error: dangling field scope lenient no-hit mismatch '
                    f'python={py_lenient!r} c={c_lenient!r}'
                )

            grouped_boolean_cases = [
                {'query': '(المنصور OR اللغة) AND المصور', 'field': 'page'},
                {'query': '(الكاتب^3 OR الشاعر) AND بارع', 'field': 'both'},
                {'query': '((الكاتب^3 OR الشاعر) AND بارع)^2', 'field': 'both'},
                {'query': '("طريق العلم"^4 OR "طريق الادب") AND نافع', 'field': 'both'},
                {'query': 'title:(المنصور^3 OR المصور) AND باب', 'field': 'both'},
                {'query': '-(المصور OR مزور) AND منصور', 'field': 'both'},
                {'query': '(المنصور OR المصور) AND (باحث OR باب)', 'field': 'both'},
                {'query': 'title:(المنصور OR المصور)^2 AND page:بارع', 'field': 'both'},
                {'query': '("\\"طريق العلم\\""^4 OR "\\"طريق الادب\\"") AND نافع', 'field': 'both'},
            ]
            for case in grouped_boolean_cases:
                options = SearchOptions()
                py_result = run_search_backend(
                    conn,
                    query=str(case['query']),
                    field=str(case['field']),
                    options=options,
                    limit=20,
                    backend='python',
                )
                c_result = run_search_backend(
                    conn,
                    query=str(case['query']),
                    field=str(case['field']),
                    options=options,
                    limit=20,
                    backend='c',
                )
                if py_result != c_result:
                    raise SystemExit(
                        'error: c execute grouped-boolean parity mismatch '
                        f'case={case!r} python={py_result!r} c={c_result!r}'
                    )

            wildcard_fuzzy_cases = [
                {'query': 'منسور~1', 'field': 'both'},
                {'query': 'منسور~1^2', 'field': 'both'},
                {'query': 'م*ور', 'field': 'both'},
                {'query': 'م*ور^3', 'field': 'both'},
            ]
            for case in wildcard_fuzzy_cases:
                options = SearchOptions()
                py_result = run_search_backend(
                    conn,
                    query=str(case['query']),
                    field=str(case['field']),
                    options=options,
                    limit=20,
                    backend='python',
                )
                c_result = run_search_backend(
                    conn,
                    query=str(case['query']),
                    field=str(case['field']),
                    options=options,
                    limit=20,
                    backend='c',
                )
                if py_result != c_result:
                    raise SystemExit(
                        'error: c execute wildcard/fuzzy parity mismatch '
                        f'case={case!r} python={py_result!r} c={c_result!r}'
                    )

            print('ok: execute_query_c_simple_term')
        finally:
            conn.close()


if __name__ == '__main__':
    main()
