#!/usr/bin/env python3
import argparse
import base64
import json
import random
import shutil
import subprocess
import tempfile
from pathlib import Path
import glob
import os
import re
import sqlite3


ASCII_LETTERS = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ'
ASCII_DIGITS = '0123456789'
ARABIC_LETTERS = 'ابتثجحخدذرزسشصضطظعغفقكلمنهويىةأإآؤئ'
ARABIC_DIACRITICS = '\u064b\u064c\u064d\u064e\u064f\u0650\u0651\u0652\u0670'
ARABIC_DIGITS = '٠١٢٣٤٥٦٧٨٩'
SEPARATORS = [' ', '  ', '\n', '.', ',', '،', ';', '؛', '!', '?', '؟', ' (', ') ', ' - ']


def random_arabic_word(rng: random.Random, max_len: int) -> str:
    n = rng.randint(1, max(1, min(16, max_len)))
    out = []
    for _ in range(n):
        out.append(rng.choice(ARABIC_LETTERS))
        if rng.random() < 0.18:
            out.append(rng.choice(ARABIC_DIACRITICS))
    return ''.join(out)


def random_latin_word(rng: random.Random, max_len: int) -> str:
    n = rng.randint(1, max(1, min(16, max_len)))
    alphabet = ASCII_LETTERS + ASCII_DIGITS
    return ''.join(rng.choice(alphabet) for _ in range(n))


def random_digits(rng: random.Random, max_len: int) -> str:
    n = rng.randint(1, max(1, min(12, max_len)))
    digits = ARABIC_DIGITS if rng.random() < 0.5 else ASCII_DIGITS
    return ''.join(rng.choice(digits) for _ in range(n))


def make_random_text(rng: random.Random, max_len: int) -> str:
    token_count = rng.randint(1, 20)
    tokens = []
    for _ in range(token_count):
        roll = rng.random()
        if roll < 0.55:
            token = random_arabic_word(rng, max_len=max_len)
        elif roll < 0.85:
            token = random_latin_word(rng, max_len=max_len)
        else:
            token = random_digits(rng, max_len=max_len)
        tokens.append(token)

    text = tokens[0]
    for token in tokens[1:]:
        text += rng.choice(SEPARATORS) + token
    if len(text) > max_len:
        text = text[:max_len]
    return text or 'a'


def write_fixture(path: Path, count: int, seed: int, max_len: int) -> None:
    rng = random.Random(seed)
    lines = []
    for idx in range(count):
        text = make_random_text(rng, max_len=max_len)
        encoded = base64.b64encode(text.encode('utf-8')).decode('ascii')
        lines.append(f'rnd-{idx}\t{encoded}')
    path.write_text('\n'.join(lines) + '\n', encoding='utf-8')


def run_checked(cmd: list[str], cwd: Path) -> None:
    subprocess.run(cmd, check=True, cwd=str(cwd))


def load_jsonl(path: Path) -> list[dict]:
    rows: list[dict] = []
    for raw_line in path.read_text(encoding='utf-8').splitlines():
        line = raw_line.strip()
        if not line:
            continue
        rows.append(json.loads(line))
    return rows


def load_fixture_texts(path: Path) -> dict[str, str]:
    out: dict[str, str] = {}
    for raw_line in path.read_text(encoding='utf-8').splitlines():
        line = raw_line.strip()
        if not line or line.startswith('#'):
            continue
        parts = line.split('\t', 1)
        if len(parts) != 2:
            continue
        fixture_id, encoded = parts
        try:
            out[fixture_id] = base64.b64decode(encoded.encode('ascii')).decode('utf-8')
        except Exception:
            continue
    return out


def extract_terms(row: dict) -> list[str]:
    out: list[str] = []
    for token in row.get('tokens', []):
        term = str(token.get('term', '')).strip('_')
        if term:
            out.append(term)
    return out


def extract_terms_and_positions_from_oracle(row: dict) -> tuple[list[str], list[dict]]:
    terms: list[str] = []
    positions: list[dict] = []
    current_position = -1
    for token in row.get('tokens', []):
        term = str(token.get('term', '')).strip('_')
        if term == '':
            continue
        pos_inc = int(token.get('pos_inc', 1))
        current_position += pos_inc
        terms.append(term)
        positions.append({'term': term, 'position': current_position})
    return terms, positions


def extract_positions_from_sqlite_udf(conn: sqlite3.Connection, text: str) -> list[dict]:
    row = conn.execute('SELECT sqlite_tokenizer_ar_analyze_positions_json(?)', (text,)).fetchone()
    if row is None or row[0] in {None, ''}:
        return []
    payload = json.loads(str(row[0]))
    if not isinstance(payload, list):
        return []
    out: list[dict] = []
    for item in payload:
        if not isinstance(item, dict):
            continue
        term = str(item.get('term', '')).strip('_')
        if term == '':
            continue
        out.append({'term': term, 'position': int(item.get('position', 0))})
    return out


def java_major_version() -> int | None:
    try:
        output = subprocess.check_output(['javac', '-version'], stderr=subprocess.STDOUT, text=True).strip()
    except Exception:
        return None
    match = re.search(r'(\d+)(?:\.\d+)?', output)
    if not match:
        return None
    major = int(match.group(1))
    if major == 1:
        legacy_match = re.search(r'1\.(\d+)', output)
        if legacy_match:
            return int(legacy_match.group(1))
    return major


def ensure_lucene_classpath_available(repo_dir: Path) -> None:
    if 'LUCENE_CLASSPATH' in os.environ and os.environ['LUCENE_CLASSPATH'].strip():
        return
    local_jars = glob.glob(str(repo_dir / '.build-tools' / 'lucene' / '*.jar'))
    if local_jars:
        return
    opt_jars = glob.glob('/opt/lucene/*.jar')
    if opt_jars:
        return
    raise SystemExit(
        'error: Lucene jars not found. Set LUCENE_CLASSPATH or place jars in '
        f'{repo_dir / ".build-tools" / "lucene"} or /opt/lucene'
    )


def main() -> None:
    parser = argparse.ArgumentParser(description='Run deterministic random Lucene-vs-tokenizer analyzer parity check')
    parser.add_argument('--cases', type=int, default=500, help='Number of random input rows')
    parser.add_argument('--seed', type=int, default=1337, help='RNG seed')
    parser.add_argument('--max-len', type=int, default=120, help='Max random text length')
    parser.add_argument('--keep-artifacts', default='', help='Optional directory to keep generated TSV/JSONL')
    parser.add_argument('--strict', action='store_true', help='Fail if any mismatches are found')
    parser.add_argument('--check-positions', action='store_true', help='Also compare analyzer logical positions against Lucene pos_inc')
    parser.add_argument('--sample', type=int, default=5, help='Max mismatch samples to print')
    parser.add_argument(
        '--extension',
        default=str(Path(__file__).resolve().parents[1] / 'build' / 'sqlite_tokenizer_ar.so'),
        help='Tokenizer extension path',
    )
    args = parser.parse_args()

    repo_dir = Path(__file__).resolve().parents[2]
    conformance_dir = repo_dir / 'conformance'
    extension_path = Path(args.extension).resolve()
    if not extension_path.exists():
        raise SystemExit(f'error: tokenizer extension not found: {extension_path}')
    if shutil.which('javac') is None or shutil.which('java') is None:
        raise SystemExit('error: java toolchain not found (requires javac/java)')
    major = java_major_version()
    if major is None or major < 11:
        raise SystemExit('error: javac 11+ is required for analyzer oracle compilation')
    ensure_lucene_classpath_available(repo_dir)

    keep_dir = Path(args.keep_artifacts).resolve() if args.keep_artifacts else None
    if keep_dir is not None:
        keep_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix='lucene_random_parity_') as tmp_dir:
        tmp_root = Path(tmp_dir)
        fixture_tsv = tmp_root / 'inputs.random.tsv'
        oracle_jsonl = tmp_root / 'oracle.random.jsonl'
        sqlite_jsonl = tmp_root / 'sqlite.random.jsonl'

        write_fixture(fixture_tsv, count=args.cases, seed=args.seed, max_len=args.max_len)

        run_checked(
            [str(conformance_dir / 'scripts' / 'run_java_analyzer_oracle.sh'), str(fixture_tsv), str(oracle_jsonl)],
            cwd=conformance_dir,
        )
        run_checked(
            [
                'python3',
                str(conformance_dir / 'scripts' / 'run_sqlite_analyzer_oracle.py'),
                str(fixture_tsv),
                str(sqlite_jsonl),
                '--extension',
                str(extension_path),
            ],
            cwd=conformance_dir,
        )
        if keep_dir is not None:
            shutil.copy2(fixture_tsv, keep_dir / fixture_tsv.name)
            shutil.copy2(oracle_jsonl, keep_dir / oracle_jsonl.name)
            shutil.copy2(sqlite_jsonl, keep_dir / sqlite_jsonl.name)

        oracle_rows = load_jsonl(oracle_jsonl)
        sqlite_rows = load_jsonl(sqlite_jsonl)
        fixture_texts = load_fixture_texts(fixture_tsv)
        udf_conn: sqlite3.Connection | None = None
        if args.check_positions:
            udf_conn = sqlite3.connect(':memory:')
            udf_conn.enable_load_extension(True)
            udf_conn.load_extension(str(extension_path))
        row_count = min(len(oracle_rows), len(sqlite_rows))
        mismatches: list[dict] = []
        for idx in range(row_count):
            expected = oracle_rows[idx]
            actual = sqlite_rows[idx]
            if expected.get('id') != actual.get('id'):
                mismatches.append(
                    {
                        'row': idx + 1,
                        'id_expected': expected.get('id'),
                        'id_actual': actual.get('id'),
                        'reason': 'id_mismatch',
                    }
                )
                continue
            exp_terms, exp_positions = extract_terms_and_positions_from_oracle(expected)
            act_terms = extract_terms(actual)
            if exp_terms != act_terms:
                mismatches.append(
                    {
                        'row': idx + 1,
                        'id': expected.get('id'),
                        'reason': 'term_sequence_mismatch',
                        'expected_terms': exp_terms,
                        'actual_terms': act_terms,
                    }
                )
                continue
            if args.check_positions:
                if udf_conn is None:
                    raise SystemExit('error: internal sqlite connection missing for position checks')
                fixture_id = str(expected.get('id'))
                text = fixture_texts.get(fixture_id)
                if text is None:
                    mismatches.append({'row': idx + 1, 'id': fixture_id, 'reason': 'missing_fixture_text'})
                    continue
                act_positions = extract_positions_from_sqlite_udf(udf_conn, text)
                if exp_positions != act_positions:
                    mismatches.append(
                        {
                            'row': idx + 1,
                            'id': fixture_id,
                            'reason': 'position_sequence_mismatch',
                            'expected_positions': exp_positions,
                            'actual_positions': act_positions,
                        }
                    )
        if udf_conn is not None:
            udf_conn.close()

        if len(oracle_rows) != len(sqlite_rows):
            mismatches.append(
                {
                    'row': None,
                    'reason': 'row_count_mismatch',
                    'expected_rows': len(oracle_rows),
                    'actual_rows': len(sqlite_rows),
                }
            )

        if mismatches:
            print(f'warn: lucene_random_parity mismatches={len(mismatches)} rows={args.cases} seed={args.seed}')
            for item in mismatches[: max(args.sample, 0)]:
                print(f'warn: sample_mismatch {json.dumps(item, ensure_ascii=False)}')
            if args.strict:
                raise SystemExit('error: random parity mismatches found in strict mode')
        else:
            summary = f'ok: lucene_random_parity exact cases={args.cases} seed={args.seed} max_len={args.max_len}'
            if args.check_positions:
                summary += ' positions=on'
            print(summary)

    if not mismatches:
        return
    print(f'ok: lucene_random_parity completed with mismatches cases={args.cases} seed={args.seed} max_len={args.max_len}')


if __name__ == '__main__':
    main()
