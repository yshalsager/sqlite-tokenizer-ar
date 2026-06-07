#!/usr/bin/env node

const path = require('node:path')
const { buildRow, appendRow } = require('./append_validation_matrix_row.cjs')

function nowUtcDate() {
  return new Date().toISOString().slice(0, 10)
}

function readRequired(name) {
  const value = String(process.env[name] || '').trim()
  if (value === '') throw new Error(`missing required env: ${name}`)
  return value
}

function readOptional(name, fallback = '') {
  const value = String(process.env[name] || '').trim()
  return value === '' ? fallback : value
}

function main() {
  const matrixPath = path.resolve(process.cwd(), readOptional('PLAYGROUND_MATRIX_PATH', 'playground/WASM_VALIDATION_MATRIX.md'))
  const payload = {
    matrixRow: {
      dateUtc: readOptional('MATRIX_DATE_UTC', nowUtcDate()),
      os: readRequired('MATRIX_OS'),
      browser: readRequired('MATRIX_BROWSER'),
      sqliteVersion: readOptional('MATRIX_SQLITE_VERSION', '_n/a_'),
      sqliteSourcePrefix: readOptional('MATRIX_SOURCE_PREFIX', '_n/a_'),
      enableFts5: readOptional('MATRIX_ENABLE_FTS5', '_n/a_'),
      tokenizerUdfPreflight: readOptional('MATRIX_PREFLIGHT', 'pass'),
      scenarioPack: readOptional('MATRIX_SCENARIO_PACK', 'strict_hamza_letter + suffix *صور'),
      result: readOptional('MATRIX_RESULT', 'pass'),
      notes: readRequired('MATRIX_NOTES')
    }
  }

  const row = buildRow(payload)
  const result = appendRow(matrixPath, row)
  console.log(`${result.changed ? 'updated' : 'nochange'}: ${result.reason}`)
  console.log(row)
}

try {
  main()
} catch (error) {
  console.error(`error: ${error.message || String(error)}`)
  process.exit(1)
}
