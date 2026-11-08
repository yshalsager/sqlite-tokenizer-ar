"""Run against an installed wheel: uv run --with dist/<wheel> python -I tests/test_python_wheel.py."""

import sqlite3
import unittest
from unittest.mock import Mock, call

import sqlite_tokenizer_ar


class PythonWheelTests(unittest.TestCase):
    def test_load(self):
        with sqlite3.connect(':memory:') as db:
            sqlite_tokenizer_ar.load(db)
            db.execute("CREATE VIRTUAL TABLE docs USING fts5(body, tokenize='sqlite_tokenizer_ar')")
            db.execute("INSERT INTO docs VALUES ('قُرْآن كريم')")
            self.assertEqual(db.execute("SELECT count(*) FROM docs WHERE docs MATCH 'قران'").fetchone()[0], 1)
            self.assertEqual(db.execute("SELECT sqlite_tokenizer_ar_normalize('قُرْآن ١٢٣')").fetchone()[0], 'قران 123')
            with self.assertRaises(sqlite3.OperationalError):
                db.load_extension(sqlite_tokenizer_ar.loadable_path())

    def test_failed_load_disables_loading(self):
        connection = Mock()
        connection.load_extension.side_effect = sqlite3.OperationalError('load failed')
        with self.assertRaisesRegex(sqlite3.OperationalError, 'load failed'):
            sqlite_tokenizer_ar.load(connection)
        self.assertEqual(connection.enable_load_extension.call_args_list, [call(True), call(False)])


if __name__ == '__main__':
    unittest.main()
