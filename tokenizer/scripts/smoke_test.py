#!/usr/bin/env python3
import sqlite3
from pathlib import Path


def assert_count(conn: sqlite3.Connection, query: str, expected: int) -> None:
    row = conn.execute('SELECT count(*) FROM t WHERE t MATCH ?', (query,)).fetchone()
    actual = 0 if row is None else int(row[0])
    if actual != expected:
        raise SystemExit(f"error: query={query!r} expected={expected} got={actual}")


def assert_table_count(conn: sqlite3.Connection, table: str, query: str, expected: int) -> None:
    row = conn.execute(f'SELECT count(*) FROM {table} WHERE {table} MATCH ?', (query,)).fetchone()
    actual = 0 if row is None else int(row[0])
    if actual != expected:
        raise SystemExit(f"error: table={table} query={query!r} expected={expected} got={actual}")


def assert_table_row_match(conn: sqlite3.Connection, table: str, rowid: int, query: str) -> None:
    row = conn.execute(f'SELECT count(*) FROM {table} WHERE rowid = ? AND {table} MATCH ?', (rowid, query)).fetchone()
    actual = 0 if row is None else int(row[0])
    if actual != 1:
        raise SystemExit(f"error: table={table} rowid={rowid} query={query!r} expected_match=1 got={actual}")


def assert_term_doc_count(conn: sqlite3.Connection, term: str, expected: int) -> None:
    row = conn.execute('SELECT count(*) FROM tv WHERE term = ?', (term,)).fetchone()
    actual = 0 if row is None else int(row[0])
    if actual != expected:
        raise SystemExit(f"error: term={term!r} expected_docs={expected} got={actual}")


def assert_value(conn: sqlite3.Connection, sql: str, params: tuple, expected, label: str) -> None:
    row = conn.execute(sql, params).fetchone()
    actual = None if row is None else row[0]
    if actual != expected:
        raise SystemExit(f'error: {label} expected={expected!r} got={actual!r}')


def main() -> None:
    tokenizer_dir = Path(__file__).resolve().parent.parent
    extension_path = tokenizer_dir / 'build' / 'sqlite_tokenizer_ar.so'
    if not extension_path.exists():
        raise SystemExit(f'error: extension not found: {extension_path}')

    conn = sqlite3.connect(':memory:')
    try:
        conn.enable_load_extension(True)
        conn.load_extension(str(extension_path))
        conn.execute("CREATE VIRTUAL TABLE t USING fts5(content, tokenize='sqlite_tokenizer_ar')")
        conn.execute("INSERT INTO t(content) VALUES ('الصلاة في الإسلام')")
        conn.execute("INSERT INTO t(content) VALUES ('الزكاة ركن')")
        conn.execute("INSERT INTO t(content) VALUES ('قُرْآن كريم')")
        conn.execute("INSERT INTO t(content) VALUES ('الفتى الصالح')")
        conn.execute("INSERT INTO t(content) VALUES ('طبعة ١٢٣')")
        conn.execute("INSERT INTO t(content) VALUES ('والكتاب مفيد')")
        conn.execute("INSERT INTO t(content) VALUES ('كتابها جديد')")
        conn.execute("INSERT INTO t(content) VALUES ('___')")
        conn.execute("INSERT INTO t(content) VALUES ('ـــــ')")
        conn.execute("INSERT INTO t(content) VALUES ('البريد الالكتروني:ahmd577@gmile.com')")
        conn.execute("INSERT INTO t(content) VALUES ('السنن٢:ولكن يصلي')")
        conn.execute("INSERT INTO t(content) VALUES ('مرجع ص350،351 وطبعة ط16،1408')")
        conn.execute("INSERT INTO t(content) VALUES (?)", ("والعجيب أن إ'لام الغرب",))
        conn.execute("INSERT INTO t(content) VALUES ('توحيد من2litera في الاشتقاق')")
        conn.execute("INSERT INTO t(content) VALUES ('موسوعية encyclopedique:قد بدأت')")
        conn.execute("INSERT INTO t(content) VALUES ('الرياض ̈الرابعة')")
        conn.execute("INSERT INTO t(content) VALUES ('فصولٍ: \u00ad\u00ad\u00ad · الفصل الأول')")
        conn.execute("INSERT INTO t(content) VALUES ('كالرم÷اح والطثري')")

        assert_count(conn, 'الصلاة', 1)
        assert_count(conn, 'الاسلام', 1)
        assert_count(conn, 'الزكاه', 1)
        assert_count(conn, 'قران', 1)
        assert_count(conn, 'الفتي', 1)
        assert_count(conn, '123', 1)
        assert_count(conn, 'في', 0)
        assert_count(conn, 'كتاب', 2)
        assert_count(conn, '___', 0)

        conn.execute("CREATE VIRTUAL TABLE t_honorific_off USING fts5(content, tokenize='sqlite_tokenizer_ar')")
        conn.execute("INSERT INTO t_honorific_off(content) VALUES ('قال ﵀')")
        assert_table_count(conn, 't_honorific_off', 'رحمه', 0)

        honorific_cases = [
            ('﯃', 'جل وعلا'),
            ('﯄', 'دامت بركاتهم'),
            ('﯅', 'رحمة الله تعالى عليه'),
            ('﯆', 'رحمة الله عليهم'),
            ('﯇', 'رحمة الله عليهما'),
            ('﯈', 'رحمهم الله تعالى'),
            ('﯉', 'رحمهما الله'),
            ('﯊', 'رحمهما الله تعالى'),
            ('﯋', 'رضي الله تعالى عنهم'),
            ('﯌', 'حفظه الله'),
            ('﯍', 'حفظه الله تعالى'),
            ('﯎', 'حفظهم الله تعالى'),
            ('﯏', 'حفظهما الله تعالى'),
            ('﯐', 'صلى الله تعالى عليه وسلم'),
            ('﯑', 'عجل الله فرجه الشريف'),
            ('﯒', 'عليه الرحمة'),
            ('﵀', 'رحمه الله'),
            ('﵁', 'رضي الله عنه'),
            ('﵂', 'رضي الله عنها'),
            ('﵃', 'رضي الله عنهم'),
            ('﵄', 'رضي الله عنهما'),
            ('﵅', 'رضي الله عنهن'),
            ('﵆', 'صلى الله عليه وآله'),
            ('﵇', 'عليه السلام'),
            ('﵈', 'عليهم السلام'),
            ('﵉', 'عليهما السلام'),
            ('﵊', 'عليه الصلاة والسلام'),
            ('﵋', 'قدس سره'),
            ('﵌', 'صلى الله عليه وآله وسلم'),
            ('﵍', 'عليها السلام'),
            ('﵎', 'تبارك وتعالى'),
            ('﵏', 'رحمهم الله'),
            ('﶐', 'رحمة الله عليه'),
            ('﶑', 'رحمة الله عليها'),
            ('﷈', 'رحمه الله تعالى'),
            ('﷉', 'رضي الله تعالى عنه'),
            ('﷊', 'رضي الله تعالى عنها'),
            ('﷋', 'رضي الله تعالى عنهما'),
            ('﷌', 'صلى الله عليه وعلى آله وسلم'),
            ('﷍', 'عجل الله تعالى فرجه الشريف'),
            ('﷎', 'كرم الله وجهه'),
            ('﷏', 'سلامه علينا'),
            ('ﷺ', 'صلى الله عليه وسلم'),
            ('ﷻ', 'جل جلاله'),
            ('﷽', 'بسم الله الرحمن الرحيم'),
            ('﷾', 'سبحانه وتعالى'),
            ('﷿', 'عز وجل'),
        ]
        conn.execute("CREATE VIRTUAL TABLE t_honorific_on USING fts5(content, tokenize='sqlite_tokenizer_ar honorific_expansions')")
        for rowid, (glyph, phrase) in enumerate(honorific_cases, 1):
            conn.execute('INSERT INTO t_honorific_on(rowid, content) VALUES (?, ?)', (rowid, f'قال {glyph}'))
            assert_table_row_match(conn, 't_honorific_on', rowid, f'"{phrase}"')

        conn.execute("CREATE VIRTUAL TABLE tv USING fts5vocab(t, 'row')")
        assert_term_doc_count(conn, 'الكتروني:ahmd577', 1)
        assert_term_doc_count(conn, 'ahmd577', 0)
        assert_term_doc_count(conn, 'سنن2', 1)
        assert_term_doc_count(conn, 'لكن', 1)
        assert_term_doc_count(conn, 'سنن2:ولكن', 0)
        assert_term_doc_count(conn, 'ص350،351', 1)
        assert_term_doc_count(conn, 'ط16،1408', 1)
        assert_term_doc_count(conn, "ا'لام", 1)
        assert_term_doc_count(conn, 'من2litera', 1)
        assert_term_doc_count(conn, 'encyclopedique:قد', 1)
        assert_term_doc_count(conn, '÷', 0)
        assert_term_doc_count(conn, '\u00ad\u00ad\u00ad', 0)
        assert_term_doc_count(conn, '̈', 0)
        row_empty = conn.execute("SELECT count(*) FROM tv WHERE term = ?", ('\ue000',)).fetchone()
        if row_empty is None or int(row_empty[0]) < 1:
            raise SystemExit('error: expected empty-token sentinel for tatweel-only input')

        assert_value(
            conn,
            "SELECT sqlite_tokenizer_ar_analyze_json(?)",
            ('الذين ملكت أيمانكم',),
            '["ملكت","ايمانكم"]',
            'analyze_json stopword pipeline',
        )
        assert_value(
            conn,
            "SELECT sqlite_tokenizer_ar_normalize(?, 1, 1, 1, 1, 1, 0)",
            ('قُرْآن ١٢٣ مدرسة',),
            'قران 123 مدرسه',
            'normalize folded forms',
        )
        assert_value(
            conn,
            "SELECT sqlite_tokenizer_ar_normalize(?, 1, 1, 1, 1, 0, 1)",
            ('*قُر?آن*',),
            '*قر?ان*',
            'normalize wildcard preserve metacharacters',
        )
        assert_value(
            conn,
            "SELECT sqlite_tokenizer_ar_has_sensitive_forms(?, 1, 1, 1, 1)",
            ('قُرْآن ١٢٣ مدرسة',),
            1,
            'strict sensitivity positive',
        )
        assert_value(
            conn,
            "SELECT sqlite_tokenizer_ar_has_sensitive_forms(?, 1, 1, 1, 1)",
            ('كتاب مفيد',),
            0,
            'strict sensitivity negative',
        )
        assert_value(
            conn,
            "SELECT sqlite_tokenizer_ar_highlight_normalized_matches(?, ?, ?, char(0xE000), char(0xE001), ?)",
            ('من يريد برجوعه فيها', '["من","يريد"]', 'all', 8),
            '\ue000من\ue001 \ue000يريد\ue001 برجوعه فيها',
            'highlight normalized all terms',
        )
        assert_value(
            conn,
            "SELECT sqlite_tokenizer_ar_highlight_normalized_matches(?, ?, ?, char(0xE000), char(0xE001), ?)",
            ('قال برجوعه فيها ثم رجع', '["برجوعه","فيها"]', 'phrase', 8),
            'قال \ue000برجوعه فيها\ue001 ثم رجع',
            'highlight normalized phrase',
        )
        assert_value(
            conn,
            "SELECT sqlite_tokenizer_ar_highlight_normalized_matches(?, ?, ?, char(0xE000), char(0xE001), ?)",
            ('اللغة العربيّة مفيدة', '["العربية"]', 'all', 8),
            'اللغة \ue000العربيّة\ue001 مفيدة',
            'highlight normalized diacritics',
        )
        assert_value(
            conn,
            "SELECT sqlite_tokenizer_ar_highlight_normalized_matches(?, ?, ?, char(0xE000), char(0xE001), ?)",
            ('قال الإمام ﵀ في كتابه', '["رحمه الله"]', 'phrase', 8),
            'قال الإمام \ue000﵀\ue001 في كتابه',
            'highlight normalized honorific phrase glyph',
        )
        assert_value(
            conn,
            "SELECT sqlite_tokenizer_ar_highlight_analyzed_matches(?, ?, ?, char(0xE000), char(0xE001), ?)",
            ('قال الإمام ﵀ وكان لسان أهل الجنة عربي', '["عرب"]', 'all', 8),
            'قال الإمام ﵀ وكان لسان أهل الجنة \ue000عربي\ue001',
            'highlight analyzed stemmed source token',
        )
        assert_value(
            conn,
            "SELECT sqlite_tokenizer_ar_highlight_analyzed_matches(?, sqlite_tokenizer_ar_analyze_json(?), ?, char(0xE000), char(0xE001), ?, ?)",
            ('قال برجوعه فيها ثم رجع', 'برجوعه فيها', 'all', 8, '["برجوعه","فيها"]'),
            'قال \ue000برجوعه\ue001 \ue000فيها\ue001 ثم رجع',
            'highlight analyzed with raw stopword display term',
        )
        assert_value(
            conn,
            "SELECT sqlite_tokenizer_ar_highlight_analyzed_matches(?, ?, ?, char(0xE000), char(0xE001), ?)",
            ('قال الإمام ﵀', '["رحم","له"]', 'all', 8),
            'قال الإمام \ue000﵀\ue001',
            'highlight analyzed honorific source glyph',
        )
        assert_value(
            conn,
            "SELECT sqlite_tokenizer_ar_find_all_analyzed_match_spans_json(?, ?, ?, ?)",
            ('قال عربي', '["عرب"]', 'all', 8),
            '[{"start":4,"end":8}]',
            'find analyzed stemmed source token spans',
        )
        assert_value(
            conn,
            "SELECT sqlite_tokenizer_ar_highlight_normalized_matches(?, ?, ?, char(0xE000), char(0xE001), ?)",
            ('قال الإمام ﷿', '["وجل"]', 'any', 8),
            'قال الإمام \ue000﷿\ue001',
            'highlight normalized honorific any glyph',
        )
        assert_value(
            conn,
            "SELECT sqlite_tokenizer_ar_highlight_normalized_matches(?, ?, ?, char(0xE000), char(0xE001), ?)",
            ('قال الإمام ﵀ في كتابه', '["الإمام","رحمه"]', 'all', 8),
            'قال \ue000الإمام\ue001 \ue000﵀\ue001 في كتابه',
            'highlight normalized honorific all glyph',
        )
        assert_value(
            conn,
            "SELECT sqlite_tokenizer_ar_highlight_normalized_matches(?, ?, ?, char(0xE000), char(0xE001), ?)",
            ('من يريد', '["من","غائب"]', 'all', 8),
            None,
            'highlight normalized all missing term',
        )
        try:
            conn.execute(
                "SELECT sqlite_tokenizer_ar_highlight_normalized_matches(?, ?, ?, char(0xE000), char(0xE001), ?)",
                ('من يريد', '[] trailing', 'any', 8),
            ).fetchone()
        except sqlite3.OperationalError as exc:
            if 'invalid terms_json' not in str(exc):
                raise
        else:
            raise SystemExit('error: highlight normalized invalid empty JSON suffix accepted')

        assert_value(
            conn,
            "SELECT sqlite_tokenizer_ar_highlight_normalized_matches(?, ?, ?, char(0xE000), char(0xE001), ?)",
            ('😀', '["\\uD83D\\uDE00"]', 'any', 8),
            '\ue000😀\ue001',
            'highlight normalized JSON surrogate pair',
        )

        assert_value(conn, "SELECT sqlite_tokenizer_ar_levenshtein('', 'abc', 1)", (), 2, 'levenshtein empty left capped')
        assert_value(conn, "SELECT sqlite_tokenizer_ar_levenshtein('abc', '', 1)", (), 2, 'levenshtein empty right capped')
        assert_value(conn, "SELECT sqlite_tokenizer_ar_levenshtein('', 'abc', 5)", (), 3, 'levenshtein empty left uncapped')
        assert_value(conn, "SELECT sqlite_tokenizer_ar_levenshtein('aa', 'bbbb', 2)", (), 3, 'levenshtein final distance capped')
        assert_value(conn, "SELECT sqlite_tokenizer_ar_levenshtein('abc', 'xyzuvw', 3)", (), 4, 'levenshtein final distance capped at max plus one')

        try:
            conn.execute("CREATE VIRTUAL TABLE t_bad USING fts5(content, tokenize='sqlite_tokenizer_ar disable_stopword')")
        except sqlite3.OperationalError:
            pass
        else:
            raise SystemExit('error: unknown tokenizer option accepted')

        conn.execute(
            "CREATE VIRTUAL TABLE t_excl_surface USING fts5(content, tokenize='sqlite_tokenizer_ar stem_exclusion مدرسة')"
        )
        conn.execute("INSERT INTO t_excl_surface(content) VALUES ('مدرسة')")
        conn.execute("CREATE VIRTUAL TABLE tv_excl_surface USING fts5vocab(t_excl_surface, 'row')")
        row_excl_surface = conn.execute("SELECT count(*) FROM tv_excl_surface WHERE term = ?", ('مدرسه',)).fetchone()
        row_excl_surface_stem = conn.execute("SELECT count(*) FROM tv_excl_surface WHERE term = ?", ('مدرس',)).fetchone()
        if (row_excl_surface is None or row_excl_surface[0] != 1) or (
            row_excl_surface_stem is None or row_excl_surface_stem[0] != 0
        ):
            raise SystemExit('error: normalized stem-exclusion surface form mismatch')

        conn.execute(
            "CREATE VIRTUAL TABLE t_excl USING fts5(content, tokenize='sqlite_tokenizer_ar stem_exclusion كتابها')"
        )
        conn.execute("INSERT INTO t_excl(content) VALUES ('كتابها جديد')")
        row_excl_exact = conn.execute("SELECT count(*) FROM t_excl WHERE t_excl MATCH 'كتابها'").fetchone()
        row_excl_stem = conn.execute("SELECT count(*) FROM t_excl WHERE t_excl MATCH 'كتاب'").fetchone()
        if (row_excl_exact is None or row_excl_exact[0] != 1) or (row_excl_stem is None or row_excl_stem[0] != 0):
            raise SystemExit('error: stem exclusion behavior mismatch')

        print('ok: tokenizer_smoke')
    finally:
        conn.close()


if __name__ == '__main__':
    main()
