BEGIN;

PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS ingest_meta (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  schema_version TEXT NOT NULL,
  created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
  updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))
);

INSERT INTO ingest_meta (id, schema_version)
VALUES (1, '001')
ON CONFLICT(id) DO UPDATE SET
  schema_version = excluded.schema_version,
  updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now');

CREATE TABLE IF NOT EXISTS ingest_runs (
  run_id TEXT PRIMARY KEY,
  tier TEXT NOT NULL CHECK (tier IN ('smoke', 'parity', 'full')),
  manifest_version TEXT NOT NULL,
  manifest_sha256 TEXT NOT NULL,
  source_path TEXT NOT NULL,
  tokenizer TEXT NOT NULL DEFAULT 'sqlite_tokenizer_ar',
  status TEXT NOT NULL DEFAULT 'running' CHECK (status IN ('running', 'completed', 'failed')),
  started_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
  completed_at TEXT,
  error_message TEXT
);

CREATE INDEX IF NOT EXISTS idx_ingest_runs_tier_status ON ingest_runs (tier, status);

CREATE TABLE IF NOT EXISTS ingest_checkpoints (
  stream_name TEXT PRIMARY KEY CHECK (stream_name IN ('pages', 'titles')),
  run_id TEXT NOT NULL REFERENCES ingest_runs (run_id) ON DELETE CASCADE,
  last_book_id INTEGER NOT NULL DEFAULT 0,
  last_source_rowid INTEGER NOT NULL DEFAULT 0,
  last_target_rowid INTEGER NOT NULL DEFAULT 0,
  updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))
);

CREATE TABLE IF NOT EXISTS page_doc_map (
  rowid INTEGER PRIMARY KEY,
  book_id INTEGER NOT NULL,
  page_id INTEGER NOT NULL,
  page_no INTEGER,
  source_deleted INTEGER NOT NULL DEFAULT 0 CHECK (source_deleted IN (0, 1)),
  source_sha1 TEXT,
  ingested_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
  UNIQUE (book_id, page_id)
);

CREATE INDEX IF NOT EXISTS idx_page_doc_map_book_page ON page_doc_map (book_id, page_id, rowid);
CREATE INDEX IF NOT EXISTS idx_page_doc_map_book_rowid ON page_doc_map (book_id, rowid);

CREATE TABLE IF NOT EXISTS page_content_store (
  rowid INTEGER PRIMARY KEY REFERENCES page_doc_map (rowid) ON DELETE CASCADE,
  body TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS title_doc_map (
  rowid INTEGER PRIMARY KEY,
  book_id INTEGER NOT NULL,
  title_id INTEGER NOT NULL,
  title_level INTEGER,
  title_parent_id INTEGER,
  source_deleted INTEGER NOT NULL DEFAULT 0 CHECK (source_deleted IN (0, 1)),
  source_sha1 TEXT,
  ingested_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
  UNIQUE (book_id, title_id)
);

CREATE INDEX IF NOT EXISTS idx_title_doc_map_book_title ON title_doc_map (book_id, title_id, rowid);
CREATE INDEX IF NOT EXISTS idx_title_doc_map_book_rowid ON title_doc_map (book_id, rowid);

CREATE TABLE IF NOT EXISTS title_content_store (
  rowid INTEGER PRIMARY KEY REFERENCES title_doc_map (rowid) ON DELETE CASCADE,
  title TEXT NOT NULL
);

CREATE VIRTUAL TABLE IF NOT EXISTS page_fts USING fts5(
  body,
  tokenize='sqlite_tokenizer_ar',
  content='',
  detail='full'
);

CREATE VIRTUAL TABLE IF NOT EXISTS title_fts USING fts5(
  title,
  tokenize='sqlite_tokenizer_ar',
  content='',
  detail='full'
);

CREATE VIEW IF NOT EXISTS v_page_search_docs AS
SELECT
  rowid,
  book_id,
  page_id,
  page_no,
  source_deleted
FROM page_doc_map;

CREATE VIEW IF NOT EXISTS v_title_search_docs AS
SELECT
  rowid,
  book_id,
  title_id,
  title_level,
  title_parent_id,
  source_deleted
FROM title_doc_map;

COMMIT;
