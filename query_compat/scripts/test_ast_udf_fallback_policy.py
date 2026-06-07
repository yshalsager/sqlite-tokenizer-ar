#!/usr/bin/env python3
import sqlite3

import sqlite_query_compat as qc


def main() -> None:
    conn = sqlite3.connect(':memory:')
    try:
        query = 'المنصور OR المصور'

        original_parse_query_ast_c_backend = qc.parse_query_ast_c_backend
        original_iter_query_ast_leaves_c_backend = qc.iter_query_ast_leaves_c_backend
        try:
            # Missing-UDF errors should raise explicitly in strict C-only mode.
            def parse_missing_udf(_conn: sqlite3.Connection, _query: str) -> str:
                raise qc.SearchCompileError(
                    'C parser backend is not available: missing UDF sqlite_tokenizer_ar_parse_query_ast_json'
                )

            def leaves_missing_udf(_conn: sqlite3.Connection, _query: str) -> list[tuple[qc.QueryAstLeaf, str]]:
                raise qc.SearchCompileError(
                    'C AST leaves backend is not available: missing UDF sqlite_tokenizer_ar_iter_query_ast_leaves_json'
                )

            qc.parse_query_ast_c_backend = parse_missing_udf
            qc.iter_query_ast_leaves_c_backend = leaves_missing_udf

            try:
                qc.parse_query_ast_with_udf(conn, query)
                raise SystemExit('error: expected missing-UDF AST query to raise')
            except qc.SearchCompileError as exc:
                if 'missing UDF sqlite_tokenizer_ar_parse_query_ast_json' not in str(exc):
                    raise SystemExit(f'error: missing-UDF AST query returned unexpected error: {exc!r}') from exc

            try:
                qc.iter_query_ast_leaves_with_udf(conn, query)
                raise SystemExit('error: expected missing-UDF leaves query to raise')
            except qc.SearchCompileError as exc:
                if 'missing UDF sqlite_tokenizer_ar_iter_query_ast_leaves_json' not in str(exc):
                    raise SystemExit(f'error: missing-UDF leaves query returned unexpected error: {exc!r}') from exc

            # Unsupported-shape errors should still use Python fallback for valid queries.
            def parse_unsupported(_conn: sqlite3.Connection, _query: str) -> str:
                raise qc.SearchCompileError('C parser backend execution failed: unsupported query shape for current C parser rollout')

            def leaves_unsupported(_conn: sqlite3.Connection, _query: str) -> list[tuple[qc.QueryAstLeaf, str]]:
                raise qc.SearchCompileError('C AST leaves backend execution failed: unsupported query shape for current C parser rollout')

            qc.parse_query_ast_c_backend = parse_unsupported
            qc.iter_query_ast_leaves_c_backend = leaves_unsupported

            # Unsupported-shape errors should not use Python fallback unless UDFs are missing.
            try:
                qc.parse_query_ast_with_udf(conn, query)
                raise SystemExit('error: expected unsupported AST query to raise')
            except qc.SearchCompileError as exc:
                if 'unsupported query shape for current C parser rollout' not in str(exc):
                    raise SystemExit(f'error: unsupported AST query returned unexpected error: {exc!r}') from exc

            try:
                qc.iter_query_ast_leaves_with_udf(conn, query)
                raise SystemExit('error: expected unsupported leaves query to raise')
            except qc.SearchCompileError as exc:
                if 'unsupported query shape for current C parser rollout' not in str(exc):
                    raise SystemExit(f'error: unsupported leaves query returned unexpected error: {exc!r}') from exc

            # Unsupported-shape errors stay unsupported even for malformed literals in forced-unsupported mode.
            malformed_query = '"غير مغلق'
            try:
                qc.parse_query_ast_with_udf(conn, malformed_query)
                raise SystemExit('error: expected malformed unsupported AST query to raise')
            except qc.SearchCompileError as exc:
                if 'unsupported query shape for current C parser rollout' not in str(exc):
                    raise SystemExit(f'error: malformed unsupported AST query returned unexpected error: {exc!r}') from exc

            try:
                qc.iter_query_ast_leaves_with_udf(conn, malformed_query)
                raise SystemExit('error: expected malformed unsupported leaves query to raise')
            except qc.SearchCompileError as exc:
                if 'unsupported query shape for current C parser rollout' not in str(exc):
                    raise SystemExit(f'error: malformed unsupported leaves query returned unexpected error: {exc!r}') from exc
        finally:
            qc.parse_query_ast_c_backend = original_parse_query_ast_c_backend
            qc.iter_query_ast_leaves_c_backend = original_iter_query_ast_leaves_c_backend
    finally:
        conn.close()

    print('ok: ast_udf_fallback_policy')


if __name__ == '__main__':
    main()
