"""Refresh vendored headers from the SQLite release pinned in mise.toml."""

import argparse
import hashlib
import io
from pathlib import Path
import re
import tomllib
from urllib.request import urlopen
import zipfile


def update_sqlite(root, check=False):
    with (root / 'mise.toml').open('rb') as source:
        ref = tomllib.load(source)['env']['SQLITE_TOKENIZER_AR_SQLITE_REF']
    if not re.fullmatch(r'version-\d+\.\d+\.\d+', ref):
        raise ValueError(f'Expected a SQLite release tag, got {ref!r}')
    version = ref.removeprefix('version-')
    vendor = root / 'vendor/sqlite'
    if check:
        if not re.search(rf'^#define SQLITE_VERSION\s+"{re.escape(version)}"$', (vendor / 'sqlite3.h').read_text(), re.M):
            raise ValueError(f'SQLite headers do not match {ref}; run mise run sqlite:update')
        return

    with urlopen(f'https://sqlite.org/releaselog/{version.replace(".", "_")}.html', timeout=30) as response:
        release = response.read().decode()
    year = re.search(rf'SQLite Release {re.escape(version)} On (\d{{4}})-\d{{2}}-\d{{2}}', release)[1]
    major, minor, patch = map(int, version.split('.'))
    number = major * 1000000 + minor * 10000 + patch * 100
    url = f'https://sqlite.org/{year}/sqlite-amalgamation-{number}.zip'
    with urlopen(url, timeout=30) as response:
        data = response.read()
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        prefix = f'sqlite-amalgamation-{number}/'
        expected = re.search(r'SHA3-256 for sqlite3.c:\s*([0-9a-f]{64})', release)[1]
        if hashlib.sha3_256(archive.read(prefix + 'sqlite3.c')).hexdigest() != expected:
            raise ValueError('SQLite archive does not match the official release checksum')
        headers = {name: archive.read(prefix + name) for name in ('sqlite3.h', 'sqlite3ext.h')}
    if not re.search(rf'^#define SQLITE_VERSION\s+"{re.escape(version)}"$', headers['sqlite3.h'].decode(), re.M):
        raise ValueError('Downloaded SQLite headers have an unexpected version')
    readme = f'''# SQLite headers

Unmodified public-domain headers from SQLite **{version}**, matching `SQLITE_TOKENIZER_AR_SQLITE_REF` in `mise.toml`.

- Source: {url}
- Archive SHA-256: `{hashlib.sha256(data).hexdigest()}`
'''
    for name, content in headers.items():
        readme += f'- `{name}` SHA-256: `{hashlib.sha256(content).hexdigest()}`\n'
    readme += '''
Desktop extension and Python wheel builds use these headers, not system SQLite headers. No SQLite runtime is bundled or linked into the extension. Android/WASM builds retain headers from their configured SQLite source; iOS uses its SDK headers.

Renovate's SQLite PRs automatically regenerate these files and rerun CI. For manual updates, change the pinned ref and run `mise run sqlite:update`. The updater verifies the amalgamation against the release's published SHA3-256 checksum before writing files. `mise run sqlite:check` checks the header version without downloading.
'''
    for name, content in headers.items():
        (vendor / name).write_bytes(content)
    (vendor / 'README.md').write_text(readme)
    print(f'Updated SQLite headers to {version}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='check the header version without downloading')
    update_sqlite(Path(__file__).resolve().parent.parent, parser.parse_args().check)
