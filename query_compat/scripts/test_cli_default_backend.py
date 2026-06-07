#!/usr/bin/env python3

import sqlite_query_compat


def main() -> None:
    parser = sqlite_query_compat.build_cli_parser()
    parsed = parser.parse_args(['--db', '/tmp/example.sqlite3', '--query', 'المنصور'])
    if parsed.backend != 'c':
        raise SystemExit(f'error: expected default CLI backend=c, got backend={parsed.backend!r}')
    print('ok: cli_default_backend')


if __name__ == '__main__':
    main()
