#!/usr/bin/env python3
import argparse
import sqlite3
import statistics
import time
from pathlib import Path


PAIRS = {
    'one term': (
        "sqlite_tokenizer_ar_find_all_normalized_match_spans_json(text, 'قال', 1) != '[]'",
        "sqlite_tokenizer_ar_matches_normalized(text, '[\"قال\"]', 'any') = 1",
    ),
    'two terms': (
        "sqlite_tokenizer_ar_find_all_normalized_match_spans_json(text, 'قال', 1) != '[]' AND "
        "sqlite_tokenizer_ar_find_all_normalized_match_spans_json(text, 'الله', 1) != '[]'",
        "sqlite_tokenizer_ar_matches_normalized(text, '[\"قال\",\"الله\"]', 'all') = 1",
    ),
    'missing term': (
        "sqlite_tokenizer_ar_find_all_normalized_match_spans_json(text, 'قال', 1) != '[]' AND "
        "sqlite_tokenizer_ar_find_all_normalized_match_spans_json(text, 'غيرموجود', 1) != '[]'",
        "sqlite_tokenizer_ar_matches_normalized(text, '[\"قال\",\"غيرموجود\"]', 'all') = 1",
    ),
    'four terms': (
        "sqlite_tokenizer_ar_find_all_normalized_match_spans_json(text, 'قال', 1) != '[]' AND "
        "sqlite_tokenizer_ar_find_all_normalized_match_spans_json(text, 'الله', 1) != '[]' AND "
        "sqlite_tokenizer_ar_find_all_normalized_match_spans_json(text, 'العربية', 1) != '[]' AND "
        "sqlite_tokenizer_ar_find_all_normalized_match_spans_json(text, 'العلم', 1) != '[]'",
        "sqlite_tokenizer_ar_matches_normalized(text, '[\"قال\",\"الله\",\"العربية\",\"العلم\"]', 'all') = 1",
    ),
    'phrase': (
        "sqlite_tokenizer_ar_find_all_normalized_match_spans_json(text, 'من يريد', 1) != '[]'",
        "sqlite_tokenizer_ar_matches_normalized(text, '[\"من\",\"يريد\"]', 'phrase') = 1",
    ),
    'missing phrase': (
        "sqlite_tokenizer_ar_find_all_normalized_match_spans_json(text, 'يريد غيرموجود', 1) != '[]'",
        "sqlite_tokenizer_ar_matches_normalized(text, '[\"يريد\",\"غيرموجود\"]', 'phrase') = 1",
    ),
}

HONORIFIC_WHERE = "sqlite_tokenizer_ar_matches_normalized(text, '[\"رحمه\",\"الله\"]', 'phrase') = 1"


def median_ms(conn: sqlite3.Connection, sql: str, runs: int) -> tuple[int, float]:
    count = conn.execute(sql).fetchone()[0]
    conn.execute(sql).fetchone()
    samples = []
    for _ in range(runs):
        started = time.perf_counter()
        conn.execute(sql).fetchone()
        samples.append((time.perf_counter() - started) * 1000)
    return count, statistics.median(samples)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--extension', type=Path, default=Path(__file__).parents[1] / 'build/sqlite_tokenizer_ar.so')
    parser.add_argument('--rows', type=int, default=2_000)
    parser.add_argument('--runs', type=int, default=5)
    parser.add_argument('--lengths', default='1128,4973')
    args = parser.parse_args()

    conn = sqlite3.connect(':memory:')
    conn.enable_load_extension(True)
    conn.load_extension(str(args.extension.resolve()))
    conn.execute('CREATE TABLE pages(text)')
    sentence = 'قال الله في العربية إن من يريد العلم فليطلبه، وهذا نص صفحة من كتاب. '

    for text_length in map(int, args.lengths.split(',')):
        text = (sentence * (text_length // len(sentence) + 1))[:text_length]
        conn.execute('DELETE FROM pages')
        conn.executemany('INSERT INTO pages VALUES (?)', ((text,) for _ in range(args.rows)))
        print(f'\n{text_length} characters, {args.rows} rows')
        for name, (old_where, new_where) in PAIRS.items():
            old_count, old_ms = median_ms(conn, f'SELECT count(*) FROM pages WHERE {old_where}', args.runs)
            new_count, new_ms = median_ms(conn, f'SELECT count(*) FROM pages WHERE {new_where}', args.runs)
            assert old_count == new_count, f'{name}: {old_count} != {new_count}'
            print(f'{name:14} old={old_ms:8.1f} ms new={new_ms:8.1f} ms speedup={old_ms / new_ms:5.2f}x')

        honorific_text = ('قال الإمام ﵀ ' + text)[:text_length]
        conn.execute('DELETE FROM pages')
        conn.executemany('INSERT INTO pages VALUES (?)', ((honorific_text,) for _ in range(args.rows)))
        count, elapsed_ms = median_ms(conn, f'SELECT count(*) FROM pages WHERE {HONORIFIC_WHERE}', args.runs)
        assert count == args.rows
        print(f'{"honorific":14} new={elapsed_ms:8.1f} ms')
        old_count, old_ms = median_ms(conn, f'SELECT count(*) FROM pages WHERE {PAIRS["missing term"][0]}', args.runs)
        new_count, new_ms = median_ms(conn, f'SELECT count(*) FROM pages WHERE {PAIRS["missing term"][1]}', args.runs)
        assert old_count == new_count == 0
        print(f'{"honorific miss":14} old={old_ms:8.1f} ms new={new_ms:8.1f} ms speedup={old_ms / new_ms:5.2f}x')

    conn.close()


if __name__ == '__main__':
    main()
