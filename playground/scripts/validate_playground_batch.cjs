#!/usr/bin/env node

const fs = require('node:fs')
const path = require('node:path')
const { spawnSync } = require('node:child_process')
const { buildRow, appendRow } = require('./append_validation_matrix_row.cjs')

const ALLOWED_BROWSERS = new Set(['chromium', 'firefox', 'webkit'])

function nowUtcDate() {
  return new Date().toISOString().slice(0, 10)
}

function parseBrowsers(raw) {
  return String(raw || 'chromium,firefox,webkit')
    .split(',')
    .map(value => value.trim().toLowerCase())
    .filter(Boolean)
}

function browserLabel(browser, version = '') {
  const name = browser === 'webkit' ? 'WebKit' : browser[0].toUpperCase() + browser.slice(1)
  return `Headless ${name}${version ? ` ${version}` : ''}`
}

function parseValidationJsonFromStdout(stdout) {
  const marker = 'PLAYGROUND_VALIDATION_JSON='
  const line = String(stdout || '')
    .split('\n')
    .map(value => value.trim())
    .find(value => value.startsWith(marker))
  if (!line) return null
  try {
    return JSON.parse(line.slice(marker.length))
  } catch (_error) {
    return null
  }
}

function parseErrorMessage(stderr, status, signal) {
  const marker = 'PLAYGROUND_VALIDATION_ERROR='
  const raw = String(stderr || '')
  const lines = raw
    .split('\n')
    .map(value => value.trim())
    .filter(Boolean)
  const markerIndex = raw.indexOf(marker)
  const payload = markerIndex >= 0 ? raw.slice(markerIndex + marker.length).trim() : ''
  if (payload !== '') {
    const normalized = payload
      .replace(/\s+/g, ' ')
      .replace(/[╔╗╚╝║]/g, ' ')
      .trim()
    if (/libavif/i.test(normalized)) return 'missing host dependency: libavif16'
    const match = normalized.match(/missing dependencies[^.]*\.?/i)
    if (match) return match[0]
    if (normalized !== '') return normalized
  }

  const first = lines.find(line => !/^[╔╗╚╝║<>=\-]+$/.test(line)) || lines[0] || 'unknown validation error'
  return `${first} (status=${status ?? 'n/a'}${signal ? `, signal=${signal}` : ''})`
}

function buildBlockedPayload({ browser, message, scenarioPack, resultDir }) {
  return {
    validatedAtUtc: new Date().toISOString(),
    dateUtc: nowUtcDate(),
    browser: {
      engine: browser,
      version: ''
    },
    matrixRow: {
      dateUtc: nowUtcDate(),
      os: 'Linux (a remote Linux host)',
      browser: browserLabel(browser),
      sqliteVersion: '_n/a_',
      sqliteSourcePrefix: '_n/a_',
      enableFts5: '_n/a_',
      tokenizerUdfPreflight: 'blocked',
      scenarioPack: '_n/a_',
      result: 'blocked',
      notes: message
    },
    artifacts: {
      resultDir
    },
    failure: {
      message,
      scenarioPack
    }
  }
}

function writeJson(file, payload) {
  fs.mkdirSync(path.dirname(file), { recursive: true })
  fs.writeFileSync(file, JSON.stringify(payload, null, 2))
}

function runSingleBrowser({ browser, validatorScript, baseEnv, outRoot, scenario, suffixQuery }) {
  const resultDir = path.join(outRoot, browser)
  const child = spawnSync(
    process.execPath,
    [validatorScript],
    {
      env: {
        ...baseEnv,
        PLAYGROUND_BROWSER: browser,
        PLAYGROUND_OUT_DIR: resultDir,
        PLAYGROUND_SCENARIO: scenario,
        PLAYGROUND_SUFFIX_QUERY: suffixQuery
      },
      encoding: 'utf8'
    }
  )

  const validationPath = path.join(resultDir, 'validation.json')
  if (child.status === 0) {
    let payload = null
    if (fs.existsSync(validationPath)) {
      payload = JSON.parse(fs.readFileSync(validationPath, 'utf8'))
    }
    if (!payload) {
      payload = parseValidationJsonFromStdout(child.stdout)
      if (payload) writeJson(validationPath, payload)
    }
    if (!payload) {
      const message = `validator succeeded but no JSON payload emitted for ${browser}`
      const blocked = buildBlockedPayload({ browser, message, scenarioPack: `${scenario} + suffix ${suffixQuery}`, resultDir })
      writeJson(validationPath, blocked)
      return { browser, status: 'blocked', payload: blocked, stdout: child.stdout, stderr: child.stderr }
    }
    return { browser, status: 'pass', payload, stdout: child.stdout, stderr: child.stderr }
  }

  const message = parseErrorMessage(child.stderr, child.status, child.signal)
  const blocked = buildBlockedPayload({ browser, message, scenarioPack: `${scenario} + suffix ${suffixQuery}`, resultDir })
  writeJson(validationPath, blocked)
  return { browser, status: 'blocked', payload: blocked, stdout: child.stdout, stderr: child.stderr }
}

function maybeAppendMatrix(payload, matrixPath) {
  const row = buildRow(payload)
  const result = appendRow(matrixPath, row)
  return { row, ...result }
}

function main() {
  const root = process.cwd()
  const validatorScript = path.resolve(root, 'playground/scripts/validate_playground_playwright.cjs')
  if (!fs.existsSync(validatorScript)) {
    throw new Error(`validator script not found: ${validatorScript}`)
  }

  const browsers = parseBrowsers(process.env.PLAYGROUND_BROWSERS)
  if (browsers.length === 0) throw new Error('no browsers selected (PLAYGROUND_BROWSERS)')
  for (const browser of browsers) {
    if (!ALLOWED_BROWSERS.has(browser)) {
      throw new Error(`unsupported browser in PLAYGROUND_BROWSERS: ${browser}`)
    }
  }

  const outRoot = path.resolve(root, process.env.PLAYGROUND_BATCH_OUT_DIR || 'test-results/playground/batch')
  const summaryPath = path.resolve(root, process.env.PLAYGROUND_BATCH_SUMMARY || path.join('test-results/playground/batch', 'summary.json'))
  const matrixPath = path.resolve(root, process.env.PLAYGROUND_MATRIX_PATH || 'playground/WASM_VALIDATION_MATRIX.md')
  const scenario = process.env.PLAYGROUND_SCENARIO || 'strict_hamza_letter'
  const suffixQuery = process.env.PLAYGROUND_SUFFIX_QUERY || '*صور'
  const append = String(process.env.PLAYGROUND_BATCH_APPEND || '0') === '1'

  const baseEnv = { ...process.env }
  const results = []
  for (const browser of browsers) {
    const result = runSingleBrowser({ browser, validatorScript, baseEnv, outRoot, scenario, suffixQuery })
    if (append) {
      const appendResult = maybeAppendMatrix(result.payload, matrixPath)
      result.append = appendResult
    }
    results.push(result)
  }

  const summary = {
    generatedAtUtc: new Date().toISOString(),
    scenario,
    suffixQuery,
    append,
    matrixPath: append ? matrixPath : null,
    browsers,
    counts: {
      pass: results.filter(result => result.status === 'pass').length,
      blocked: results.filter(result => result.status === 'blocked').length
    },
    results: results.map(result => ({
      browser: result.browser,
      status: result.status,
      validationPath: path.join(outRoot, result.browser, 'validation.json'),
      row: buildRow(result.payload),
      append: result.append || null,
      error: result.status === 'blocked' ? result.payload?.failure?.message || '' : ''
    }))
  }

  writeJson(summaryPath, summary)
  console.log(`PLAYGROUND_BATCH_SUMMARY_JSON=${JSON.stringify(summary)}`)
}

try {
  main()
} catch (error) {
  console.error(`PLAYGROUND_BATCH_ERROR=${error.message || String(error)}`)
  process.exit(1)
}
