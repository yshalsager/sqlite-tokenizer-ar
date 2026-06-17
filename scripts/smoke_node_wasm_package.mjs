import sqlite3InitModule from '../dist/sqlite3-node.mjs'

const sqlite3 = await sqlite3InitModule()
const db = new sqlite3.oo1.DB(':memory:')

try {
  const normalized = db.selectValue("SELECT sqlite_tokenizer_ar_normalize('ٱلْعَرَبِيَّة')")
  if (normalized !== 'ٱلعربيه') throw new Error(`normalize mismatch: ${normalized}`)

  db.exec("CREATE VIRTUAL TABLE t USING fts5(x, tokenize='sqlite_tokenizer_ar honorific_expansions')")
  db.exec("INSERT INTO t VALUES ('قال الإمام ﵀')")
  const count = db.selectValue('SELECT count(*) FROM t WHERE t MATCH \'"رحمه الله"\'')
  if (count !== 1) throw new Error(`honorific match mismatch: ${count}`)
} finally {
  db.close()
}

console.log('ok: node_wasm_package')
