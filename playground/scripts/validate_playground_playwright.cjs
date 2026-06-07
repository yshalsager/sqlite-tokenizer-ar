#!/usr/bin/env node

const fs = require('node:fs')
const path = require('node:path')
const { chromium, firefox, webkit } = require('playwright')

function nowUtcDate() {
  return new Date().toISOString().slice(0, 10)
}

async function waitForOkStatus(page, timeoutMs) {
  const start = Date.now()
  while (Date.now() - start < timeoutMs) {
    const status = await page.$eval('#status', el => String(el.textContent || '').trim())
    if (status.startsWith('ok:')) return status
    if (status.startsWith('error:') || status.startsWith('failed to initialize:')) {
      throw new Error(`playground status failed: ${status}`)
    }
    await page.waitForTimeout(250)
  }
  throw new Error('timed out waiting for ok status')
}

function parseJson(name, text) {
  try {
    return JSON.parse(text)
  } catch (error) {
    throw new Error(`failed to parse ${name}: ${String(error)}`)
  }
}

function requirePassBadge(label, value) {
  if (!String(value).includes('PASS')) {
    throw new Error(`${label} assertion failed: ${value}`)
  }
}

async function main() {
  const url = process.env.PLAYGROUND_URL || 'http://127.0.0.1:8090/playground/'
  const browserName = String(process.env.PLAYGROUND_BROWSER || 'chromium').toLowerCase()
  const scenario = process.env.PLAYGROUND_SCENARIO || 'strict_hamza_letter'
  const suffixQuery = process.env.PLAYGROUND_SUFFIX_QUERY || '*صور'
  const timeoutMs = Number(process.env.PLAYGROUND_TIMEOUT_MS || 180000)
  const outDir = process.env.PLAYGROUND_OUT_DIR || path.resolve(process.cwd(), 'test-results/playground')
  const screenshotPath = path.join(outDir, 'validation.png')
  const jsonPath = path.join(outDir, 'validation.json')

  fs.mkdirSync(outDir, { recursive: true })

  const browserType = browserName === 'firefox' ? firefox : browserName === 'webkit' ? webkit : chromium
  if (!['chromium', 'firefox', 'webkit'].includes(browserName)) {
    throw new Error(`unsupported PLAYGROUND_BROWSER: ${browserName}`)
  }
  const browser = await browserType.launch({ headless: true })
  const page = await browser.newPage()

  try {
    await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 120000 })
    await page.waitForSelector('#status', { timeout: 120000 })

    const initialStatus = await waitForOkStatus(page, timeoutMs)

    await page.click('#probeRun')
    await page.waitForTimeout(400)
    const probePayload = parseJson('probe JSON', await page.$eval('#probeOutput', el => String(el.textContent || '{}')))
    const probeTokenCount = Array.isArray(probePayload.tokens) ? probePayload.tokens.length : 0
    if (probeTokenCount === 0) throw new Error('tokenizer probe returned zero tokens')

    await page.selectOption('#scenarioPreset', scenario)
    await page.click('#applyScenario')
    const scenarioStatus = await waitForOkStatus(page, timeoutMs)

    const relaxedBadge = await page.$eval('#relaxedBadge', el => String(el.textContent || '').trim())
    const strictBadge = await page.$eval('#strictBadge', el => String(el.textContent || '').trim())
    const crossBadge = await page.$eval('#crossBadge', el => String(el.textContent || '').trim())
    requirePassBadge('relaxed', relaxedBadge)
    requirePassBadge('strict', strictBadge)
    requirePassBadge('cross', crossBadge)

    await page.fill('#query', suffixQuery)
    await page.click('#run')
    const suffixStatus = await waitForOkStatus(page, timeoutMs)

    const explainPayload = parseJson('explain JSON', await page.$eval('#explainOutput', el => String(el.textContent || '{}')))
    const expansions = Array.isArray(explainPayload?.relaxed?.expansions) ? explainPayload.relaxed.expansions : []
    const suffixEvent = expansions.find(event => event && event.kind === 'suffix' && Array.isArray(event.expansions))
    if (!suffixEvent || suffixEvent.expansions.length === 0) {
      throw new Error(`suffix expansion missing for query: ${suffixQuery}`)
    }

    const relaxedHits = await page.$$eval('#hitsRelaxed tbody tr', rows => rows.length)
    if (relaxedHits === 0) throw new Error(`suffix query returned zero hits: ${suffixQuery}`)

    const runtimeInfo = explainPayload.runtimeInfo || {}
    const sqliteVersion = String(runtimeInfo.sqliteVersion || '')
    const sqliteSourceId = String(runtimeInfo.sqliteSourceId || '')
    const hasFts5Option = Boolean(runtimeInfo.hasFts5Option)
    if (!sqliteVersion || !sqliteSourceId) throw new Error('missing sqlite runtime info in explain output')
    if (!hasFts5Option) throw new Error('ENABLE_FTS5 compile option not detected')

    const browserVersion = browser.version()
    const userAgent = await page.evaluate(() => navigator.userAgent)

    await page.screenshot({ path: screenshotPath, fullPage: true })

    const sourcePrefix = `${sqliteSourceId.split(' ')[0]} ${(sqliteSourceId.split(' ')[2] || '').slice(0, 12)}`.trim()

    const result = {
      validatedAtUtc: new Date().toISOString(),
      dateUtc: nowUtcDate(),
      url,
      scenario,
      suffixQuery,
      initialStatus,
      scenarioStatus,
      suffixStatus,
      badges: { relaxedBadge, strictBadge, crossBadge },
      probe: {
        tokenizerDirective: probePayload.tokenizerDirective,
        tokenCount: probeTokenCount,
        strictSensitive: Boolean(probePayload?.udf?.strictSensitive)
      },
      suffix: {
        expansionCount: suffixEvent.expansions.length,
        relaxedHits
      },
      browser: {
        engine: browserName,
        version: browserVersion,
        userAgent
      },
      runtimeInfo: {
        sqliteVersion,
        sqliteSourceId,
        sqliteSourcePrefix: sourcePrefix,
        hasFts5Option
      },
      matrixRow: {
        dateUtc: nowUtcDate(),
        os: userAgent.includes('Linux') ? 'Linux' : 'Unknown',
        browser: `Headless ${browserName[0].toUpperCase()}${browserName.slice(1)} ${browserVersion}`,
        sqliteVersion,
        sqliteSourcePrefix: sourcePrefix,
        enableFts5: 'yes',
        tokenizerUdfPreflight: 'pass',
        scenarioPack: `${scenario} + suffix ${suffixQuery}`,
        result: 'pass',
        notes: `Assertions PASS; suffix expansions=${suffixEvent.expansions.length}; hits=${relaxedHits}`
      },
      artifacts: {
        screenshotPath,
        jsonPath
      }
    }

    fs.writeFileSync(jsonPath, JSON.stringify(result, null, 2))
    console.log(`PLAYGROUND_VALIDATION_JSON=${JSON.stringify(result)}`)
  } finally {
    await browser.close()
  }
}

main().catch(error => {
  console.error(`PLAYGROUND_VALIDATION_ERROR=${error?.message || String(error)}`)
  process.exit(1)
})
