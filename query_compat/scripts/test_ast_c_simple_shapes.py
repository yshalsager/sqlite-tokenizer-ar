#!/usr/bin/env python3
import sqlite3
import tempfile
from pathlib import Path

from sqlite_query_compat import parse_query_ast_backend


def main() -> None:
    query_compat_dir = Path(__file__).resolve().parents[1]
    extension_path = (query_compat_dir.parent / 'tokenizer' / 'build' / 'sqlite_tokenizer_ar.so').resolve()
    if not extension_path.exists():
        raise SystemExit(f'error: tokenizer extension not found: {extension_path}')

    with tempfile.TemporaryDirectory(prefix='ast_c_simple_shapes_') as tmp_dir:
        db_path = Path(tmp_dir) / 'parser.sqlite3'
        conn = sqlite3.connect(str(db_path))
        try:
            conn.enable_load_extension(True)
            conn.load_extension(str(extension_path))

            supported = [
                'المنصور',
                '+المنصور',
                '-المنصور',
                'المنصور AND المصور',
                'المنصور OR المصور',
                'المنصور NOT المصور',
                'المنصور AND NOT المصور',
                '(المنصور OR المصور)^2',
                '(الكاتب^3 OR الشاعر) AND بارع',
                '((الكاتب^3 OR الشاعر) AND بارع)^2',
                '((المنصور OR المصور) AND NOT (مزور OR باب))',
                'title:(المنصور OR المصور)',
                'title:(المنصور OR المصور)^2',
                'title:(المنصور OR المصور)^2 OR المصور',
                'title:(المنصور^3 OR المصور) AND باب',
                '+page:("العلم نور" OR المصور)',
                '-(المصور OR مزور) AND منصور',
                '(المنصور OR المصور) AND (باحث OR باب)',
                '"صلى الله"',
                '"طريق العلم"~2',
                'title:الزكاه',
                '"العلم نور" OR العلم',
                'title:"باب القراءة" OR page:المدرسه',
                '"\\\"طريق العلم\\\""',
                'title:"\\\"باب القراءة\\\""^2 OR المصور',
                '\\(المنصور\\)',
                'المنصور\\ باحث',
                'وسائل الدعوة',
                '+المنصور -مزور',
            ]
            for query in supported:
                py_ast = parse_query_ast_backend(conn, query, 'python')
                c_ast = parse_query_ast_backend(conn, query, 'c')
                if c_ast != py_ast:
                    raise SystemExit(
                        'error: C AST simple-shape mismatch '
                        f'query={query!r} expected={py_ast!r} actual={c_ast!r}'
                    )
        finally:
            conn.close()

    print('ok: ast_c_simple_shapes')


if __name__ == '__main__':
    main()
