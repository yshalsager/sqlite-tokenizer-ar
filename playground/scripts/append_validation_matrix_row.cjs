#!/usr/bin/env node

const fs = require('node:fs')
const path = require('node:path')

function esc(value) {
  return String(value ?? '').replaceAll('|', '\\|').trim()
}

function codeCell(value) {
  const text = esc(value)
  if (text === '' || /^_.*_$/.test(text)) return text
  return '`' + text + '`'
}

function buildRow(payload) {
  const row = payload.matrixRow
  if (!row) throw new Error('validation JSON missing matrixRow')
  const parts = [
    esc(row.dateUtc),
    esc(row.os),
    esc(row.browser),
    esc(row.sqliteVersion),
    codeCell(row.sqliteSourcePrefix),
    esc(row.enableFts5),
    esc(row.tokenizerUdfPreflight),
    codeCell(row.scenarioPack),
    esc(row.result),
    esc(row.notes)
  ]
  return `| ${parts.join(' | ')} |`
}

function readJson(file) {
  if (!fs.existsSync(file)) throw new Error(`validation json not found: ${file}`)
  return JSON.parse(fs.readFileSync(file, 'utf8'))
}

function appendRow(matrixPath, rowLine) {
  if (!fs.existsSync(matrixPath)) throw new Error(`matrix file not found: ${matrixPath}`)
  const text = fs.readFileSync(matrixPath, 'utf8')
  if (text.includes(rowLine)) return { changed: false, reason: 'row already exists (exact line)' }

  const keyFromRow = row => {
    const cells = row
      .split('|')
      .map(cell => cell.trim())
      .filter(Boolean)
      .map(cell => cell.replaceAll('`', ''))
    if (cells.length < 8) return ''
    return [cells[0], cells[2], cells[3], cells[4], cells[7]].join('||')
  }

  const incomingKey = keyFromRow(rowLine)
  if (incomingKey !== '') {
    for (const line of text.split('\n')) {
      const trimmed = line.trim()
      if (!trimmed.startsWith('|')) continue
      if (trimmed.startsWith('|---')) continue
      if (trimmed.includes('Date (UTC)')) continue
      if (keyFromRow(trimmed) === incomingKey) {
        return { changed: false, reason: 'row already exists (same key columns)' }
      }
    }
  }

  const marker = '\n## Target Coverage\n'
  const markerIndex = text.indexOf(marker)
  if (markerIndex === -1) throw new Error('failed to find "## Target Coverage" marker in matrix file')

  const before = text.slice(0, markerIndex)
  const after = text.slice(markerIndex)
  const next = `${before}${rowLine}\n${after}`
  fs.writeFileSync(matrixPath, next)
  return { changed: true, reason: 'row appended' }
}

function main() {
  const validationPath = process.argv[2] || path.resolve(process.cwd(), 'test-results/playground/validation.json')
  const matrixPath = process.argv[3] || path.resolve(process.cwd(), 'playground/WASM_VALIDATION_MATRIX.md')

  const payload = readJson(validationPath)
  const rowLine = buildRow(payload)
  const result = appendRow(matrixPath, rowLine)
  console.log(`${result.changed ? 'updated' : 'nochange'}: ${result.reason}`)
  console.log(rowLine)
}

module.exports = {
  esc,
  codeCell,
  buildRow,
  readJson,
  appendRow
}

if (require.main === module) {
  try {
    main()
  } catch (error) {
    console.error(`error: ${error.message || String(error)}`)
    process.exit(1)
  }
}
