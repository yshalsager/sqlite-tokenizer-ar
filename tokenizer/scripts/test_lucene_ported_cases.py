#!/usr/bin/env python3
import json
import sqlite3
import tempfile
from pathlib import Path


EMPTY_TERM_SENTINEL = '\ue000'


def analyze_terms(conn: sqlite3.Connection, text: str, tokenize_clause: str = "sqlite_tokenizer_ar") -> list[str]:
    conn.execute('DROP TABLE IF EXISTS t_port')
    conn.execute('DROP TABLE IF EXISTS tv_port')
    conn.execute(f"CREATE VIRTUAL TABLE t_port USING fts5(content, tokenize='{tokenize_clause}')")
    conn.execute("CREATE VIRTUAL TABLE tv_port USING fts5vocab(t_port, 'instance')")
    conn.execute('INSERT INTO t_port(rowid, content) VALUES(1, ?)', (text,))
    rows = conn.execute('SELECT term FROM tv_port WHERE doc = 1 ORDER BY offset').fetchall()
    out: list[str] = []
    for (term,) in rows:
        token = '' if term == EMPTY_TERM_SENTINEL else str(term)
        out.append(token)
    return out


def analyze_terms_from_vocab(conn: sqlite3.Connection, vocab_table: str, doc: int) -> list[str]:
    rows = conn.execute(f'SELECT term FROM {vocab_table} WHERE doc = ? ORDER BY offset', (doc,)).fetchall()
    out: list[str] = []
    for (term,) in rows:
        token = '' if term == EMPTY_TERM_SENTINEL else str(term)
        out.append(token)
    return out


def assert_terms(conn: sqlite3.Connection, label: str, text: str, expected: list[str], tokenize_clause: str = "sqlite_tokenizer_ar") -> None:
    actual = analyze_terms(conn, text, tokenize_clause=tokenize_clause)
    if actual != expected:
        raise SystemExit(f'error: {label}: expected={expected} actual={actual} input={text!r}')


def main() -> None:
    tokenizer_dir = Path(__file__).resolve().parents[1]
    extension_path = (tokenizer_dir / 'build' / 'sqlite_tokenizer_ar.so').resolve()
    if not extension_path.exists():
        raise SystemExit(f'error: tokenizer extension not found: {extension_path}')

    with tempfile.TemporaryDirectory(prefix='lucene_ported_cases_') as tmp_dir:
        db_path = Path(tmp_dir) / 'ported_cases.sqlite3'
        conn = sqlite3.connect(str(db_path))
        try:
            conn.enable_load_extension(True)
            conn.load_extension(str(extension_path))

            # Ported from TestArabicAnalyzer.testBasicFeatures/testEnglishInput/testDigits.
            assert_terms(conn, 'analyzer-kabir', 'كبير', ['كبير'])
            assert_terms(conn, 'analyzer-kabira', 'كبيرة', ['كبير'])
            assert_terms(conn, 'analyzer-mashrub', 'مشروب', ['مشروب'])
            assert_terms(conn, 'analyzer-mashrubat', 'مشروبات', ['مشروب'])
            assert_terms(conn, 'analyzer-amerikiyin', 'أمريكيين', ['امريك'])
            assert_terms(conn, 'analyzer-ameriki', 'امريكي', ['امريك'])
            assert_terms(conn, 'analyzer-kitab', 'كتاب', ['كتاب'])
            assert_terms(conn, 'analyzer-alkitab', 'الكتاب', ['كتاب'])
            assert_terms(conn, 'analyzer-stopwords', 'ما ملكت أيمانكم', ['ملكت', 'ايمانكم'])
            assert_terms(conn, 'analyzer-stopwords-2', 'الذين ملكت أيمانكم', ['ملكت', 'ايمانكم'])
            assert_terms(conn, 'analyzer-english', 'English text.', ['english', 'text'])
            assert_terms(conn, 'analyzer-non-arabic-single', 'English', ['english'])
            assert_terms(conn, 'analyzer-digits', '١٢٣٤', ['1234'])
            assert_terms(
                conn,
                'analyzer-custom-stopwords-english',
                'The quick brown fox.',
                ['quick', 'brown', 'fox'],
                tokenize_clause='sqlite_tokenizer_ar stopwords the stopwords and stopwords a',
            )
            assert_terms(conn, 'analyzer-custom-stopwords-replace-default', 'في كتاب', ['في', 'كتاب'], tokenize_clause='sqlite_tokenizer_ar stopwords ملكت')
            assert_terms(conn, 'analyzer-custom-stopwords-disabled', 'في كتاب', ['في', 'كتاب'], tokenize_clause='sqlite_tokenizer_ar disable_stopwords')
            assert_terms(conn, 'analyzer-custom-stopwords-applied', 'في كتاب', ['كتاب'], tokenize_clause='sqlite_tokenizer_ar stopwords في')

            # Ported from TestArabicAnalyzer.testWithStemExclusionSet (using tokenizer arg).
            assert_terms(
                conn,
                'analyzer-stem-exclusion',
                'كبيرة the quick ساهدهات',
                ['كبير', 'the', 'quick', 'ساهدهات'],
                tokenize_clause='sqlite_tokenizer_ar stem_exclusion ساهدهات',
            )
            assert_terms(
                conn,
                'analyzer-no-stem-exclusion',
                'كبيرة the quick ساهدهات',
                ['كبير', 'the', 'quick', 'ساهد'],
            )
            conn.execute('DROP TABLE IF EXISTS t_stem_reuse')
            conn.execute('DROP TABLE IF EXISTS tv_stem_reuse')
            conn.execute("CREATE VIRTUAL TABLE t_stem_reuse USING fts5(content, tokenize='sqlite_tokenizer_ar stem_exclusion ساهدهات')")
            conn.execute("CREATE VIRTUAL TABLE tv_stem_reuse USING fts5vocab(t_stem_reuse, 'instance')")
            conn.execute('INSERT INTO t_stem_reuse(rowid, content) VALUES(1, ?)', ('كبيرة the quick ساهدهات',))
            conn.execute('INSERT INTO t_stem_reuse(rowid, content) VALUES(2, ?)', ('كبيرة the quick ساهدهات',))
            stem_reuse_doc1 = analyze_terms_from_vocab(conn, 'tv_stem_reuse', 1)
            stem_reuse_doc2 = analyze_terms_from_vocab(conn, 'tv_stem_reuse', 2)
            expected_stem_reuse = ['كبير', 'the', 'quick', 'ساهدهات']
            if stem_reuse_doc1 != expected_stem_reuse:
                raise SystemExit(
                    f'error: analyzer-stem-exclusion-reuse-doc1: expected={expected_stem_reuse} actual={stem_reuse_doc1}'
                )
            if stem_reuse_doc2 != expected_stem_reuse:
                raise SystemExit(
                    f'error: analyzer-stem-exclusion-reuse-doc2: expected={expected_stem_reuse} actual={stem_reuse_doc2}'
                )

            # Ported from TestArabicAnalyzer.testReusableTokenStream.
            conn.execute('DROP TABLE IF EXISTS t_reuse')
            conn.execute('DROP TABLE IF EXISTS tv_reuse')
            conn.execute("CREATE VIRTUAL TABLE t_reuse USING fts5(content, tokenize='sqlite_tokenizer_ar')")
            conn.execute("CREATE VIRTUAL TABLE tv_reuse USING fts5vocab(t_reuse, 'instance')")
            conn.execute('INSERT INTO t_reuse(rowid, content) VALUES(1, ?)', ('كبير',))
            conn.execute('INSERT INTO t_reuse(rowid, content) VALUES(2, ?)', ('كبيرة',))
            reusable_doc1 = analyze_terms_from_vocab(conn, 'tv_reuse', 1)
            reusable_doc2 = analyze_terms_from_vocab(conn, 'tv_reuse', 2)
            if reusable_doc1 != ['كبير']:
                raise SystemExit(f'error: analyzer-reusable-token-stream-doc1: expected={[ "كبير" ]} actual={reusable_doc1}')
            if reusable_doc2 != ['كبير']:
                raise SystemExit(f'error: analyzer-reusable-token-stream-doc2: expected={[ "كبير" ]} actual={reusable_doc2}')

            # Ported from TestArabicNormalizationFilter (stem bypass via per-case exclusion).
            normalization_cases = [
                ('norm-alif-madda', 'آجن', 'اجن'),
                ('norm-alif-hamza-above', 'أحمد', 'احمد'),
                ('norm-alif-hamza-below', 'إعاذ', 'اعاذ'),
                ('norm-alif-maksura', 'بنى', 'بني'),
                ('norm-teh-marbuta', 'فاطمة', 'فاطمه'),
                ('norm-tatweel', 'روبرـــــت', 'روبرت'),
                ('norm-fatha', 'مَبنا', 'مبنا'),
                ('norm-kasra', 'علِي', 'علي'),
                ('norm-damma', 'بُوات', 'بوات'),
                ('norm-fathatan', 'ولداً', 'ولدا'),
                ('norm-kasratan', 'ولدٍ', 'ولد'),
                ('norm-dammatan', 'ولدٌ', 'ولد'),
                ('norm-sukun', 'نلْسون', 'نلسون'),
                ('norm-shaddah', 'هتميّ', 'هتمي'),
                ('norm-superscript-alef-preserved', 'هٰ', 'هٰ'),
            ]
            for label, text, normalized in normalization_cases:
                assert_terms(
                    conn,
                    label,
                    text,
                    [normalized],
                    tokenize_clause=f'sqlite_tokenizer_ar stem_exclusion {normalized}',
                )

            # Ported from TestArabicStemFilter core cases.
            stem_cases = [
                ('stem-al-prefix', 'الحسن', ['حسن']),
                ('stem-wal-prefix', 'والحسن', ['حسن']),
                ('stem-bal-prefix', 'بالحسن', ['حسن']),
                ('stem-kal-prefix', 'كالحسن', ['حسن']),
                ('stem-fal-prefix', 'فالحسن', ['حسن']),
                ('stem-ll-prefix', 'للاخر', ['اخر']),
                ('stem-wa-prefix', 'وحسن', ['حسن']),
                ('stem-ah-suffix', 'زوجها', ['زوج']),
                ('stem-an-suffix', 'ساهدان', ['ساهد']),
                ('stem-at-suffix', 'ساهدات', ['ساهد']),
                ('stem-wn-suffix', 'ساهدون', ['ساهد']),
                ('stem-yn-suffix', 'ساهدين', ['ساهد']),
                ('stem-yh-suffix', 'ساهديه', ['ساهد']),
                ('stem-yp-suffix', 'ساهدية', ['ساهد']),
                ('stem-h-suffix', 'ساهده', ['ساهد']),
                ('stem-p-suffix', 'ساهدة', ['ساهد']),
                ('stem-y-suffix', 'ساهدي', ['ساهد']),
                ('stem-combo-pref-suf', 'وساهدون', ['ساهد']),
                ('stem-combo-suf', 'ساهدهات', ['ساهد']),
                ('stem-shouldnt-stem', 'الو', ['الو']),
            ]
            for label, text, expected in stem_cases:
                assert_terms(conn, label, text, expected)

            assert_terms(
                conn,
                'stem-keyword-attribute-equivalent',
                'ساهدهات',
                ['ساهدهات'],
                tokenize_clause='sqlite_tokenizer_ar stem_exclusion ساهدهات',
            )

            # Tokenizer parity regressions discovered on full-corpus oracle diffs.
            assert_terms(conn, 'tok-arabic-apostrophe-join', "إ'لام الغرب", ["ا'لام", 'غرب'])
            assert_terms(conn, 'tok-latin-arabic-colon-join', 'EncycloPedique:قد بدأ', ['encyclopedique:قد', 'بدا'])
            assert_terms(conn, 'tok-arabic-digit-comma-join', 'ص٣٥٠،٣٥١', ['ص350،351'])
            assert_terms(conn, 'tok-arabic-digit-latin-join', 'من٢litera', ['من2litera'])
            assert_terms(conn, 'tok-semicolon-digit-join', '1;2', ['1;2'])
            assert_terms(conn, 'tok-semicolon-arabic-digit-join', '١;٢', ['1;2'])
            assert_terms(conn, 'tok-semicolon-alnum-digit-boundary-join', 'a1;2a', ['a1;2a'])
            assert_terms(conn, 'tok-semicolon-alpha-split', 'abc;20582', ['abc', '20582'])
            assert_terms(conn, 'tok-arabic-semicolon-split', '1؛2', ['1', '2'])
            assert_terms(
                conn,
                'tok-dot-numeric-to-alpha-then-numeric-split',
                '٢١٨.91iDHbtNo.20920،٠٥٧٧٢٣',
                ['218.91idhbtno', '20920،057723'],
            )
            assert_terms(conn, 'tok-division-sign-split', 'رم÷اح', ['رم', 'اح'])
            assert_terms(conn, 'tok-soft-hyphen-split', 'abc\u00addef', ['abc', 'def'])
            assert_terms(conn, 'tok-combining-mark-suppressed', 'الرياض \u0308الرابعة', ['رياض', 'رابع'])

            long_ascii = 'a' * 300
            assert_terms(conn, 'tok-standard-max-token-length-split', long_ascii, ['a' * 255, 'a' * 45])

            # UDF edge checks (query-compat relies on these C helpers).
            norm_empty = conn.execute("SELECT sqlite_tokenizer_ar_normalize('')").fetchone()
            if norm_empty is None or norm_empty[0] != '':
                raise SystemExit(f"error: udf-normalize-empty: expected='' actual={norm_empty}")
            analyze_empty = conn.execute("SELECT sqlite_tokenizer_ar_analyze_json('')").fetchone()
            if analyze_empty is None or analyze_empty[0] != '[]':
                raise SystemExit(f"error: udf-analyze-empty: expected='[]' actual={analyze_empty}")
            analyze_tatweel = conn.execute("SELECT sqlite_tokenizer_ar_analyze_json('ــــ')").fetchone()
            if analyze_tatweel is None or analyze_tatweel[0] != '[]':
                raise SystemExit(f"error: udf-analyze-tatweel-only: expected='[]' actual={analyze_tatweel}")
            stem_empty = conn.execute("SELECT sqlite_tokenizer_ar_stem('')").fetchone()
            if stem_empty is None or stem_empty[0] != '':
                raise SystemExit(f"error: udf-stem-empty: expected='' actual={stem_empty}")
            stem_non_arabic = conn.execute("SELECT sqlite_tokenizer_ar_stem('English')").fetchone()
            if stem_non_arabic is None or stem_non_arabic[0] != 'English':
                raise SystemExit(f"error: udf-stem-non-arabic: expected='English' actual={stem_non_arabic}")
            stem_arabic = conn.execute("SELECT sqlite_tokenizer_ar_stem('ساهدهات')").fetchone()
            if stem_arabic is None or stem_arabic[0] != 'ساهد':
                raise SystemExit(f"error: udf-stem-arabic: expected='ساهد' actual={stem_arabic}")
            analyze_positions = conn.execute("SELECT sqlite_tokenizer_ar_analyze_positions_json('المنصور في اللغة')").fetchone()
            if analyze_positions is None:
                raise SystemExit('error: udf-analyze-positions: missing result row')
            analyze_positions_payload = json.loads(str(analyze_positions[0]))
            expected_positions = [{'term': 'منصور', 'position': 0}, {'term': 'لغ', 'position': 2}]
            if analyze_positions_payload != expected_positions:
                raise SystemExit(
                    'error: udf-analyze-positions: expected='
                    f'{expected_positions} actual={analyze_positions_payload}'
                )
            analyze_positions_empty = conn.execute("SELECT sqlite_tokenizer_ar_analyze_positions_json('في')").fetchone()
            if analyze_positions_empty is None or analyze_positions_empty[0] != '[]':
                raise SystemExit(f"error: udf-analyze-positions-stopword-only: expected='[]' actual={analyze_positions_empty}")

            print('ok: lucene ported tokenizer cases')
        finally:
            conn.close()


if __name__ == '__main__':
    main()
