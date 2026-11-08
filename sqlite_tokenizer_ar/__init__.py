"""Load the bundled Arabic FTS5 tokenizer into a SQLite connection."""

from pathlib import Path
import sys


def loadable_path():
    """Return the bundled library's full path for SQLite or other clients."""
    suffix = '.dll' if sys.platform == 'win32' else '.so'
    return str(Path(__file__).with_name(f'sqlite_tokenizer_ar{suffix}'))


def load(connection):
    """Load into this connection, then disable extension loading, even on failure."""
    connection.enable_load_extension(True)
    try:
        connection.load_extension(loadable_path())
    finally:
        connection.enable_load_extension(False)
