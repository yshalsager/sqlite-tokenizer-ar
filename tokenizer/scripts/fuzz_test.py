#!/usr/bin/env python3
import argparse
import random
import sqlite3
from pathlib import Path


ARABIC_LETTERS = [chr(cp) for cp in range(0x0621, 0x063B)] + [chr(cp) for cp in range(0x0641, 0x064B)]
ARABIC_DIACRITICS = [chr(cp) for cp in range(0x064B, 0x0660)] + [chr(0x0670)]
ARABIC_DIGITS = [chr(cp) for cp in range(0x0660, 0x066A)] + [chr(cp) for cp in range(0x06F0, 0x06FA)]
ASCII_ALNUM = list('abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789')
WHITESPACE = [' ', '\t', '\n']
PUNCT = list(".,:;!?-_'/\\@#()[]{}<>") + [chr(0x060C), chr(0x061B), chr(0x061F), chr(0x066B), chr(0x066C)]
SYMBOLS = [chr(0x200C), chr(0x200D), chr(0x20AC), chr(0xFDFA), chr(0xFDFB), chr(0x2997), chr(0x2998), chr(0x2026)]

REGRESSION_CASES = [
    '_\u0625\u0646 \u0634\u0627\u0621 \u0627\u0644\u0644\u0647 \u062a\u0639\u0627\u0644\u0649_',
    '\u0639.\u0649.',
    'qur\'anic',
    'alammary4@hotmail.com',
    '2_\u062a\u0639\u0631\u064a\u0641',
    'mkh\u0661\u0663\u0668\u0664@gmail.com',
    'gusu\u0665\u0661\u0660\u0663',
]


def random_text(rng: random.Random, max_len: int) -> str:
    target = rng.randint(0, max_len)
    if target == 0:
        return ''

    buckets = [
        (ARABIC_LETTERS, 26),
        (ARABIC_DIACRITICS, 8),
        (ARABIC_DIGITS, 8),
        (ASCII_ALNUM, 28),
        (PUNCT, 18),
        (SYMBOLS, 5),
        (WHITESPACE, 7),
    ]
    weighted_pool: list[list[str]] = []
    for bucket, weight in buckets:
        weighted_pool.extend([bucket] * weight)

    out = []
    for _ in range(target):
        bucket = weighted_pool[rng.randint(0, len(weighted_pool) - 1)]
        out.append(bucket[rng.randint(0, len(bucket) - 1)])
    return ''.join(out)


def tokenize(conn: sqlite3.Connection, text: str) -> list[str]:
    conn.execute('DELETE FROM t')
    conn.execute('INSERT INTO t(rowid, content) VALUES (1, ?)', (text,))
    rows = conn.execute('SELECT term, offset FROM tv WHERE doc = 1 ORDER BY offset').fetchall()
    return [str(term) for term, _offset in rows]


def assert_deterministic(conn: sqlite3.Connection, text: str, label: str) -> None:
    first = tokenize(conn, text)
    second = tokenize(conn, text)
    if first != second:
        raise SystemExit(f'error: non-deterministic tokenization for {label}')


def main() -> None:
    parser = argparse.ArgumentParser(description='Fuzz tokenizer for crash-safety and deterministic output')
    parser.add_argument('--iterations', type=int, default=3000)
    parser.add_argument('--max-len', type=int, default=320)
    parser.add_argument('--seed', type=int, default=99)
    parser.add_argument(
        '--extension',
        default=str(Path(__file__).resolve().parents[1] / 'build' / 'sqlite_tokenizer_ar.so'),
    )
    args = parser.parse_args()

    if args.iterations < 1:
        raise SystemExit('error: --iterations must be >= 1')
    if args.max_len < 1:
        raise SystemExit('error: --max-len must be >= 1')

    extension_path = Path(args.extension).resolve()
    if not extension_path.exists():
        raise SystemExit(f'error: extension not found: {extension_path}')

    conn = sqlite3.connect(':memory:')
    rng = random.Random(args.seed)
    try:
        conn.enable_load_extension(True)
        conn.load_extension(str(extension_path))
        conn.execute("CREATE VIRTUAL TABLE t USING fts5(content, tokenize='sqlite_tokenizer_ar')")
        conn.execute("CREATE VIRTUAL TABLE tv USING fts5vocab(t, 'instance')")

        for i, case in enumerate(REGRESSION_CASES, start=1):
            assert_deterministic(conn, case, f'regression#{i}')

        for i in range(1, args.iterations + 1):
            text = random_text(rng, args.max_len)
            assert_deterministic(conn, text, f'fuzz#{i}')

        for size in (4096, 16384, 65536):
            assert_deterministic(conn, random_text(rng, size), f'long#{size}')

        print(f'ok: tokenizer_fuzz iterations={args.iterations} seed={args.seed}')
    finally:
        conn.close()


if __name__ == '__main__':
    main()
