#!/usr/bin/env python3
import sqlite3
import struct
import tempfile
from pathlib import Path

from generate_compile_baseline import setup_fixture_db
from python_reference_helpers import build_norm_inverse_cache_python, byte4_to_int, int_to_byte4


def main() -> None:
    query_compat_dir = Path(__file__).resolve().parents[1]
    root = query_compat_dir.parent
    schema_path = (root / 'ingester' / 'sql' / '001_canonical_schema.sql').resolve()
    extension_path = (root / 'tokenizer' / 'build' / 'sqlite_tokenizer_ar.so').resolve()
    if not schema_path.exists():
        raise SystemExit(f'error: schema not found: {schema_path}')
    if not extension_path.exists():
        raise SystemExit(f'error: tokenizer extension not found: {extension_path}')

    with tempfile.TemporaryDirectory(prefix='norm_helpers_udf_test_') as tmp_dir:
        db_path = Path(tmp_dir) / 'helpers.sqlite3'
        conn = sqlite3.connect(str(db_path))
        try:
            setup_fixture_db(conn, schema_path, extension_path)

            for value in [0, 1, 7, 23, 24, 25, 100, 1_000, 50_000]:
                row = conn.execute('SELECT sqlite_tokenizer_ar_int_to_byte4(?)', (value,)).fetchone()
                actual = -1 if row is None or row[0] is None else int(row[0])
                expected = int_to_byte4(value)
                if actual != expected:
                    raise SystemExit(
                        'error: int_to_byte4 UDF mismatch '
                        f'value={value} expected={expected} actual={actual}'
                    )

            for encoded in range(256):
                row = conn.execute('SELECT sqlite_tokenizer_ar_byte4_to_int(?)', (encoded,)).fetchone()
                actual = -1 if row is None or row[0] is None else int(row[0])
                expected = byte4_to_int(encoded)
                if actual != expected:
                    raise SystemExit(
                        'error: byte4_to_int UDF mismatch '
                        f'encoded={encoded} expected={expected} actual={actual}'
                    )

            cases = [(1.2, 0.75, 0.0), (1.2, 0.75, 1.0), (1.2, 0.75, 4.0), (1.2, 0.75, 15.5)]
            for k1, b, avgdl in cases:
                row = conn.execute(
                    'SELECT sqlite_tokenizer_ar_norm_inverse_cache_blob(?, ?, ?)',
                    (k1, b, avgdl),
                ).fetchone()
                blob = None if row is None else row[0]
                if blob is None:
                    raise SystemExit(
                        'error: norm_inverse_cache_blob returned NULL '
                        f'for k1={k1} b={b} avgdl={avgdl}'
                    )
                raw = bytes(blob)
                if len(raw) != 256 * 4:
                    raise SystemExit(
                        'error: norm_inverse_cache_blob size mismatch '
                        f'expected={256*4} actual={len(raw)}'
                    )
                actual = list(struct.unpack('<256f', raw))
                expected = build_norm_inverse_cache_python(k1, b, avgdl)
                for idx, (lhs, rhs) in enumerate(zip(actual, expected)):
                    if abs(float(lhs) - float(rhs)) > 1e-7:
                        raise SystemExit(
                            'error: norm_inverse_cache_blob mismatch '
                            f'k1={k1} b={b} avgdl={avgdl} idx={idx} expected={rhs} actual={lhs}'
                        )
        finally:
            conn.close()

    print('ok: norm_helpers_udf')


if __name__ == '__main__':
    main()
