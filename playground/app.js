const SQLITE_WASM_MODULE_URL = './sqlite-wasm-custom/sqlite3.mjs'
const SQLITE_WASM_CUSTOM_PATH = './sqlite-wasm-custom/sqlite3.wasm'
const SQLITE_WASM_CUSTOM_URL = new URL(SQLITE_WASM_CUSTOM_PATH, globalThis.location?.href || 'http://localhost/').href

const SAMPLE_PAGE_DOCS = [
  { rowid: 1, book_id: 10, item_id: 11, raw: 'المنصور باحث في اللغة' },
  { rowid: 2, book_id: 10, item_id: 12, raw: 'المصور يكتب المقال' },
  { rowid: 3, book_id: 10, item_id: 13, raw: 'قُرْآن وإيمان ١٢٣' },
  { rowid: 4, book_id: 10, item_id: 14, raw: 'والكتاب مفيد' },
  { rowid: 5, book_id: 10, item_id: 15, raw: 'إلتي نافعة' },
  { rowid: 6, book_id: 10, item_id: 16, raw: 'قران وايمان 123' },
  { rowid: 7, book_id: 10, item_id: 17, raw: 'مشروبات نافعة' },
  { rowid: 8, book_id: 10, item_id: 18, raw: 'كاتب امريكي' },
  { rowid: 9, book_id: 10, item_id: 19, raw: 'الكاتب بارع' },
  { rowid: 10, book_id: 10, item_id: 20, raw: 'الشاعر بارع' },
  { rowid: 11, book_id: 10, item_id: 21, raw: 'طريق العلم نافع' },
  { rowid: 12, book_id: 10, item_id: 22, raw: 'طريق الادب نافع' },
  { rowid: 13, book_id: 10, item_id: 23, raw: 'مدرسة متقدمة' },
  { rowid: 14, book_id: 10, item_id: 24, raw: 'مدرسه متقدمه' }
]

const SAMPLE_TITLE_DOCS = [
  { rowid: 1, book_id: 10, item_id: 21, raw: 'باب المنصور' },
  { rowid: 2, book_id: 10, item_id: 22, raw: 'باب المصور' }
]

const FIXTURE_DOCS_JSONL_PATHS = [
  '../tests/fixtures/queries/docs.smoke.jsonl',
  '../tests/fixtures/queries/docs.complex.jsonl',
  '../tests/fixtures/queries/docs.snippets.jsonl'
]
const FIXTURE_STATIC_DOCSETS = {
  fixture_core_mini: ['d1', 'd2', 'd3', 'd4', 'd5', 'd6', 'd7'],
  fixture_complex_mini: ['c1', 'c2', 'c3', 'c4', 'c5', 'c6', 'c7', 'c8'],
  fixture_snippet_mini: ['s1', 's2', 's3', 's4', 's5', 's6', 's7', 's8'],
  fixture_diacritics_mini: ['s1', 's2', 's4', 's5', 's6', 'd7']
}

const FIXTURE_DYNAMIC_DOCSET_TARGETS = {
  fixture_diverse_20: 20,
  fixture_feature_heavy_12: 12,
  fixture_diacritics_12: 12,
  fixture_hadith_8: 8,
  fixture_fiqh_8: 8
}

const CORPUS_PRESETS = {
  sample: {
    page: SAMPLE_PAGE_DOCS.map(({ book_id, item_id, raw }) => ({ book_id, item_id, raw })),
    title: SAMPLE_TITLE_DOCS.map(({ book_id, item_id, raw }) => ({ book_id, item_id, raw }))
  }
}

const OPTION_PRESETS = {
  lucene_default: {
    allowPrefix: true,
    allowSuffix: true,
    allowWildcard: true,
    allowFuzzy: true,
    respectDiacritics: false,
    respectHamza: false,
    respectLetter: false,
    respectDigit: false,
    suffixMax: 256,
    wildcardMax: 256,
    fuzzyMax: 128,
    limit: 20,
    field: 'both'
  },
  strict_orthography: {
    allowPrefix: true,
    allowSuffix: true,
    allowWildcard: true,
    allowFuzzy: true,
    respectDiacritics: true,
    respectHamza: true,
    respectLetter: true,
    respectDigit: true,
    suffixMax: 192,
    wildcardMax: 192,
    fuzzyMax: 64,
    limit: 20,
    field: 'both'
  },
  no_wildcard_fuzzy: {
    allowPrefix: true,
    allowSuffix: true,
    allowWildcard: false,
    allowFuzzy: false,
    respectDiacritics: false,
    respectHamza: false,
    respectLetter: false,
    respectDigit: false,
    suffixMax: 128,
    wildcardMax: 128,
    fuzzyMax: 32,
    limit: 20,
    field: 'both'
  },
  recall_heavy: {
    allowPrefix: true,
    allowSuffix: true,
    allowWildcard: true,
    allowFuzzy: true,
    respectDiacritics: false,
    respectHamza: false,
    respectLetter: false,
    respectDigit: false,
    suffixMax: 512,
    wildcardMax: 768,
    fuzzyMax: 256,
    limit: 50,
    field: 'both'
  },
  precision_heavy: {
    allowPrefix: true,
    allowSuffix: false,
    allowWildcard: false,
    allowFuzzy: false,
    respectDiacritics: true,
    respectHamza: true,
    respectLetter: true,
    respectDigit: true,
    suffixMax: 64,
    wildcardMax: 64,
    fuzzyMax: 16,
    limit: 20,
    field: 'both'
  }
}

const SCENARIO_PRESETS = {
  baseline_sample: {
    label: 'baseline sample',
    corpusPreset: 'sample',
    optionPreset: 'lucene_default',
    tokenizerArgs: '',
    query: 'م*ور^2 OR قُرْآن',
    probeText: 'قُرْآن وإيمان ١٢٣',
    assertions: {
      relaxed: { minHits: 2, mustInclude: ['10:12'] },
      strict: { minHits: 1 }
    }
  },
  tafsir_quran: {
    label: 'tafsir: quran + baqara',
    corpusPreset: 'fixture_core_mini',
    optionPreset: 'lucene_default',
    tokenizerArgs: '',
    query: 'الطهارة OR الصلاة OR الزكاة',
    probeText: 'الْقُرْآنُ القرآن سورة البقرة',
    assertions: {
      relaxed: { minHits: 1 },
      strict: { minHits: 1 }
    }
  },
  hadith_title_phrase: {
    label: 'hadith: title + phrase',
    corpusPreset: 'fixture_core_mini',
    optionPreset: 'recall_heavy',
    tokenizerArgs: '',
    query: 'title:الحديث OR الإيمان',
    probeText: 'الإيمان علوم الحديث',
    assertions: {
      relaxed: { minHits: 1 },
      strict: { minHits: 1 }
    }
  },
  fiqh_core_terms: {
    label: 'fiqh: core terms',
    corpusPreset: 'fixture_core_mini',
    optionPreset: 'lucene_default',
    tokenizerArgs: '',
    query: 'الطهارة OR الزكاة OR الصلاة',
    probeText: 'الطهارة الزكاة الصلاة',
    assertions: {
      relaxed: { minHits: 1 },
      strict: { minHits: 1 }
    }
  },
  stem_exclusion: {
    label: 'stem exclusion',
    corpusPreset: 'sample',
    optionPreset: 'lucene_default',
    tokenizerArgs: 'stem_exclusion كتابها',
    query: 'كتابها',
    probeText: 'كتابها والكتاب',
    assertions: {
      relaxed: { maxHits: 0 },
      strict: { maxHits: 0 }
    }
  },
  strict_hamza_letter: {
    label: 'strict hamza + letter',
    corpusPreset: 'sample',
    optionPreset: 'strict_orthography',
    tokenizerArgs: '',
    query: 'إيمان OR مدرسة',
    probeText: 'إيمان ايمان مدرسة مدرسه',
    assertions: {
      relaxed: { minHits: 2 },
      strict: { minHits: 1 },
      cross: { strictLessOrEqualRelaxed: true, expectDifferentCounts: true }
    }
  },
  diacritics_digits: {
    label: 'diacritics + digits',
    corpusPreset: 'fixture_diacritics_mini',
    optionPreset: 'lucene_default',
    tokenizerArgs: '',
    query: 'الرَّحْمَن OR الرحمن OR ١',
    probeText: 'الرَّحْمَن الرحمن ١ 1',
    assertions: {
      relaxed: { minHits: 1 },
      strict: { minHits: 1 }
    }
  },
  fixture_diverse_broad: {
    label: 'fixture diverse: broad recall',
    corpusPreset: 'fixture_diverse_20',
    optionPreset: 'lucene_default',
    tokenizerArgs: '',
    query: 'الحديث OR العلم OR الكتاب OR الطهارة',
    probeText: 'الحديث العلم الكتاب الطهارة',
    assertions: {
      relaxed: { minHits: 3 },
      strict: { minHits: 1 }
    }
  },
  fixture_feature_strict: {
    label: 'fixture feature-heavy strict',
    corpusPreset: 'fixture_feature_heavy_12',
    optionPreset: 'strict_orthography',
    tokenizerArgs: '',
    query: 'الرَّحْمَن OR الرحمن OR ١ OR 1',
    probeText: 'الرَّحْمَن الرحمن ١ 1',
    assertions: {
      relaxed: { minHits: 1 },
      strict: { minHits: 1 },
      cross: { strictLessOrEqualRelaxed: true, expectDifferentCounts: true }
    }
  },
  fixture_hadith_focus: {
    label: 'fixture hadith focus',
    corpusPreset: 'fixture_hadith_8',
    optionPreset: 'recall_heavy',
    tokenizerArgs: '',
    query: 'حديث OR title:الحديث OR الإيمان',
    probeText: 'حديث الإسناد رواه',
    assertions: {
      relaxed: { minHits: 1 },
      strict: { minHits: 1 }
    }
  }
}

const TOKENIZER_ARG_PRESETS = {
  default: '',
  stem_exclusion_kitabha: 'stem_exclusion كتابها',
  stem_exclusion_hamza: 'stem_exclusion إيمان,ايمان',
  stem_exclusion_quran: 'stem_exclusion القرآن'
}

const BOOL_OPS = new Set(['AND', 'OR', 'NOT'])

const defaultSearchOptions = {
  allowPrefix: true,
  allowSuffix: true,
  allowWildcard: true,
  allowFuzzy: true,
  respectDiacritics: false,
  respectHamza: false,
  respectLetter: false,
  respectDigit: false,
  suffixMax: 256,
  wildcardMax: 256,
  fuzzyMax: 128,
  limit: 20,
  field: 'both'
}

const state = {
  db: null,
  searchKind: 'fts5',
  sqlite3: null,
  sqliteInit: null,
  runtimeInfo: null,
  tokenizerDirective: 'sqlite_tokenizer_ar',
  fixtureDocsById: null,
  fixtureDocRows: null,
  fixtureDynamicDocsets: {},
  fixtureDocsLoadError: '',
  lastComparison: null
}

const els = {
  query: document.getElementById('query'),
  run: document.getElementById('run'),
  status: document.getElementById('status'),
  explainOutput: document.getElementById('explainOutput'),
  compiledRelaxed: document.getElementById('compiledRelaxed'),
  compiledStrict: document.getElementById('compiledStrict'),
  hitsRelaxed: document.getElementById('hitsRelaxed'),
  hitsStrict: document.getElementById('hitsStrict'),
  relaxedBadge: document.getElementById('relaxedBadge'),
  strictBadge: document.getElementById('strictBadge'),
  crossBadge: document.getElementById('crossBadge'),
  shareState: document.getElementById('shareState'),
  field: document.getElementById('field'),
  limit: document.getElementById('limit'),
  optionPreset: document.getElementById('optionPreset'),
  applyOptionPreset: document.getElementById('applyOptionPreset'),
  scenarioPreset: document.getElementById('scenarioPreset'),
  applyScenario: document.getElementById('applyScenario'),
  tokenizerArgs: document.getElementById('tokenizerArgs'),
  tokenizerPreset: document.getElementById('tokenizerPreset'),
  applyTokenizerPreset: document.getElementById('applyTokenizerPreset'),
  suffixMax: document.getElementById('suffixMax'),
  wildcardMax: document.getElementById('wildcardMax'),
  fuzzyMax: document.getElementById('fuzzyMax'),
  allowPrefix: document.getElementById('allowPrefix'),
  allowSuffix: document.getElementById('allowSuffix'),
  allowWildcard: document.getElementById('allowWildcard'),
  allowFuzzy: document.getElementById('allowFuzzy'),
  respectDiacritics: document.getElementById('respectDiacritics'),
  respectHamza: document.getElementById('respectHamza'),
  respectLetter: document.getElementById('respectLetter'),
  respectDigit: document.getElementById('respectDigit'),
  pageCorpus: document.getElementById('pageCorpus'),
  titleCorpus: document.getElementById('titleCorpus'),
  corpusPreset: document.getElementById('corpusPreset'),
  applyCorpusPreset: document.getElementById('applyCorpusPreset'),
  rebuildCorpus: document.getElementById('rebuildCorpus'),
  resetCorpus: document.getElementById('resetCorpus'),
  probeText: document.getElementById('probeText'),
  probeRun: document.getElementById('probeRun'),
  probeOutput: document.getElementById('probeOutput'),
  chips: [...document.querySelectorAll('.chip')]
}

function setStatus(text, isError = false) {
  els.status.textContent = text
  els.status.style.color = isError ? '#b32020' : '#bd4f2f'
}

function execSql(db, sql, bind = []) {
  if (bind.length === 0) {
    db.exec(sql)
    return
  }
  db.exec({ sql, bind })
}

function queryRows(db, sql, bind = []) {
  const options = {
    sql,
    rowMode: 'object',
    returnValue: 'resultRows'
  }
  if (bind.length > 0) options.bind = bind
  return db.exec(options)
}

function escapeSqlString(value) {
  return String(value).replaceAll("'", "''")
}

function normalizeTokenizerArgs(raw) {
  return String(raw || '').replace(/\s+/g, ' ').trim()
}

function tokenizerDirectiveFromArgs(rawArgs) {
  const args = normalizeTokenizerArgs(rawArgs)
  if (args === '') return 'sqlite_tokenizer_ar'
  return `sqlite_tokenizer_ar ${args}`
}

function getTokenizerDirectiveForSql() {
  return escapeSqlString(state.tokenizerDirective || 'sqlite_tokenizer_ar')
}

function sqlBool(value) {
  return value ? 1 : 0
}

function analyzeJsonViaUdf(db, text) {
  const raw = String(text || '').trim()
  if (raw === '' || BOOL_OPS.has(raw.toUpperCase())) return []
  const row = queryRows(db, 'SELECT sqlite_tokenizer_ar_analyze_json(?) AS payload', [raw])[0]
  if (!row || row.payload == null || String(row.payload) === '') return []
  try {
    const parsed = JSON.parse(String(row.payload))
    if (!Array.isArray(parsed)) return []
    return parsed.map(item => String(item))
  } catch (_error) {
    return []
  }
}

function normalizeViaUdf(db, value, options, { lowercase = false, keepWildcards = false, trim = true } = {}) {
  const row = queryRows(
    db,
    'SELECT sqlite_tokenizer_ar_normalize(?, ?, ?, ?, ?, ?, ?) AS normalized',
    [
      String(value || ''),
      sqlBool(!options.respectDiacritics),
      sqlBool(!options.respectHamza),
      sqlBool(!options.respectLetter),
      sqlBool(!options.respectDigit),
      sqlBool(lowercase),
      sqlBool(keepWildcards)
    ]
  )[0]
  const normalized = row && row.normalized != null ? String(row.normalized) : ''
  return trim ? normalized.trim() : normalized
}

function hasSensitiveFormsViaUdf(db, text, options) {
  const row = queryRows(
    db,
    'SELECT sqlite_tokenizer_ar_has_sensitive_forms(?, ?, ?, ?, ?) AS sensitive',
    [String(text || ''), sqlBool(options.respectDiacritics), sqlBool(options.respectHamza), sqlBool(options.respectLetter), sqlBool(options.respectDigit)]
  )[0]
  return Boolean(row && Number(row.sensitive) !== 0)
}

async function getSqliteInit() {
  if (state.sqliteInit) return state.sqliteInit

  const module = await import(SQLITE_WASM_MODULE_URL)
  const init = module?.default
  if (typeof init !== 'function') {
    throw new Error(`invalid SQLite WASM module: ${SQLITE_WASM_MODULE_URL}`)
  }
  state.sqliteInit = init
  return init
}

async function initSqlite() {
  if (state.sqlite3) return state.sqlite3

  const init = await getSqliteInit()
  let wasmBinary = null
  try {
    const response = await fetch(SQLITE_WASM_CUSTOM_URL, { cache: 'no-store' })
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`)
    }
    wasmBinary = new Uint8Array(await response.arrayBuffer())
  } catch (error) {
    const reason = String(error?.message || error)
    throw new Error(
      `missing custom wasm at ${SQLITE_WASM_CUSTOM_URL} (${reason}). build it with: ./playground/scripts/build_custom_wasm.sh /path/to/sqlite-source-tree`
    )
  }

  if (!wasmBinary || wasmBinary.byteLength < 1024) {
    throw new Error(`invalid custom wasm at ${SQLITE_WASM_CUSTOM_URL}: file is empty or too small`)
  }

  state.sqlite3 = await init({
    wasmBinary,
    locateFile: file => {
      if (String(file).endsWith('sqlite3.wasm')) return SQLITE_WASM_CUSTOM_URL
      return file
    }
  })
  return state.sqlite3
}

function hasSQLiteTokenizerArTokenizer(db, tokenizerDirective = 'sqlite_tokenizer_ar') {
  const escapedDirective = escapeSqlString(tokenizerDirective)
  try {
    execSql(db, `CREATE VIRTUAL TABLE __tokenizer_probe USING fts5(content, tokenize='${escapedDirective}')`)
    execSql(db, 'DROP TABLE __tokenizer_probe')
    return true
  } catch (_error) {
    try {
      execSql(db, 'DROP TABLE IF EXISTS __tokenizer_probe')
    } catch (_dropError) {}
    return false
  }
}

function hasSQLiteTokenizerArUdfs(db) {
  const probes = [
    "SELECT sqlite_tokenizer_ar_analyze_json('الكتاب') AS v",
    "SELECT sqlite_tokenizer_ar_analyze_positions_json('المنصور في اللغة') AS v",
    "SELECT sqlite_tokenizer_ar_normalize('قُرْآن') AS v",
    "SELECT sqlite_tokenizer_ar_stem('ساهدهات') AS v",
    "SELECT sqlite_tokenizer_ar_has_sensitive_forms('قُرْآن', 1, 1, 1, 1) AS v"
  ]
  try {
    for (const sql of probes) queryRows(db, sql)
    return true
  } catch (_error) {
    return false
  }
}

function collectRuntimeInfo(db) {
  const sqliteVersion = String(queryRows(db, 'SELECT sqlite_version() AS version')[0]?.version || '')
  const sqliteSourceId = String(queryRows(db, 'SELECT sqlite_source_id() AS source_id')[0]?.source_id || '')
  const compileRows = queryRows(db, 'PRAGMA compile_options')
  const compileOptions = compileRows
    .map(row => String(row.compile_options ?? row.compile_option ?? row.value ?? Object.values(row)[0] ?? ''))
    .filter(Boolean)
  const hasFts5Option = compileOptions.includes('ENABLE_FTS5')
  return {
    sqliteVersion,
    sqliteSourceId,
    hasFts5Option
  }
}

function runtimeLabel() {
  const version = state.runtimeInfo?.sqliteVersion ? `sqlite ${state.runtimeInfo.sqliteVersion}` : 'sqlite (version unknown)'
  const sourceHash = state.runtimeInfo?.sqliteSourceId ? state.runtimeInfo.sqliteSourceId.split(' ')[0] : 'source-id unknown'
  return `${version} · ${sourceHash} · native sqlite_tokenizer_ar (+UDF analyze/positions/normalize/stem)`
}

function normalizeFixtureText(value) {
  return String(value || '')
    .replace(/<[^>]+>/g, ' ')
    .replace(/^[\s¬•舄]+/g, '')
    .replace(/[_\u00ad]+/g, ' ')
    .replace(/\s+/g, ' ')
    .trim()
}

function snippetFromFixturePage(pageText, maxChars = 340) {
  const clean = normalizeFixtureText(pageText)
  if (clean.length <= maxChars) return clean
  return clean.slice(0, maxChars).trimEnd() + '…'
}

function titleFromFixturePage(pageText, fallbackText) {
  const spanMatch = String(pageText || '').match(/<span[^>]*data-type=(?:"|')title(?:"|')[^>]*>(.*?)<\/span>/i)
  const spanTitle = normalizeFixtureText(spanMatch?.[1] || '')
  if (spanTitle !== '') return spanTitle

  const words = normalizeFixtureText(fallbackText).split(' ').filter(Boolean)
  return words.slice(0, 6).join(' ')
}

function parseFixtureDocId(docId, fallbackIndex) {
  const match = String(docId).match(/^(\d+)-(\d+)$/)
  if (match) return { bookId: Number(match[1]), itemId: Number(match[2]) }
  return { bookId: 9000, itemId: fallbackIndex + 1 }
}

async function ensureFixtureDocsLoaded() {
  if (state.fixtureDocsById) return state.fixtureDocsById
  if (state.fixtureDocsLoadError) throw new Error(state.fixtureDocsLoadError)

  const map = new Map()
  const rows = []
  const errors = []

  for (const path of FIXTURE_DOCS_JSONL_PATHS) {
    try {
      const response = await fetch(path, { cache: 'no-store' })
      if (!response.ok) {
        errors.push(`${path} (${response.status})`)
        continue
      }
      const content = await response.text()
      for (const rawLine of content.split('\n')) {
        const line = rawLine.trim()
        if (line === '') continue
        try {
          const row = JSON.parse(line)
          const docId = String(row.doc_id || '').trim()
          const page = String(row.page || '')
          if (docId === '' || page === '' || map.has(docId)) continue
          const enriched = { ...row, doc_id: docId, _source: path }
          map.set(docId, enriched)
          rows.push(enriched)
        } catch (_error) {}
      }
    } catch (error) {
      errors.push(`${path} (${String(error?.message || error)})`)
    }
  }

  if (rows.length === 0) {
    state.fixtureDocsLoadError = `failed to load fixture snippets: ${errors.join('; ')}`
    throw new Error(state.fixtureDocsLoadError)
  }

  state.fixtureDocRows = rows
  state.fixtureDynamicDocsets = buildDynamicFixtureDocsets(rows)
  state.fixtureDocsById = map
  return map
}

function selectDynamicDocsetRows(rows, target, predicate = null, { fillToTarget = false } = {}) {
  const picked = []
  const seen = new Set()
  for (const row of rows) {
    if (picked.length >= target) break
    const docId = String(row.doc_id || '')
    if (docId === '' || seen.has(docId)) continue
    if (predicate && !predicate(row)) continue
    picked.push(docId)
    seen.add(docId)
  }
  if (fillToTarget && picked.length < target) {
    for (const row of rows) {
      if (picked.length >= target) break
      const docId = String(row.doc_id || '')
      if (docId === '' || seen.has(docId)) continue
      picked.push(docId)
      seen.add(docId)
    }
  }
  return picked
}

function buildDiverseDocset(rows, target) {
  const picked = []
  const seenDoc = new Set()
  const seenBook = new Set()
  for (const row of rows) {
    if (picked.length >= target) break
    const docId = String(row.doc_id || '')
    if (docId === '') continue
    const bookId = docId.split('-', 1)[0]
    if (seenDoc.has(docId) || seenBook.has(bookId)) continue
    seenDoc.add(docId)
    seenBook.add(bookId)
    picked.push(docId)
  }
  if (picked.length >= target) return picked
  for (const row of rows) {
    if (picked.length >= target) break
    const docId = String(row.doc_id || '')
    if (docId === '' || seenDoc.has(docId)) continue
    seenDoc.add(docId)
    picked.push(docId)
  }
  return picked
}

function buildDynamicFixtureDocsets(rows) {
  const hasDiacritics = text => /[\u064B-\u065F\u0670\u06D6-\u06ED]/.test(text)
  const hasEasternDigits = text => /[\u0660-\u0669\u06F0-\u06F9]/.test(text)
  const hasHamza = text => /[أإآ]/.test(text)
  const hadithKeywords = ['حديث', 'الحديث', 'إسناد', 'الإسناد', 'رواه', 'الراوي', 'صحيح', 'سنن']
  const fiqhKeywords = ['الفقه', 'النكاح', 'البيع', 'الطهارة', 'الزكاة', 'الصلاة', 'الوقف', 'العدة', 'الطلاق']
  return {
    fixture_diverse_20: buildDiverseDocset(rows, FIXTURE_DYNAMIC_DOCSET_TARGETS.fixture_diverse_20),
    fixture_feature_heavy_12: selectDynamicDocsetRows(
      rows,
      FIXTURE_DYNAMIC_DOCSET_TARGETS.fixture_feature_heavy_12,
      row => {
        const text = String(row.page || '')
        return hasDiacritics(text) && hasEasternDigits(text) && hasHamza(text)
      },
      { fillToTarget: true }
    ),
    fixture_diacritics_12: selectDynamicDocsetRows(
      rows,
      FIXTURE_DYNAMIC_DOCSET_TARGETS.fixture_diacritics_12,
      row => {
        const text = String(row.page || '')
        return hasDiacritics(text) || hasEasternDigits(text)
      },
      { fillToTarget: true }
    ),
    fixture_hadith_8: selectDynamicDocsetRows(
      rows,
      FIXTURE_DYNAMIC_DOCSET_TARGETS.fixture_hadith_8,
      row => hadithKeywords.some(keyword => String(row.page || '').includes(keyword)),
      { fillToTarget: true }
    ),
    fixture_fiqh_8: selectDynamicDocsetRows(
      rows,
      FIXTURE_DYNAMIC_DOCSET_TARGETS.fixture_fiqh_8,
      row => fiqhKeywords.some(keyword => String(row.page || '').includes(keyword)),
      { fillToTarget: true }
    )
  }
}

function allFixtureDocsets() {
  return { ...FIXTURE_STATIC_DOCSETS, ...(state.fixtureDynamicDocsets || {}) }
}

function buildFixtureRealPreset(name) {
  const ids = allFixtureDocsets()[name]
  if (!ids) throw new Error(`unknown fixture corpus preset: ${name}`)
  if (!state.fixtureDocsById) throw new Error('fixture snippets are not loaded yet')

  const pageDocs = []
  const titleDocs = []
  ids.forEach((docId, index) => {
    const row = state.fixtureDocsById.get(docId)
    if (!row) return
    const snippet = snippetFromFixturePage(row.page)
    if (snippet === '') return
    const { bookId, itemId } = parseFixtureDocId(docId, index)
    pageDocs.push({ book_id: bookId, item_id: itemId, raw: snippet })

    const title = titleFromFixturePage(row.page, snippet)
    if (title !== '') titleDocs.push({ book_id: bookId, item_id: itemId, raw: title })
  })

  if (pageDocs.length === 0) throw new Error(`no fixture snippets found for preset: ${name}`)
  return { page: pageDocs, title: titleDocs }
}

function isFixtureCorpusPreset(name) {
  const key = String(name || '')
  return (
    Object.prototype.hasOwnProperty.call(FIXTURE_STATIC_DOCSETS, key) ||
    Object.prototype.hasOwnProperty.call(FIXTURE_DYNAMIC_DOCSET_TARGETS, key) ||
    Object.prototype.hasOwnProperty.call(allFixtureDocsets(), key)
  )
}

function docsToCorpusText(docs) {
  return docs.map(doc => `${doc.book_id}|${doc.item_id}|${doc.raw}`).join('\n')
}

function setOptionControls(preset) {
  els.allowPrefix.checked = Boolean(preset.allowPrefix)
  els.allowSuffix.checked = Boolean(preset.allowSuffix)
  els.allowWildcard.checked = Boolean(preset.allowWildcard)
  els.allowFuzzy.checked = Boolean(preset.allowFuzzy)
  els.respectDiacritics.checked = Boolean(preset.respectDiacritics)
  els.respectHamza.checked = Boolean(preset.respectHamza)
  els.respectLetter.checked = Boolean(preset.respectLetter)
  els.respectDigit.checked = Boolean(preset.respectDigit)
  els.suffixMax.value = String(preset.suffixMax)
  els.wildcardMax.value = String(preset.wildcardMax)
  els.fuzzyMax.value = String(preset.fuzzyMax)
  els.limit.value = String(preset.limit)
  els.field.value = preset.field
}

function applyOptionPreset(name) {
  const preset = OPTION_PRESETS[name]
  if (!preset) throw new Error(`unknown option preset: ${name}`)
  setOptionControls(preset)
}

function setTokenizerPresetFromArgs(rawArgs) {
  const args = normalizeTokenizerArgs(rawArgs)
  const found = Object.entries(TOKENIZER_ARG_PRESETS).find(([, value]) => normalizeTokenizerArgs(value) === args)
  els.tokenizerPreset.value = found ? found[0] : 'custom'
}

function applyTokenizerPreset(name) {
  if (name === 'custom') return
  if (!Object.prototype.hasOwnProperty.call(TOKENIZER_ARG_PRESETS, name)) throw new Error(`unknown tokenizer preset: ${name}`)
  els.tokenizerArgs.value = TOKENIZER_ARG_PRESETS[name]
  setTokenizerPresetFromArgs(els.tokenizerArgs.value)
}

function getCurrentUrlStateParams() {
  const params = new URLSearchParams()
  params.set('q', els.query.value || '')
  params.set('field', els.field.value || 'both')
  params.set('limit', String(els.limit.value || '20'))
  params.set('suffixMax', String(els.suffixMax.value || defaultSearchOptions.suffixMax))
  params.set('wildcardMax', String(els.wildcardMax.value || defaultSearchOptions.wildcardMax))
  params.set('fuzzyMax', String(els.fuzzyMax.value || defaultSearchOptions.fuzzyMax))
  params.set('optionPreset', els.optionPreset.value || 'lucene_default')
  params.set('scenario', els.scenarioPreset.value || 'fixture_diverse_broad')
  params.set('corpus', els.corpusPreset.value || 'fixture_diverse_20')
  params.set('tokenizerArgs', normalizeTokenizerArgs(els.tokenizerArgs.value || ''))
  params.set('tokenizerPreset', els.tokenizerPreset.value || 'default')
  params.set('probe', els.probeText.value || '')
  params.set('allowPrefix', els.allowPrefix.checked ? '1' : '0')
  params.set('allowSuffix', els.allowSuffix.checked ? '1' : '0')
  params.set('allowWildcard', els.allowWildcard.checked ? '1' : '0')
  params.set('allowFuzzy', els.allowFuzzy.checked ? '1' : '0')
  params.set('respectDiacritics', els.respectDiacritics.checked ? '1' : '0')
  params.set('respectHamza', els.respectHamza.checked ? '1' : '0')
  params.set('respectLetter', els.respectLetter.checked ? '1' : '0')
  params.set('respectDigit', els.respectDigit.checked ? '1' : '0')
  return params
}

function updateUrlState() {
  const params = getCurrentUrlStateParams()
  const url = new URL(globalThis.location.href)
  url.search = params.toString()
  globalThis.history.replaceState({}, '', url.toString())
  return url.toString()
}

async function applyUrlStateIfPresent() {
  const url = new URL(globalThis.location.href)
  const params = url.searchParams
  if ([...params.keys()].length === 0) return

  const readBool = (key, fallback) => {
    if (!params.has(key)) return fallback
    return params.get(key) === '1'
  }

  if (params.has('optionPreset') && OPTION_PRESETS[params.get('optionPreset')]) {
    els.optionPreset.value = params.get('optionPreset')
    applyOptionPreset(els.optionPreset.value)
  }

  if (params.has('scenario') && SCENARIO_PRESETS[params.get('scenario')]) els.scenarioPreset.value = params.get('scenario')
  if (params.has('corpus')) {
    const corpus = params.get('corpus')
    if (corpus && (CORPUS_PRESETS[corpus] || isFixtureCorpusPreset(corpus))) {
      if (isFixtureCorpusPreset(corpus)) await ensureFixtureDocsLoaded()
      els.corpusPreset.value = corpus
      loadCorpusPreset(corpus)
    }
  }

  if (params.has('q')) els.query.value = params.get('q')
  if (params.has('probe')) els.probeText.value = params.get('probe')
  if (params.has('field')) els.field.value = params.get('field')
  if (params.has('limit')) els.limit.value = params.get('limit')
  if (params.has('suffixMax')) els.suffixMax.value = params.get('suffixMax')
  if (params.has('wildcardMax')) els.wildcardMax.value = params.get('wildcardMax')
  if (params.has('fuzzyMax')) els.fuzzyMax.value = params.get('fuzzyMax')
  if (params.has('tokenizerArgs')) els.tokenizerArgs.value = params.get('tokenizerArgs')
  if (params.has('tokenizerPreset')) {
    const preset = params.get('tokenizerPreset')
    if (preset && (preset === 'custom' || Object.prototype.hasOwnProperty.call(TOKENIZER_ARG_PRESETS, preset))) els.tokenizerPreset.value = preset
  }

  els.allowPrefix.checked = readBool('allowPrefix', els.allowPrefix.checked)
  els.allowSuffix.checked = readBool('allowSuffix', els.allowSuffix.checked)
  els.allowWildcard.checked = readBool('allowWildcard', els.allowWildcard.checked)
  els.allowFuzzy.checked = readBool('allowFuzzy', els.allowFuzzy.checked)
  els.respectDiacritics.checked = readBool('respectDiacritics', els.respectDiacritics.checked)
  els.respectHamza.checked = readBool('respectHamza', els.respectHamza.checked)
  els.respectLetter.checked = readBool('respectLetter', els.respectLetter.checked)
  els.respectDigit.checked = readBool('respectDigit', els.respectDigit.checked)

  setTokenizerPresetFromArgs(els.tokenizerArgs.value)
}

async function applyScenarioPreset(name) {
  const preset = SCENARIO_PRESETS[name]
  if (!preset) throw new Error(`unknown scenario preset: ${name}`)
  if (preset.corpusPreset) {
    if (isFixtureCorpusPreset(preset.corpusPreset)) await ensureFixtureDocsLoaded()
    els.corpusPreset.value = preset.corpusPreset
    loadCorpusPreset(preset.corpusPreset)
  }
  if (preset.optionPreset) {
    els.optionPreset.value = preset.optionPreset
    applyOptionPreset(preset.optionPreset)
  }
  if (typeof preset.tokenizerArgs === 'string') els.tokenizerArgs.value = preset.tokenizerArgs
  setTokenizerPresetFromArgs(els.tokenizerArgs.value)
  if (typeof preset.query === 'string') els.query.value = preset.query
  if (typeof preset.probeText === 'string') els.probeText.value = preset.probeText
}

function loadCorpusPreset(name) {
  const preset = isFixtureCorpusPreset(name) ? buildFixtureRealPreset(name) : CORPUS_PRESETS[name]
  if (!preset) throw new Error(`unknown corpus preset: ${name}`)
  els.pageCorpus.value = docsToCorpusText(preset.page)
  els.titleCorpus.value = docsToCorpusText(preset.title)
}

function seedCorpusEditors() {
  const defaultCorpus = els.corpusPreset?.value || 'fixture_diverse_20'
  if (els.pageCorpus.value.trim() === '' && els.titleCorpus.value.trim() === '') {
    loadCorpusPreset(defaultCorpus)
  } else {
    if (els.pageCorpus.value.trim() === '') els.pageCorpus.value = docsToCorpusText(CORPUS_PRESETS.sample.page)
    if (els.titleCorpus.value.trim() === '') els.titleCorpus.value = docsToCorpusText(CORPUS_PRESETS.sample.title)
  }
  if (!els.scenarioPreset.value) els.scenarioPreset.value = 'fixture_diverse_broad'
  applyOptionPreset(els.optionPreset?.value || 'lucene_default')
  setTokenizerPresetFromArgs(els.tokenizerArgs?.value || '')
}

function parseCorpusText(text, label) {
  const docs = []
  const lines = text.split('\n')
  for (const rawLine of lines) {
    const line = rawLine.trim()
    if (line === '' || line.startsWith('#')) continue
    const firstSep = line.indexOf('|')
    const secondSep = firstSep < 0 ? -1 : line.indexOf('|', firstSep + 1)
    if (firstSep < 1 || secondSep < 0) throw new Error(`${label}: expected "book_id|item_id|text"`)
    const bookId = Number(line.slice(0, firstSep))
    const itemId = Number(line.slice(firstSep + 1, secondSep))
    const body = line.slice(secondSep + 1).trim()
    if (!Number.isInteger(bookId) || bookId <= 0) throw new Error(`${label}: invalid book_id in "${line}"`)
    if (!Number.isInteger(itemId) || itemId <= 0) throw new Error(`${label}: invalid item_id in "${line}"`)
    if (body === '') throw new Error(`${label}: empty text in "${line}"`)
    docs.push({ rowid: docs.length + 1, book_id: bookId, item_id: itemId, raw: body })
  }
  if (docs.length === 0) throw new Error(`${label}: no rows`)
  return docs
}

function analyzeTextRuntime(db, text) {
  return analyzeJsonViaUdf(db, text)
}

function quoteMatchTerm(value) {
  return '"' + value.replaceAll('"', '""') + '"'
}

function parseScopedToken(token) {
  const idx = token.indexOf(':')
  if (idx <= 0) return { scope: null, raw: token }
  const scope = token.slice(0, idx).toLowerCase()
  const raw = token.slice(idx + 1)
  if ((scope === 'page' || scope === 'title') && raw !== '') return { scope, raw }
  return { scope: null, raw: token }
}

function stripBoost(token) {
  const match = token.match(/^(.+?)\^([0-9]+(?:\.[0-9]+)?)$/)
  if (!match) return { base: token, boost: null }
  return { base: match[1], boost: Number(match[2]) }
}

function parseFuzzyToken(token) {
  const match = token.match(/^(.+?)~([0-9]*)$/)
  if (!match) return null
  const base = match[1]
  const edits = match[2] === '' ? 2 : Math.max(0, Math.min(2, Number(match[2])))
  return { base, edits }
}

function parseFieldGroupSegment(query, start) {
  const fieldSep = query.indexOf(':', start)
  if (fieldSep <= start) return null
  const scope = query.slice(start, fieldSep).toLowerCase()
  if (scope !== 'page' && scope !== 'title') return null
  if (fieldSep + 1 >= query.length || query[fieldSep + 1] !== '(') return null

  let index = fieldSep + 2
  let depth = 1
  let inQuote = false

  while (index < query.length) {
    const ch = query[index]
    if (ch === '"') {
      inQuote = !inQuote
      index += 1
      continue
    }
    if (!inQuote) {
      if (ch === '(') depth += 1
      if (ch === ')') {
        depth -= 1
        if (depth === 0) break
      }
    }
    index += 1
  }

  if (depth !== 0) throw new Error('unclosed field-group parentheses in query')
  const inner = query.slice(fieldSep + 2, index)
  index += 1

  let boost = null
  if (index < query.length && query[index] === '^') {
    index += 1
    const startBoost = index
    while (index < query.length && /[0-9.]/.test(query[index])) index += 1
    if (index > startBoost) boost = Number(query.slice(startBoost, index))
  }

  return { scope, inner, boost, next: index }
}

function parseQuotedSegment(query, start, label = 'quote') {
  if (start >= query.length || query[start] !== '"') return null

  let index = start + 1
  let phrase = ''
  while (index < query.length && query[index] !== '"') {
    phrase += query[index]
    index += 1
  }
  if (index >= query.length) throw new Error(`unclosed ${label} in query`)
  index += 1

  let slop = null
  if (index < query.length && query[index] === '~') {
    index += 1
    const slopStart = index
    while (index < query.length && /[0-9]/.test(query[index])) index += 1
    if (index > slopStart) slop = Number(query.slice(slopStart, index))
  }

  let boost = null
  if (index < query.length && query[index] === '^') {
    index += 1
    const boostStart = index
    while (index < query.length && /[0-9.]/.test(query[index])) index += 1
    if (index > boostStart) boost = Number(query.slice(boostStart, index))
  }

  return { phrase, slop, boost, next: index }
}

function parseFieldPhraseSegment(query, start) {
  const fieldSep = query.indexOf(':', start)
  if (fieldSep <= start) return null
  const scope = query.slice(start, fieldSep).toLowerCase()
  if (scope !== 'page' && scope !== 'title') return null
  if (fieldSep + 1 >= query.length || query[fieldSep + 1] !== '"') return null

  const parsed = parseQuotedSegment(query, fieldSep + 1, 'fielded quote')
  return { scope, phrase: parsed.phrase, slop: parsed.slop, boost: parsed.boost, next: parsed.next }
}

function pushPhraseToken(tokens, parsed) {
  if (parsed.slop === null) {
    tokens.push({
      kind: parsed.boost === null ? 'phrase' : 'phrase_boost',
      value: parsed.boost === null ? parsed.phrase : `${parsed.boost}\t${parsed.phrase}`
    })
    return
  }
  tokens.push({
    kind: parsed.boost === null ? 'phrase_slop' : 'phrase_slop_boost',
    value: parsed.boost === null ? `${parsed.slop}\t${parsed.phrase}` : `${parsed.boost}\t${parsed.slop}\t${parsed.phrase}`
  })
}

function pushFieldPhraseToken(tokens, parsed) {
  if (parsed.slop === null) {
    tokens.push({
      kind: parsed.boost === null ? 'field_phrase' : 'field_phrase_boost',
      value: parsed.boost === null ? `${parsed.scope}:${parsed.phrase}` : `${parsed.scope}\t${parsed.boost}\t${parsed.phrase}`
    })
    return
  }
  tokens.push({
    kind: parsed.boost === null ? 'field_phrase_slop' : 'field_phrase_slop_boost',
    value: parsed.boost === null ? `${parsed.scope}:${parsed.slop}\t${parsed.phrase}` : `${parsed.scope}\t${parsed.boost}\t${parsed.slop}\t${parsed.phrase}`
  })
}

function splitQuery(query) {
  const tokens = []
  let i = 0
  while (i < query.length) {
    const ch = query[i]
    if (/\s/.test(ch)) {
      i += 1
      continue
    }

    if (ch === '(' || ch === ')') {
      tokens.push({ kind: 'paren', value: ch })
      i += 1
      if (ch === ')' && i < query.length && query[i] === '^') {
        i += 1
        while (i < query.length && /[0-9.]/.test(query[i])) i += 1
      }
      continue
    }

    if ((ch === '+' || ch === '-') && i + 1 < query.length) {
      const fieldGroup = parseFieldGroupSegment(query, i + 1)
      if (fieldGroup) {
        tokens.push({ kind: 'token', value: ch })
        tokens.push({
          kind: fieldGroup.boost === null ? 'field_group' : 'field_group_boost',
          value: fieldGroup.boost === null ? `${fieldGroup.scope}\t${fieldGroup.inner}` : `${fieldGroup.scope}\t${fieldGroup.boost}\t${fieldGroup.inner}`
        })
        i = fieldGroup.next
        continue
      }

      const fieldPhrase = parseFieldPhraseSegment(query, i + 1)
      if (fieldPhrase) {
        tokens.push({ kind: 'token', value: ch })
        pushFieldPhraseToken(tokens, fieldPhrase)
        i = fieldPhrase.next
        continue
      }

      if (query[i + 1] === '"') {
        const phrase = parseQuotedSegment(query, i + 1)
        tokens.push({ kind: 'token', value: ch })
        pushPhraseToken(tokens, phrase)
        i = phrase.next
        continue
      }
    }

    if (ch === '"') {
      const phrase = parseQuotedSegment(query, i)
      pushPhraseToken(tokens, phrase)
      i = phrase.next
      continue
    }

    const fieldGroup = parseFieldGroupSegment(query, i)
    if (fieldGroup) {
      tokens.push({
        kind: fieldGroup.boost === null ? 'field_group' : 'field_group_boost',
        value: fieldGroup.boost === null ? `${fieldGroup.scope}\t${fieldGroup.inner}` : `${fieldGroup.scope}\t${fieldGroup.boost}\t${fieldGroup.inner}`
      })
      i = fieldGroup.next
      continue
    }

    const fieldPhrase = parseFieldPhraseSegment(query, i)
    if (fieldPhrase) {
      pushFieldPhraseToken(tokens, fieldPhrase)
      i = fieldPhrase.next
      continue
    }

    let j = i
    while (j < query.length && !/\s/.test(query[j]) && query[j] !== '(' && query[j] !== ')') j += 1
    tokens.push({ kind: 'token', value: query.slice(i, j) })
    i = j
  }

  return tokens
}

function levenshteinDistance(a, b, maxEdits) {
  if (a === b) return 0
  if (Math.abs(a.length - b.length) > maxEdits) return maxEdits + 1

  let previous = [...Array(b.length + 1).keys()]
  for (let i = 1; i <= a.length; i += 1) {
    const current = [i]
    let minInRow = current[0]
    for (let j = 1; j <= b.length; j += 1) {
      const cost = a[i - 1] === b[j - 1] ? 0 : 1
      current[j] = Math.min(previous[j] + 1, current[j - 1] + 1, previous[j - 1] + cost)
      if (current[j] < minInRow) minInRow = current[j]
    }
    if (minInRow > maxEdits) return maxEdits + 1
    previous = current
  }

  return previous[previous.length - 1]
}

function selectTermsByGlob(db, vocabTable, pattern, limit) {
  if (state.searchKind === 'fts5') {
    return queryRows(db, `SELECT term FROM ${vocabTable} WHERE term <> '__empty__' AND term GLOB ? ORDER BY term LIMIT ?`, [pattern, limit]).map(r => String(r.term))
  }
  return queryRows(db, `SELECT term FROM ${vocabTable} WHERE col = '*' AND term GLOB ? ORDER BY term LIMIT ?`, [pattern, limit]).map(r => String(r.term))
}

function selectTermsByLength(db, vocabTable, minLen, maxLen, limit) {
  if (state.searchKind === 'fts5') {
    return queryRows(db, `SELECT term FROM ${vocabTable} WHERE term <> '__empty__' AND length(term) BETWEEN ? AND ? ORDER BY term LIMIT ?`, [minLen, maxLen, limit]).map(r => String(r.term))
  }
  return queryRows(db, `SELECT term FROM ${vocabTable} WHERE col = '*' AND length(term) BETWEEN ? AND ? ORDER BY term LIMIT ?`, [minLen, maxLen, limit]).map(r => String(r.term))
}

function wildcardHasLiteral(pattern) {
  for (const ch of pattern) if (ch !== '*' && ch !== '?') return true
  return false
}

function normalizeWildcardPattern(db, pattern, options) {
  return normalizeViaUdf(db, pattern, options, { lowercase: false, keepWildcards: true, trim: true })
}

function analyzeSingleToken(db, raw, options) {
  return analyzeTextRuntime(db, raw)
}

function pushExplainEvent(options, event) {
  if (!options || !Array.isArray(options._explainEvents)) return
  options._explainEvents.push(event)
}

function compileTokenExpression(db, field, token, options) {
  const vocab = field === 'page' ? 'page_vocab' : 'title_vocab'

  const upper = token.toUpperCase()
  if (BOOL_OPS.has(upper)) return upper
  if (token === '(' || token === ')') return token

  const parsed = parseScopedToken(token)
  const stripped = stripBoost(parsed.raw)
  const rawToken = stripped.base
  if (parsed.scope && parsed.scope !== field) return quoteMatchTerm('__nohit__')

  if (rawToken.includes('[') || rawToken.includes(']')) throw new Error(`contains unsupported wildcard pattern: ${rawToken}`)

  if (rawToken.includes('*') || rawToken.includes('?')) {
    if (!options.allowWildcard) throw new Error('wildcard search is disabled by policy')
    if (rawToken.startsWith('*') && !options.allowSuffix) throw new Error('suffix search is disabled by policy')
    if (rawToken.endsWith('*') && !options.allowPrefix) throw new Error('prefix search is disabled by policy')

    const wildcardPattern = normalizeWildcardPattern(db, rawToken, options)
    if (wildcardPattern === '') throw new Error('empty wildcard term')
    if (!wildcardHasLiteral(wildcardPattern)) throw new Error('wildcard term must include at least one literal character')

    const simplePrefix = wildcardPattern.endsWith('*') && wildcardPattern.indexOf('*') === wildcardPattern.length - 1 && !wildcardPattern.startsWith('*') && !wildcardPattern.includes('?')
    if (simplePrefix) {
      const base = wildcardPattern.slice(0, -1).trim()
      if (base === '') throw new Error('empty prefix term')
      pushExplainEvent(options, { field, kind: 'prefix', token: rawToken, normalized: base, expansions: [], expression: quoteMatchTerm(base) + '*' })
      return quoteMatchTerm(base) + '*'
    }

    const simpleSuffix = wildcardPattern.startsWith('*') && wildcardPattern.indexOf('*') === 0 && !wildcardPattern.endsWith('*') && !wildcardPattern.includes('?')
    if (simpleSuffix) {
      const expansions = selectTermsByGlob(db, vocab, wildcardPattern, options.suffixMax)
      if (expansions.length === 0) {
        pushExplainEvent(options, { field, kind: 'suffix', token: rawToken, normalized: wildcardPattern, expansions: [], expression: quoteMatchTerm('__nohit__') })
        return quoteMatchTerm('__nohit__')
      }
      const expression = expansions.length === 1 ? quoteMatchTerm(expansions[0]) : '(' + expansions.map(quoteMatchTerm).join(' OR ') + ')'
      pushExplainEvent(options, { field, kind: 'suffix', token: rawToken, normalized: wildcardPattern, expansions, expression })
      return expression
    }

    const expansions = selectTermsByGlob(db, vocab, wildcardPattern, options.wildcardMax)
    if (expansions.length === 0) {
      pushExplainEvent(options, { field, kind: 'wildcard', token: rawToken, normalized: wildcardPattern, expansions: [], expression: quoteMatchTerm('__nohit__') })
      return quoteMatchTerm('__nohit__')
    }
    const expression = expansions.length === 1 ? quoteMatchTerm(expansions[0]) : '(' + expansions.map(quoteMatchTerm).join(' OR ') + ')'
    pushExplainEvent(options, { field, kind: 'wildcard', token: rawToken, normalized: wildcardPattern, expansions, expression })
    return expression
  }

  const fuzzy = parseFuzzyToken(rawToken)
  if (fuzzy) {
    if (!options.allowFuzzy) throw new Error('fuzzy search is disabled by policy')
    const normalized = normalizeViaUdf(db, fuzzy.base, options, { lowercase: false, keepWildcards: false, trim: true })
    const candidates = selectTermsByLength(db, vocab, Math.max(1, normalized.length - fuzzy.edits), normalized.length + fuzzy.edits, Math.max(512, options.fuzzyMax * 8))
    const expansions = []
    for (const term of candidates) {
      if (levenshteinDistance(normalized, term, fuzzy.edits) <= fuzzy.edits) {
        expansions.push(term)
        if (expansions.length >= options.fuzzyMax) break
      }
    }
    if (expansions.length === 0) {
      pushExplainEvent(options, { field, kind: 'fuzzy', token: rawToken, normalized, expansions: [], expression: quoteMatchTerm('__nohit__') })
      return quoteMatchTerm('__nohit__')
    }
    const expression = expansions.length === 1 ? quoteMatchTerm(expansions[0]) : '(' + expansions.map(quoteMatchTerm).join(' OR ') + ')'
    pushExplainEvent(options, { field, kind: 'fuzzy', token: rawToken, normalized, expansions, expression })
    return expression
  }

  const analyzed = analyzeSingleToken(db, rawToken, options)
  if (analyzed.length === 0) return null
  if (analyzed.length === 1) return quoteMatchTerm(analyzed[0])
  return '(' + analyzed.map(quoteMatchTerm).join(' OR ') + ')'
}

function buildFromClauses(clauses) {
  const mustTerms = clauses.filter(c => c.occur === 'MUST').map(c => `(${c.expr})`)
  const shouldTerms = clauses.filter(c => c.occur === 'SHOULD').map(c => `(${c.expr})`)
  const mustNotTerms = clauses.filter(c => c.occur === 'MUST_NOT').map(c => `(${c.expr})`)

  let positive = ''
  if (mustTerms.length > 0) positive = mustTerms.join(' AND ')
  else if (shouldTerms.length > 0) positive = shouldTerms.join(' OR ')

  if (positive === '') return quoteMatchTerm('__nohit__')
  if (mustNotTerms.length === 0) return positive
  return `(${positive}) NOT (${mustNotTerms.join(' OR ')})`
}

function compilePhraseExpression(db, value, options) {
  const analyzed = analyzeTextRuntime(db, value)
  if (analyzed.length === 0) return null
  if (analyzed.length === 1) return quoteMatchTerm(analyzed[0])
  return quoteMatchTerm(analyzed.join(' '))
}

function compilePhraseSlopExpression(db, value, options) {
  const [slopRaw, phrase] = value.split('\t', 2)
  const slop = Math.max(0, Number(slopRaw))
  const analyzed = analyzeTextRuntime(db, phrase)
  if (analyzed.length === 0) return null
  if (analyzed.length <= 1) return quoteMatchTerm(analyzed[0] ?? '')
  return `NEAR(${analyzed.map(quoteMatchTerm).join(' ')}, ${slop})`
}

function applyAddClause(clauses, conjunction, modifier, expr) {
  if (clauses.length > 0 && conjunction === 'AND') {
    const prev = clauses[clauses.length - 1]
    if (prev.occur !== 'MUST_NOT') prev.occur = 'MUST'
  }

  if (!expr) return

  let prohibited = modifier === 'NOT'
  let required = modifier === 'REQ'
  if (conjunction === 'AND' && !prohibited) required = true

  if (required && !prohibited) {
    clauses.push({ expr, occur: 'MUST' })
    return
  }
  if (!required && !prohibited) {
    clauses.push({ expr, occur: 'SHOULD' })
    return
  }
  if (!required && prohibited) {
    clauses.push({ expr, occur: 'MUST_NOT' })
    return
  }
  throw new Error('clause cannot be both required and prohibited')
}

function compileMatchExpression(db, field, query, options) {
  const tokens = splitQuery(query)

  function compileOperand(kind, value) {
    if (kind === 'phrase') return compilePhraseExpression(db, value, options)
    if (kind === 'phrase_boost') return compilePhraseExpression(db, value.split('\t', 2)[1], options)
    if (kind === 'phrase_slop') return compilePhraseSlopExpression(db, value, options)
    if (kind === 'phrase_slop_boost') return compilePhraseSlopExpression(db, value.split('\t', 3).slice(1).join('\t'), options)
    if (kind === 'field_phrase') {
      const [scope, phrase] = value.split(':', 2)
      if (scope !== field) return quoteMatchTerm('__nohit__')
      return compilePhraseExpression(db, phrase, options)
    }
    if (kind === 'field_phrase_boost') {
      const [scope, _boost, phrase] = value.split('\t', 3)
      if (scope !== field) return quoteMatchTerm('__nohit__')
      return compilePhraseExpression(db, phrase, options)
    }
    if (kind === 'field_phrase_slop') {
      const [scope, payload] = value.split(':', 2)
      if (scope !== field) return quoteMatchTerm('__nohit__')
      return compilePhraseSlopExpression(db, payload, options)
    }
    if (kind === 'field_phrase_slop_boost') {
      const [scope, _boost, slopRaw, phrase] = value.split('\t', 4)
      if (scope !== field) return quoteMatchTerm('__nohit__')
      return compilePhraseSlopExpression(db, `${slopRaw}\t${phrase}`, options)
    }
    if (kind === 'field_group') {
      const [scope, inner] = value.split('\t', 2)
      if (scope !== field) return quoteMatchTerm('__nohit__')
      return compileMatchExpression(db, field, inner, options)
    }
    if (kind === 'field_group_boost') {
      const [scope, _boost, inner] = value.split('\t', 3)
      if (scope !== field) return quoteMatchTerm('__nohit__')
      return compileMatchExpression(db, field, inner, options)
    }
    return compileTokenExpression(db, field, value, options)
  }

  function parseClauseSequence(start, stopOnRParen) {
    const clauses = []
    let index = start
    let pendingConj = 'NONE'
    let sawOperand = false

    while (index < tokens.length) {
      const current = tokens[index]
      if (current.kind === 'paren' && current.value === ')' && stopOnRParen) break

      if (current.kind === 'token') {
        const upper = current.value.toUpperCase()
        if (upper === 'AND') {
          if (!sawOperand) throw new Error('dangling boolean operator')
          if (pendingConj !== 'NONE') throw new Error('consecutive boolean operators')
          pendingConj = 'AND'
          index += 1
          continue
        }
        if (upper === 'OR') {
          if (!sawOperand) throw new Error('dangling boolean operator')
          if (pendingConj !== 'NONE') throw new Error('consecutive boolean operators')
          pendingConj = 'OR'
          index += 1
          continue
        }
      }

      let modifier = 'NONE'
      let operandKind = current.kind
      let operandValue = current.value

      if (operandKind === 'token') {
        const upper = operandValue.toUpperCase()
        if (upper === 'NOT') {
          modifier = 'NOT'
          index += 1
          if (index >= tokens.length) throw new Error('dangling NOT modifier')
          operandKind = tokens[index].kind
          operandValue = tokens[index].value
        } else if (operandValue === '+') {
          modifier = 'REQ'
          index += 1
          if (index >= tokens.length) throw new Error('dangling + modifier')
          operandKind = tokens[index].kind
          operandValue = tokens[index].value
        } else if (operandValue === '-') {
          modifier = 'NOT'
          index += 1
          if (index >= tokens.length) throw new Error('dangling - modifier')
          operandKind = tokens[index].kind
          operandValue = tokens[index].value
        } else if (operandValue.startsWith('+')) {
          modifier = 'REQ'
          operandKind = 'token'
          operandValue = operandValue.slice(1)
        } else if (operandValue.startsWith('-')) {
          modifier = 'NOT'
          operandKind = 'token'
          operandValue = operandValue.slice(1)
        }
      }

      if (modifier !== 'NONE' && operandKind === 'token' && BOOL_OPS.has(operandValue.toUpperCase())) {
        throw new Error('dangling clause modifier before boolean operator')
      }
      if (modifier !== 'NONE' && operandKind === 'token' && operandValue === '') {
        throw new Error('dangling clause modifier with empty operand')
      }
      if (modifier !== 'NONE' && operandKind === 'paren' && operandValue === ')') {
        throw new Error('dangling clause modifier before closing parenthesis')
      }

      let expr = null
      if (operandKind === 'paren') {
        if (operandValue === '(') {
          const parsed = parseClauseSequence(index + 1, true)
          if (parsed.index >= tokens.length || tokens[parsed.index].kind !== 'paren' || tokens[parsed.index].value !== ')') {
            throw new Error('unclosed group in query')
          }
          expr = buildFromClauses(parsed.clauses)
          index = parsed.index + 1
        } else {
          if (stopOnRParen) break
          throw new Error('unmatched closing parenthesis')
        }
      } else {
        if (operandKind === 'token' && operandValue === '') throw new Error('empty clause token')
        expr = compileOperand(operandKind, operandValue)
        index += 1
      }

      applyAddClause(clauses, pendingConj, modifier, expr)
      pendingConj = 'NONE'
      sawOperand = true
    }

    if (pendingConj !== 'NONE') throw new Error('dangling boolean operator')
    return { clauses, index }
  }

  return buildFromClauses(parseClauseSequence(0, false).clauses)
}

function containsSensitiveForm(db, text, options) {
  if (!options.respectDiacritics && !options.respectHamza && !options.respectLetter && !options.respectDigit) return false
  return hasSensitiveFormsViaUdf(db, text, options)
}

function parseStrictLiterals(db, query, runtimeField) {
  const tokens = splitQuery(query)
  const hasAnd = tokens.some(t => t.kind === 'token' && t.value.toUpperCase() === 'AND')
  const hasOr = tokens.some(t => t.kind === 'token' && t.value.toUpperCase() === 'OR')
  const hasParen = tokens.some(t => t.kind === 'paren')
  const conjunctive = hasAnd && !hasOr && !hasParen

  const out = []

  function appendOperand(kind, value, occur) {
    if (kind === 'token') {
      const upper = value.toUpperCase()
      if (BOOL_OPS.has(upper) || value === '(' || value === ')') return
      const scoped = parseScopedToken(value)
      if (scoped.scope && scoped.scope !== runtimeField) return
      const stripped = stripBoost(scoped.raw).base
      if (stripped === '' || stripped.includes('*') || stripped.includes('?') || parseFuzzyToken(stripped)) return
      if (analyzeTextRuntime(db, stripped).length === 0) return
      out.push({
        text: stripped,
        isPhrase: false,
        required: occur === 'must' || (occur === 'optional' && conjunctive),
        prohibited: occur === 'must_not'
      })
      return
    }

    if (kind === 'phrase' || kind === 'phrase_slop' || kind === 'phrase_boost' || kind === 'phrase_slop_boost') {
      let text = value
      if (kind === 'phrase_slop') text = value.split('\t', 2)[1]
      if (kind === 'phrase_boost') text = value.split('\t', 2)[1]
      if (kind === 'phrase_slop_boost') text = value.split('\t', 3)[2]
      if (text.trim() === '') return
      out.push({
        text,
        isPhrase: true,
        required: occur === 'must' || (occur === 'optional' && conjunctive),
        prohibited: occur === 'must_not'
      })
      return
    }

    if (kind === 'field_phrase' || kind === 'field_phrase_slop' || kind === 'field_phrase_boost' || kind === 'field_phrase_slop_boost') {
      let scope = null
      let text = ''
      if (kind === 'field_phrase') {
        const parsed = parseScopedToken(value)
        scope = parsed.scope
        text = parsed.raw
      } else if (kind === 'field_phrase_slop') {
        const parsed = parseScopedToken(value)
        scope = parsed.scope
        text = parsed.raw.split('\t', 2)[1] ?? ''
      } else if (kind === 'field_phrase_boost') {
        const [scoped, _boost, phrase] = value.split('\t', 3)
        scope = scoped
        text = phrase ?? ''
      } else {
        const [scoped, _boost, _slop, phrase] = value.split('\t', 4)
        scope = scoped
        text = phrase ?? ''
      }
      if (scope && scope !== runtimeField) return
      if (text.trim() === '') return
      out.push({
        text,
        isPhrase: true,
        required: occur === 'must' || (occur === 'optional' && conjunctive),
        prohibited: occur === 'must_not'
      })
      return
    }

    if (kind === 'field_group' || kind === 'field_group_boost') {
      const parts = value.split('\t')
      const scope = parts[0]
      const inner = kind === 'field_group' ? parts.slice(1).join('\t') : parts.slice(2).join('\t')
      if (scope !== runtimeField) return
      const nested = parseStrictLiterals(db, inner, runtimeField)
      for (const literal of nested) {
        if (occur === 'must_not') {
          literal.required = false
          literal.prohibited = true
        } else if (occur === 'must') {
          literal.required = true
          literal.prohibited = false
        }
        out.push(literal)
      }
    }
  }

  let index = 0
  while (index < tokens.length) {
    const tok = tokens[index]
    if (tok.kind === 'paren') {
      index += 1
      continue
    }

    let occur = 'optional'
    let operandKind = tok.kind
    let operandValue = tok.value

    if (tok.kind === 'token') {
      const upper = tok.value.toUpperCase()
      if (BOOL_OPS.has(upper)) {
        index += 1
        continue
      }
      if (tok.value === '+' || tok.value === '-') {
        occur = tok.value === '+' ? 'must' : 'must_not'
        index += 1
        if (index >= tokens.length) break
        operandKind = tokens[index].kind
        operandValue = tokens[index].value
      } else if (tok.value.startsWith('+')) {
        occur = 'must'
        operandKind = 'token'
        operandValue = tok.value.slice(1)
      } else if (tok.value.startsWith('-')) {
        occur = 'must_not'
        operandKind = 'token'
        operandValue = tok.value.slice(1)
      }
    }

    appendOperand(operandKind, operandValue, occur)
    index += 1
  }

  if (!conjunctive && !hasOr && !hasParen) {
    const positive = out.filter(x => !x.prohibited)
    if (positive.length === 1) positive[0].required = true
  }

  return out
}

function normalizeTextForStrict(db, value, options) {
  return normalizeViaUdf(db, value, options, { lowercase: true, keepWildcards: false, trim: false })
}

function strictLiteralMatches(db, rawText, literal, options) {
  if (!containsSensitiveForm(db, literal.text, options)) return true
  const doc = normalizeTextForStrict(db, rawText, options)
  const lit = normalizeTextForStrict(db, literal.text, options).trim()
  if (lit === '') return true
  return doc.includes(lit)
}

function literalPresenceMatches(db, rawText, literal, options) {
  const doc = normalizeTextForStrict(db, rawText, options)
  const lit = normalizeTextForStrict(db, literal.text, options).trim()
  if (lit === '') return true
  return doc.includes(lit)
}

function filterHitsForStrictModes(db, query, runtimeField, hits, options) {
  if (!options.respectDiacritics && !options.respectHamza && !options.respectLetter && !options.respectDigit) return hits

  const literalsByField = {
    page: runtimeField === 'title' ? [] : parseStrictLiterals(db, query, 'page'),
    title: runtimeField === 'page' ? [] : parseStrictLiterals(db, query, 'title')
  }

  const out = []
  for (const hit of hits) {
    const literals = literalsByField[hit.field] ?? []
    if (literals.length === 0) {
      out.push(hit)
      continue
    }

    const evaluated = literals.filter(lit => containsSensitiveForm(db, lit.text, options))
    if (evaluated.length === 0) {
      out.push(hit)
      continue
    }

    const prohibited = evaluated.filter(lit => lit.prohibited)
    const required = evaluated.filter(lit => lit.required && !lit.prohibited)
    const optional = evaluated.filter(lit => !lit.required && !lit.prohibited)
    const nonsensitiveOptional = literals.filter(lit => !lit.prohibited && !lit.required && !containsSensitiveForm(db, lit.text, options))

    if (prohibited.some(lit => strictLiteralMatches(db, hit.raw, lit, options))) continue
    if (required.length > 0 && !required.every(lit => strictLiteralMatches(db, hit.raw, lit, options))) continue

    if (required.length === 0 && optional.length > 0) {
      if (nonsensitiveOptional.some(lit => literalPresenceMatches(db, hit.raw, lit, options))) {
        out.push(hit)
        continue
      }
      if (!optional.some(lit => strictLiteralMatches(db, hit.raw, lit, options))) continue
    }

    out.push(hit)
  }

  return out
}

function hitMatchesExpression(db, field, rowid, expression) {
  const table = field === 'page' ? 'page_fts' : 'title_fts'
  const idCol = state.searchKind === 'fts5' ? 'rowid' : 'docid'
  const rows = queryRows(db, `SELECT 1 AS ok FROM ${table} WHERE ${idCol} = ? AND ${table} MATCH ? LIMIT 1`, [rowid, expression])
  return rows.length > 0
}

function extractBoostedClauseExpressions(db, query, field, options) {
  const tokens = splitQuery(query)
  const out = []
  let index = 0

  function appendBoosted(kind, value, boost, allowScope = null) {
    if (boost === null || Math.abs(boost - 1) < 1e-12) return
    if (allowScope && allowScope !== field) return

    let expression = null
    if (kind === 'token') expression = compileTokenExpression(db, field, value, options)
    if (kind === 'phrase') expression = compilePhraseExpression(db, value, options)
    if (kind === 'phrase_slop') expression = compilePhraseSlopExpression(db, value, options)
    if (kind === 'field_phrase') expression = compilePhraseExpression(db, value, options)
    if (kind === 'field_phrase_slop') expression = compilePhraseSlopExpression(db, value, options)
    if (kind === 'field_group') expression = compileMatchExpression(db, field, value, options)

    if (expression) out.push({ expression, boost })
  }

  while (index < tokens.length) {
    const token = tokens[index]
    let operand = token
    let occur = 'optional'

    if (token.kind === 'token') {
      if (token.value === '+' || token.value === '-') {
        occur = token.value === '+' ? 'must' : 'must_not'
        index += 1
        if (index >= tokens.length) break
        operand = tokens[index]
      } else if (token.value.startsWith('+')) {
        occur = 'must'
        operand = { kind: 'token', value: token.value.slice(1) }
      } else if (token.value.startsWith('-')) {
        occur = 'must_not'
        operand = { kind: 'token', value: token.value.slice(1) }
      }
    }

    if (occur === 'must_not') {
      index += 1
      continue
    }

    if (operand.kind === 'token') {
      const upper = operand.value.toUpperCase()
      if (BOOL_OPS.has(upper) || operand.value === '(' || operand.value === ')') {
        index += 1
        continue
      }
      const scoped = parseScopedToken(operand.value)
      if (scoped.scope && scoped.scope !== field) {
        index += 1
        continue
      }
      const { base, boost } = stripBoost(scoped.raw)
      if (base !== '') {
        const tokenForCompile = scoped.scope ? `${scoped.scope}:${base}` : base
        appendBoosted('token', tokenForCompile, boost, scoped.scope)
      }
      index += 1
      continue
    }

    if (operand.kind === 'phrase_boost') {
      const [boostRaw, phrase] = operand.value.split('\t', 2)
      appendBoosted('phrase', phrase, Number(boostRaw))
      index += 1
      continue
    }

    if (operand.kind === 'phrase_slop_boost') {
      const [boostRaw, slopRaw, phrase] = operand.value.split('\t', 3)
      appendBoosted('phrase_slop', `${slopRaw}\t${phrase}`, Number(boostRaw))
      index += 1
      continue
    }

    if (operand.kind === 'field_phrase_boost') {
      const [scope, boostRaw, phrase] = operand.value.split('\t', 3)
      appendBoosted('field_phrase', phrase, Number(boostRaw), scope)
      index += 1
      continue
    }

    if (operand.kind === 'field_phrase_slop_boost') {
      const [scope, boostRaw, slopRaw, phrase] = operand.value.split('\t', 4)
      appendBoosted('field_phrase_slop', `${slopRaw}\t${phrase}`, Number(boostRaw), scope)
      index += 1
      continue
    }

    if (operand.kind === 'field_group_boost') {
      const [scope, boostRaw, inner] = operand.value.split('\t', 3)
      appendBoosted('field_group', inner, Number(boostRaw), scope)
      index += 1
      continue
    }

    index += 1
  }

  return out
}

function applyClauseBoosts(db, query, field, options, hits) {
  const boosted = extractBoostedClauseExpressions(db, query, field, options)
  if (boosted.length === 0) return hits

  const out = []
  for (const hit of hits) {
    let factor = 1
    for (const clause of boosted) {
      if (hitMatchesExpression(db, hit.field, hit.rowid, clause.expression)) factor *= clause.boost
    }
    const score = factor === 1 ? hit.score : hit.score * factor
    out.push({ ...hit, score })
  }

  out.sort((a, b) => a.score - b.score || a.book_id - b.book_id || a.item_id - b.item_id)
  return out
}

function parseBoostFactorForField(query, runtimeField) {
  const raw = query.trim()
  if (raw === '') return 1

  const fieldGroupBoost = raw.match(/^(page|title):\((.+)\)\^([0-9]+(?:\.[0-9]+)?)$/i)
  if (fieldGroupBoost) return fieldGroupBoost[1].toLowerCase() === runtimeField ? Number(fieldGroupBoost[3]) : 1

  const fieldPhraseBoost = raw.match(/^(page|title):"[^"]+"(?:~[0-9]+)?\^([0-9]+(?:\.[0-9]+)?)$/i)
  if (fieldPhraseBoost) return fieldPhraseBoost[1].toLowerCase() === runtimeField ? Number(fieldPhraseBoost[2]) : 1

  const fieldTermBoost = raw.match(/^(page|title):([^\s"():*?~+\-]+)\^([0-9]+(?:\.[0-9]+)?)$/i)
  if (fieldTermBoost) return fieldTermBoost[1].toLowerCase() === runtimeField ? Number(fieldTermBoost[3]) : 1

  const phraseBoost = raw.match(/^"[^"]+"(?:~[0-9]+)?\^([0-9]+(?:\.[0-9]+)?)$/)
  if (phraseBoost) return Number(phraseBoost[1])

  const termBoost = raw.match(/^([^\s"():*?~+\-]+)\^([0-9]+(?:\.[0-9]+)?)$/)
  if (termBoost) return Number(termBoost[2])

  const groupBoost = raw.match(/^\((.+)\)\^([0-9]+(?:\.[0-9]+)?)$/)
  if (groupBoost) return Number(groupBoost[2])

  return 1
}

function stripBoostForRanking(query, runtimeField) {
  const raw = query.trim()
  if (raw === '') return raw

  const fieldGroupBoost = raw.match(/^(page|title):\((.+)\)\^([0-9]+(?:\.[0-9]+)?)$/i)
  if (fieldGroupBoost) return fieldGroupBoost[1].toLowerCase() === runtimeField ? fieldGroupBoost[2] : '__nohit__'

  const fieldGroup = raw.match(/^(page|title):\((.+)\)$/i)
  if (fieldGroup) return fieldGroup[1].toLowerCase() === runtimeField ? fieldGroup[2] : '__nohit__'

  const fieldPhrase = raw.match(/^((page|title):"[^"]+"(?:~[0-9]+)?)\^([0-9]+(?:\.[0-9]+)?)$/i)
  if (fieldPhrase) return fieldPhrase[2].toLowerCase() === runtimeField ? fieldPhrase[1] : raw

  const fieldTerm = raw.match(/^((page|title):[^\s"():*?~+\-]+)\^([0-9]+(?:\.[0-9]+)?)$/i)
  if (fieldTerm) return fieldTerm[2].toLowerCase() === runtimeField ? fieldTerm[1] : raw

  const phrase = raw.match(/^("[^"]+"(?:~[0-9]+)?)\^([0-9]+(?:\.[0-9]+)?)$/)
  if (phrase) return phrase[1]

  const term = raw.match(/^([^\s"():*?~+\-]+)\^([0-9]+(?:\.[0-9]+)?)$/)
  if (term) return term[1]

  const group = raw.match(/^\((.+)\)\^([0-9]+(?:\.[0-9]+)?)$/)
  if (group) return group[1]

  return raw
}

function queryHitsByField(db, field, expression, limit) {
  const table = field === 'page' ? 'page_fts' : 'title_fts'
  const docs = field === 'page' ? 'page_docs' : 'title_docs'

  const rows =
    state.searchKind === 'fts5'
      ? queryRows(
          db,
          `
            SELECT d.rowid, d.book_id, d.item_id, d.raw_body, d.analyzed_body, bm25(${table}) AS score
            FROM ${table}
            JOIN ${docs} d ON d.rowid = ${table}.rowid
            WHERE ${table} MATCH ?
            ORDER BY score, d.book_id, d.item_id
            LIMIT ?
          `,
          [expression, limit]
        )
      : queryRows(
          db,
          `
            SELECT d.rowid, d.book_id, d.item_id, d.raw_body, d.analyzed_body, 0.0 AS score
            FROM ${table}
            JOIN ${docs} d ON d.rowid = ${table}.docid
            WHERE ${table} MATCH ?
            ORDER BY d.book_id, d.item_id
            LIMIT ?
          `,
          [expression, limit]
        )

  return rows.map(row => ({
    rowid: Number(row.rowid),
    field,
    book_id: Number(row.book_id),
    item_id: Number(row.item_id),
    raw: String(row.raw_body),
    analyzed: String(row.analyzed_body),
    score: Number(row.score)
  }))
}

function countHitsByField(db, field, expression) {
  const table = field === 'page' ? 'page_fts' : 'title_fts'
  if (state.searchKind === 'fts5') {
    return Number(queryRows(db, `SELECT count(*) AS c FROM ${table} WHERE ${table} MATCH ?`, [expression])[0]?.c ?? 0)
  }
  return Number(queryRows(db, `SELECT count(*) AS c FROM ${table} WHERE ${table} MATCH ?`, [expression])[0]?.c ?? 0)
}

function compileOperandForExplain(db, field, kind, value, options) {
  if (kind === 'phrase') return compilePhraseExpression(db, value, options)
  if (kind === 'phrase_boost') return compilePhraseExpression(db, value.split('\t', 2)[1], options)
  if (kind === 'phrase_slop') return compilePhraseSlopExpression(db, value, options)
  if (kind === 'phrase_slop_boost') return compilePhraseSlopExpression(db, value.split('\t', 3).slice(1).join('\t'), options)
  if (kind === 'field_phrase') {
    const [scope, phrase] = value.split(':', 2)
    if (scope !== field) return null
    return compilePhraseExpression(db, phrase, options)
  }
  if (kind === 'field_phrase_boost') {
    const [scope, _boost, phrase] = value.split('\t', 3)
    if (scope !== field) return null
    return compilePhraseExpression(db, phrase, options)
  }
  if (kind === 'field_phrase_slop') {
    const [scope, payload] = value.split(':', 2)
    if (scope !== field) return null
    return compilePhraseSlopExpression(db, payload, options)
  }
  if (kind === 'field_phrase_slop_boost') {
    const [scope, _boost, slopRaw, phrase] = value.split('\t', 4)
    if (scope !== field) return null
    return compilePhraseSlopExpression(db, `${slopRaw}\t${phrase}`, options)
  }
  if (kind === 'field_group') {
    const [scope, inner] = value.split('\t', 2)
    if (scope !== field) return null
    return compileMatchExpression(db, field, inner, options)
  }
  if (kind === 'field_group_boost') {
    const [scope, _boost, inner] = value.split('\t', 3)
    if (scope !== field) return null
    return compileMatchExpression(db, field, inner, options)
  }
  if (kind === 'token') {
    const upper = value.toUpperCase()
    if (BOOL_OPS.has(upper) || value === '+' || value === '-' || value === '(' || value === ')' || upper === 'NOT') return null
    return compileTokenExpression(db, field, value, options)
  }
  return null
}

function buildClauseCounts(db, field, query, options) {
  const tokens = splitQuery(query)
  const out = []
  const seen = new Set()
  for (const token of tokens) {
    try {
      const expression = compileOperandForExplain(db, field, token.kind, token.value, { ...options, _explainEvents: null })
      if (!expression) continue
      const key = `${field}::${token.kind}::${token.value}::${expression}`
      if (seen.has(key)) continue
      seen.add(key)
      out.push({
        field,
        clause: token.value,
        kind: token.kind,
        expression,
        hitCount: countHitsByField(db, field, expression)
      })
    } catch (_error) {}
  }
  return out
}

function sanitizeExplainEvents(db, events) {
  return (events || []).map(event => ({
    ...event,
    expansionCount: Array.isArray(event.expansions) ? event.expansions.length : 0,
    expansions: Array.isArray(event.expansions) ? event.expansions.slice(0, 20) : [],
    hitCount: event.expression && event.field ? countHitsByField(db, event.field, event.expression) : null
  }))
}

function runSearch(db, query, options) {
  if (query.trim() === '') throw new Error('query is empty after parsing')

  const compiled = {
    _runtime: {
      tokenizerDirective: state.tokenizerDirective,
      runtime: runtimeLabel()
    }
  }
  let hits = []
  const perField = {}

  const fields = options.field === 'both' ? ['page', 'title'] : [options.field]

  for (const field of fields) {
    const boostFactor = parseBoostFactorForField(query, field)
    const rankingQuery = stripBoostForRanking(query, field)
    const expression = compileMatchExpression(db, field, rankingQuery, options)
    compiled[field] = expression

    let fieldHits = queryHitsByField(db, field, expression, options.limit)
    if (Math.abs(boostFactor - 1) >= 1e-12) fieldHits = fieldHits.map(hit => ({ ...hit, score: hit.score * boostFactor }))
    fieldHits = applyClauseBoosts(db, rankingQuery, field, options, fieldHits)
    fieldHits = filterHitsForStrictModes(db, query, field, fieldHits, options)
    perField[field] = {
      expression,
      totalHits: countHitsByField(db, field, expression),
      clauseCounts: buildClauseCounts(db, field, rankingQuery, options)
    }
    hits.push(...fieldHits)
  }

  hits.sort((a, b) => a.score - b.score || a.field.localeCompare(b.field) || a.book_id - b.book_id || a.item_id - b.item_id)
  return {
    compiled,
    hits: hits.slice(0, options.limit),
    explain: {
      rawQuery: query,
      perField,
      expansions: sanitizeExplainEvents(db, options._explainEvents)
    }
  }
}

function renderHits(targetEl, hits) {
  if (hits.length === 0) {
    targetEl.innerHTML = '<p>No hits</p>'
    return
  }

  const rows = hits
    .map(
      row => `
        <tr>
          <td>${row.field}</td>
          <td>${row.book_id}</td>
          <td>${row.item_id}</td>
          <td>${row.score.toFixed(6)}</td>
          <td class='raw'>${row.raw}</td>
        </tr>
      `
    )
    .join('')

  targetEl.innerHTML = `
    <table>
      <thead>
        <tr>
          <th>field</th>
          <th>book_id</th>
          <th>item_id</th>
          <th>score</th>
          <th>raw</th>
        </tr>
      </thead>
      <tbody>${rows}</tbody>
    </table>
  `
}

function runProbe() {
  if (!state.db) return
  const text = String(els.probeText.value || '')
  const baseOptions = collectOptions()
  const relaxedOptions = modeOptions(baseOptions, 'relaxed')
  const strictOptions = modeOptions(baseOptions, 'strict')
  const payload = {
    tokenizerDirective: state.tokenizerDirective,
    runtime: runtimeLabel(),
    input: text,
    tokens: [],
    udf: {
      relaxedNormalized: '',
      strictNormalized: '',
      strictSensitive: false
    },
    error: null
  }
  try {
    payload.tokens = analyzeTextRuntime(state.db, text)
    payload.udf.relaxedNormalized = normalizeViaUdf(state.db, text, relaxedOptions, {
      lowercase: false,
      keepWildcards: false,
      trim: true
    })
    payload.udf.strictNormalized = normalizeViaUdf(state.db, text, strictOptions, {
      lowercase: true,
      keepWildcards: false,
      trim: false
    })
    payload.udf.strictSensitive = hasSensitiveFormsViaUdf(state.db, text, strictOptions)
  } catch (error) {
    payload.error = String(error?.message || error)
  }
  els.probeOutput.textContent = JSON.stringify(payload, null, 2)
}

function collectOptions() {
  return {
    allowPrefix: els.allowPrefix.checked,
    allowSuffix: els.allowSuffix.checked,
    allowWildcard: els.allowWildcard.checked,
    allowFuzzy: els.allowFuzzy.checked,
    respectDiacritics: els.respectDiacritics.checked,
    respectHamza: els.respectHamza.checked,
    respectLetter: els.respectLetter.checked,
    respectDigit: els.respectDigit.checked,
    suffixMax: Math.max(1, Number(els.suffixMax.value || defaultSearchOptions.suffixMax)),
    wildcardMax: Math.max(1, Number(els.wildcardMax.value || defaultSearchOptions.wildcardMax)),
    fuzzyMax: Math.max(1, Number(els.fuzzyMax.value || defaultSearchOptions.fuzzyMax)),
    limit: Math.max(1, Number(els.limit.value || defaultSearchOptions.limit)),
    field: els.field.value
  }
}

function modeOptions(baseOptions, mode) {
  if (mode === 'strict') return { ...baseOptions, _explainEvents: [] }
  return {
    ...baseOptions,
    respectDiacritics: false,
    respectHamza: false,
    respectLetter: false,
    respectDigit: false,
    _explainEvents: []
  }
}

function hitDocKey(hit) {
  return `${hit.book_id}:${hit.item_id}`
}

function evaluateModeAssertions(hits, modeAssertion = {}) {
  const keys = new Set(hits.map(hitDocKey))
  const failures = []
  if (typeof modeAssertion.minHits === 'number' && hits.length < modeAssertion.minHits) {
    failures.push(`expected at least ${modeAssertion.minHits} hits, got ${hits.length}`)
  }
  if (typeof modeAssertion.maxHits === 'number' && hits.length > modeAssertion.maxHits) {
    failures.push(`expected at most ${modeAssertion.maxHits} hits, got ${hits.length}`)
  }
  if (Array.isArray(modeAssertion.mustInclude)) {
    for (const key of modeAssertion.mustInclude) {
      if (!keys.has(key)) failures.push(`missing expected doc ${key}`)
    }
  }
  return { pass: failures.length === 0, failures }
}

function evaluateScenarioAssertions(scenarioName, query, relaxedHits, strictHits) {
  const preset = SCENARIO_PRESETS[scenarioName]
  const expectedQuery = String(preset?.query || '').trim()
  const activeQuery = String(query || '').trim()
  if (expectedQuery !== '' && activeQuery !== expectedQuery) {
    return {
      applicable: false,
      reason: `query differs from scenario preset (${scenarioName})`,
      relaxed: { pass: true, failures: [], skipped: true },
      strict: { pass: true, failures: [], skipped: true },
      cross: { pass: true, failures: [], skipped: true }
    }
  }
  const assertions = preset?.assertions || {}
  const relaxed = evaluateModeAssertions(relaxedHits, assertions.relaxed)
  const strict = evaluateModeAssertions(strictHits, assertions.strict)
  const crossFailures = []
  const cross = assertions.cross || {}
  if (cross.strictLessOrEqualRelaxed && strictHits.length > relaxedHits.length) {
    crossFailures.push(`expected strict hits <= relaxed hits (${strictHits.length} > ${relaxedHits.length})`)
  }
  if (cross.expectDifferentCounts && strictHits.length === relaxedHits.length) {
    crossFailures.push(`expected strict/relaxed hit counts to differ (both ${strictHits.length})`)
  }
  return {
    applicable: true,
    reason: '',
    relaxed,
    strict,
    cross: { pass: crossFailures.length === 0, failures: crossFailures }
  }
}

function renderAssertionBadge(targetEl, label, assertionResult) {
  if (!targetEl) return
  if (assertionResult?.skipped) {
    targetEl.className = 'result-badge'
    targetEl.textContent = `${label}: N/A`
    return
  }
  const ok = assertionResult?.pass !== false
  const detail = assertionResult?.failures?.length ? ` · ${assertionResult.failures.join('; ')}` : ''
  targetEl.className = `result-badge ${ok ? 'pass' : 'fail'}`
  targetEl.textContent = `${label}: ${ok ? 'PASS' : 'FAIL'}${detail}`
}

function buildExplainPayload(query, baseOptions, relaxedResult, strictResult, assertionSummary) {
  return {
    query,
    runtime: runtimeLabel(),
    runtimeInfo: state.runtimeInfo,
    tokenizerDirective: state.tokenizerDirective,
    options: {
      allowPrefix: baseOptions.allowPrefix,
      allowSuffix: baseOptions.allowSuffix,
      allowWildcard: baseOptions.allowWildcard,
      allowFuzzy: baseOptions.allowFuzzy,
      field: baseOptions.field,
      limit: baseOptions.limit
    },
    assertions: assertionSummary,
    relaxed: relaxedResult.explain,
    strict: strictResult.explain
  }
}

async function initDb(pageDocs, titleDocs) {
  if (!state.sqlite3) state.sqlite3 = await initSqlite()
  const db = new state.sqlite3.oo1.DB(':memory:', 'c')
  state.runtimeInfo = collectRuntimeInfo(db)
  const tokenizerArgs = normalizeTokenizerArgs(els.tokenizerArgs?.value || '')
  state.tokenizerDirective = tokenizerDirectiveFromArgs(tokenizerArgs)
  if (!hasSQLiteTokenizerArTokenizer(db, state.tokenizerDirective)) {
    throw new Error('custom sqlite_tokenizer_ar tokenizer is required but was not found in sqlite3.wasm')
  }
  if (!hasSQLiteTokenizerArUdfs(db)) {
    throw new Error('custom sqlite3.wasm is missing required sqlite_tokenizer_ar helper UDFs (analyze/positions/normalize/stem)')
  }

  execSql(
    db,
    `
      CREATE TABLE page_docs (
        rowid INTEGER PRIMARY KEY,
        book_id INTEGER NOT NULL,
        item_id INTEGER NOT NULL,
        raw_body TEXT NOT NULL,
        analyzed_body TEXT NOT NULL
      );
      CREATE TABLE title_docs (
        rowid INTEGER PRIMARY KEY,
        book_id INTEGER NOT NULL,
        item_id INTEGER NOT NULL,
        raw_body TEXT NOT NULL,
        analyzed_body TEXT NOT NULL
      );
    `
  )

  let created = false
  try {
    const tokenizerClause = `, tokenize='${getTokenizerDirectiveForSql()}'`
    execSql(db, `CREATE VIRTUAL TABLE page_fts USING fts5(body, content='page_docs', content_rowid='rowid'${tokenizerClause})`)
    execSql(db, `CREATE VIRTUAL TABLE title_fts USING fts5(body, content='title_docs', content_rowid='rowid'${tokenizerClause})`)
    const createdSql = String(queryRows(db, "SELECT sql FROM sqlite_master WHERE name = 'page_fts' LIMIT 1")[0]?.sql ?? '')
    if (createdSql.toLowerCase().includes('fts5')) {
      state.searchKind = 'fts5'
      created = true
    }
  } catch (_error) {}

  if (!created) {
    try {
      execSql(db, 'DROP TABLE IF EXISTS page_fts')
      execSql(db, 'DROP TABLE IF EXISTS title_fts')
      execSql(db, 'CREATE VIRTUAL TABLE page_fts USING fts4(body)')
      execSql(db, 'CREATE VIRTUAL TABLE title_fts USING fts4(body)')
      const createdSql = String(queryRows(db, "SELECT sql FROM sqlite_master WHERE name = 'page_fts' LIMIT 1")[0]?.sql ?? '')
      if (createdSql.toLowerCase().includes('fts4')) {
        state.searchKind = 'fts4'
        created = true
      }
    } catch (_error) {}
  }

  if (!created) throw new Error('this SQLite WASM build does not include FTS3/FTS5')

  for (const doc of pageDocs) {
    const indexed = doc.raw
    execSql(db, 'INSERT INTO page_docs(rowid, book_id, item_id, raw_body, analyzed_body) VALUES (?, ?, ?, ?, ?)', [doc.rowid, doc.book_id, doc.item_id, doc.raw, indexed])
    execSql(db, state.searchKind === 'fts5' ? 'INSERT INTO page_fts(rowid, body) VALUES (?, ?)' : 'INSERT INTO page_fts(docid, body) VALUES (?, ?)', [doc.rowid, indexed])
  }

  for (const doc of titleDocs) {
    const indexed = doc.raw
    execSql(db, 'INSERT INTO title_docs(rowid, book_id, item_id, raw_body, analyzed_body) VALUES (?, ?, ?, ?, ?)', [doc.rowid, doc.book_id, doc.item_id, doc.raw, indexed])
    execSql(db, state.searchKind === 'fts5' ? 'INSERT INTO title_fts(rowid, body) VALUES (?, ?)' : 'INSERT INTO title_fts(docid, body) VALUES (?, ?)', [doc.rowid, indexed])
  }

  if (state.searchKind === 'fts5') {
    execSql(db, "CREATE VIRTUAL TABLE page_vocab USING fts5vocab(page_fts, 'row')")
    execSql(db, "CREATE VIRTUAL TABLE title_vocab USING fts5vocab(title_fts, 'row')")
  } else {
    execSql(db, 'CREATE VIRTUAL TABLE page_vocab USING fts4aux(page_fts)')
    execSql(db, 'CREATE VIRTUAL TABLE title_vocab USING fts4aux(title_fts)')
  }

  return db
}

async function rebuildFromCorpus(runQueryAfter = true) {
  const pageDocs = parseCorpusText(els.pageCorpus.value, 'page corpus')
  const titleDocs = parseCorpusText(els.titleCorpus.value, 'title corpus')
  const nextDb = await initDb(pageDocs, titleDocs)
  if (state.db && typeof state.db.close === 'function') state.db.close()
  state.db = nextDb
  const tokenizerArgs = normalizeTokenizerArgs(els.tokenizerArgs?.value || '')
  const tokenizerSuffix = tokenizerArgs ? ` · tokenizer_args=${tokenizerArgs}` : ''
  setStatus(`SQLite WASM ready (${state.searchKind}) · ${runtimeLabel()}${tokenizerSuffix} · page=${pageDocs.length} title=${titleDocs.length}`)
  runProbe()
  updateUrlState()
  if (runQueryAfter) runCurrentQuery()
}

function wireUi() {
  els.run.addEventListener('click', () => runCurrentQuery())
  els.query.addEventListener('keydown', event => {
    if (event.key === 'Enter') runCurrentQuery()
  })
  els.applyOptionPreset.addEventListener('click', () => {
    try {
      applyOptionPreset(els.optionPreset.value)
      runProbe()
      runCurrentQuery()
    } catch (error) {
      setStatus(`error: ${error.message}`, true)
    }
  })
  els.applyScenario.addEventListener('click', async () => {
    try {
      await applyScenarioPreset(els.scenarioPreset.value)
      await rebuildFromCorpus(true)
    } catch (error) {
      setStatus(`error: ${error.message}`, true)
    }
  })
  els.applyTokenizerPreset.addEventListener('click', async () => {
    try {
      applyTokenizerPreset(els.tokenizerPreset.value)
      await rebuildFromCorpus(true)
    } catch (error) {
      setStatus(`error: ${error.message}`, true)
    }
  })
  els.tokenizerPreset.addEventListener('change', () => {
    if (els.tokenizerPreset.value === 'custom') return
    try {
      applyTokenizerPreset(els.tokenizerPreset.value)
      runProbe()
      updateUrlState()
    } catch (error) {
      setStatus(`error: ${error.message}`, true)
    }
  })
  els.tokenizerArgs.addEventListener('keydown', async event => {
    if (event.key !== 'Enter') return
    try {
      setTokenizerPresetFromArgs(els.tokenizerArgs.value)
      await rebuildFromCorpus(true)
    } catch (error) {
      setStatus(`error: ${error.message}`, true)
    }
  })
  els.tokenizerArgs.addEventListener('change', async () => {
    try {
      setTokenizerPresetFromArgs(els.tokenizerArgs.value)
      await rebuildFromCorpus(true)
    } catch (error) {
      setStatus(`error: ${error.message}`, true)
    }
  })
  els.probeRun.addEventListener('click', () => runProbe())
  els.probeText.addEventListener('keydown', event => {
    if (event.key !== 'Enter') return
    event.preventDefault()
    runProbe()
  })
  els.applyCorpusPreset.addEventListener('click', async () => {
    try {
      const presetName = els.corpusPreset.value
      if (isFixtureCorpusPreset(presetName)) await ensureFixtureDocsLoaded()
      loadCorpusPreset(presetName)
      await rebuildFromCorpus(true)
    } catch (error) {
      setStatus(`error: ${error.message}`, true)
    }
  })
  els.rebuildCorpus.addEventListener('click', async () => {
    try {
      await rebuildFromCorpus(true)
    } catch (error) {
      setStatus(`error: ${error.message}`, true)
    }
  })
  els.resetCorpus.addEventListener('click', async () => {
    els.corpusPreset.value = 'sample'
    loadCorpusPreset('sample')
    els.optionPreset.value = 'lucene_default'
    applyOptionPreset('lucene_default')
    try {
      await rebuildFromCorpus(true)
    } catch (error) {
      setStatus(`error: ${error.message}`, true)
    }
  })

  for (const chip of els.chips) {
    chip.addEventListener('click', () => {
      els.query.value = chip.dataset.query ?? ''
      runCurrentQuery()
    })
  }
  els.shareState.addEventListener('click', async () => {
    try {
      const shareUrl = updateUrlState()
      if (globalThis.navigator?.clipboard?.writeText) {
        await globalThis.navigator.clipboard.writeText(shareUrl)
        setStatus(`share link copied · ${runtimeLabel()}`)
        return
      }
      setStatus(`share link ready: ${shareUrl}`)
    } catch (error) {
      setStatus(`error: ${error.message}`, true)
    }
  })
}

function runCurrentQuery() {
  if (!state.db) return
  const query = els.query.value
  const baseOptions = collectOptions()
  runProbe()
  try {
    const relaxedOptions = modeOptions(baseOptions, 'relaxed')
    const strictOptions = modeOptions(baseOptions, 'strict')
    const relaxedResult = runSearch(state.db, query, relaxedOptions)
    const strictResult = runSearch(state.db, query, strictOptions)
    const assertionSummary = evaluateScenarioAssertions(els.scenarioPreset.value, query, relaxedResult.hits, strictResult.hits)
    const explainPayload = buildExplainPayload(query, baseOptions, relaxedResult, strictResult, assertionSummary)

    state.lastComparison = { relaxedResult, strictResult, assertionSummary, explainPayload }

    els.explainOutput.textContent = JSON.stringify(explainPayload, null, 2)
    els.compiledRelaxed.textContent = JSON.stringify(relaxedResult.compiled, null, 2)
    els.compiledStrict.textContent = JSON.stringify(strictResult.compiled, null, 2)
    renderHits(els.hitsRelaxed, relaxedResult.hits)
    renderHits(els.hitsStrict, strictResult.hits)
    renderAssertionBadge(els.relaxedBadge, 'Relaxed', assertionSummary.relaxed)
    renderAssertionBadge(els.strictBadge, 'Strict', assertionSummary.strict)
    renderAssertionBadge(els.crossBadge, 'Cross', assertionSummary.cross)

    const tokenizerArgs = normalizeTokenizerArgs(els.tokenizerArgs?.value || '')
    const tokenizerSuffix = tokenizerArgs ? ` · tokenizer_args=${tokenizerArgs}` : ''
    const crossLabel = assertionSummary.applicable ? (assertionSummary.cross.pass ? 'assertions: ok' : 'assertions: fail') : 'assertions: n/a'
    setStatus(
      `ok: relaxed=${relaxedResult.hits.length} strict=${strictResult.hits.length} · ${crossLabel} · ${runtimeLabel()}${tokenizerSuffix}`
    )
    updateUrlState()
  } catch (error) {
    state.lastComparison = null
    els.explainOutput.textContent = '{}'
    els.compiledRelaxed.textContent = '{}'
    els.compiledStrict.textContent = '{}'
    els.hitsRelaxed.innerHTML = ''
    els.hitsStrict.innerHTML = ''
    renderAssertionBadge(els.relaxedBadge, 'Relaxed', { pass: false, failures: ['query failed'] })
    renderAssertionBadge(els.strictBadge, 'Strict', { pass: false, failures: ['query failed'] })
    renderAssertionBadge(els.crossBadge, 'Cross', { pass: false, failures: ['query failed'] })
    setStatus(`error: ${error.message}`, true)
  }
}

async function main() {
  try {
    try {
      await ensureFixtureDocsLoaded()
    } catch (_error) {}
    seedCorpusEditors()
    await applyUrlStateIfPresent()
    wireUi()
    await rebuildFromCorpus(true)
  } catch (error) {
    setStatus(explainInitError(error), true)
  }
}

function explainInitError(error) {
  const message = String(error?.message || error || '')
  const lower = message.toLowerCase()
  if (lower.includes('_abort_js') || lower.includes('webassembly.instantiate') || lower.includes('linkerror')) {
    return (
      'failed to initialize: sqlite3.mjs and sqlite3.wasm likely come from different builds. ' +
      'rebuild with ./playground/scripts/build_custom_wasm.sh /path/to/sqlite-source-tree and verify with ./playground/scripts/verify_custom_wasm.sh'
    )
  }
  if (lower.includes('both async and sync fetching of the wasm failed') || lower.includes('missing custom wasm at')) {
    return (
      'failed to initialize: custom sqlite3.wasm is missing/unreachable. ' +
      'build with ./playground/scripts/build_custom_wasm.sh /path/to/sqlite-source-tree'
    )
  }
  if (lower.includes('missing required sqlite_tokenizer_ar helper udfs') || lower.includes('tokenizer is required')) {
    return (
      'failed to initialize: custom wasm does not include full sqlite_tokenizer_ar registration. ' +
      'rebuild with ./playground/scripts/build_custom_wasm.sh /path/to/sqlite-source-tree'
    )
  }
  return `failed to initialize: ${message}`
}

main()
