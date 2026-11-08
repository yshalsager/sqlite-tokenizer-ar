import hashlib
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from scripts.update_sqlite import update_sqlite


class SqliteUpdateTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        (self.root / 'mise.toml').write_text("[env]\nSQLITE_TOKENIZER_AR_SQLITE_REF = 'version-3.54.1'\n")
        self.vendor = self.root / 'vendor/sqlite'
        self.vendor.mkdir(parents=True)
        (self.vendor / 'sqlite3.h').write_text('old header')
        self.header = b'#define SQLITE_VERSION "3.54.1"\n'
        source = b'SQLite amalgamation fixture'
        self.digest = hashlib.sha3_256(source).hexdigest()
        self.release = f'SQLite Release 3.54.1 On 2026-10-16\nSHA3-256 for sqlite3.c: {self.digest}'.encode()
        data = io.BytesIO()
        with zipfile.ZipFile(data, 'w') as archive:
            for name, content in [('sqlite3.h', self.header), ('sqlite3ext.h', b'extension header'), ('sqlite3.c', source)]:
                archive.writestr(f'sqlite-amalgamation-3540100/{name}', content)
        self.archive = data.getvalue()

    @patch('scripts.update_sqlite.urlopen')
    def test_update_and_check(self, download):
        with self.assertRaisesRegex(ValueError, 'run mise run sqlite:update'):
            update_sqlite(self.root, check=True)
        download.assert_not_called()
        download.side_effect = [io.BytesIO(self.release), io.BytesIO(self.archive)]
        update_sqlite(self.root)
        update_sqlite(self.root, check=True)
        self.assertEqual(download.call_count, 2)
        self.assertEqual((self.vendor / 'sqlite3.h').read_bytes(), self.header)
        self.assertEqual((self.vendor / 'sqlite3ext.h').read_bytes(), b'extension header')
        self.assertIn(hashlib.sha256(self.archive).hexdigest(), (self.vendor / 'README.md').read_text())

    @patch('scripts.update_sqlite.urlopen')
    def test_bad_checksum_preserves_headers(self, download):
        release = self.release.replace(self.digest.encode(), b'0' * 64)
        download.side_effect = [io.BytesIO(release), io.BytesIO(self.archive)]
        with self.assertRaisesRegex(ValueError, 'checksum'):
            update_sqlite(self.root)
        self.assertEqual((self.vendor / 'sqlite3.h').read_text(), 'old header')
        self.assertFalse((self.vendor / 'sqlite3ext.h').exists())


if __name__ == '__main__':
    unittest.main()
