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

function main() {
  const input = process.argv[2] || path.resolve(process.cwd(), 'test-results/playground/validation.json')
  if (!fs.existsSync(input)) {
    console.error(`error: validation json not found: ${input}`)
    process.exit(1)
  }

  const payload = JSON.parse(fs.readFileSync(input, 'utf8'))
  const row = payload.matrixRow
  if (!row) {
    console.error(`error: missing matrixRow in ${input}`)
    process.exit(1)
  }

  const line = [
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

  console.log(`| ${line.join(' | ')} |`)
}

main()
