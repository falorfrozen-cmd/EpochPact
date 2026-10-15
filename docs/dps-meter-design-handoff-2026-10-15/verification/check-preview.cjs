'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {pathToFileURL} = require('node:url');
const {chromium} = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const folder = path.resolve(__dirname, '..');
const names = ['Pact Bars', 'Quiet HUD', 'Split Meter'];

async function main() {
  const installed = process.platform === 'win32' ? [
    path.join(process.env.ProgramFiles || 'C:/Program Files', 'Google/Chrome/Application/chrome.exe'),
    path.join(process.env['ProgramFiles(x86)'] || 'C:/Program Files (x86)', 'Microsoft/Edge/Application/msedge.exe'),
  ].find(p => fs.existsSync(p)) : undefined;
  const executablePath = process.env.CHROME_PATH || installed;
  const browser = await chromium.launch({headless: true, ...(executablePath ? {executablePath} : {})});
  try {
    const page = await browser.newPage({viewport: {width: 1024, height: 950}, deviceScaleFactor: 1});
    const errors = [], remoteRequests = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.route(/^https?:\/\//, async route => { remoteRequests.push(route.request().url()); await route.abort(); });
    await page.goto(pathToFileURL(path.join(folder, 'preview.html')).href);
    await page.waitForSelector('.meter .total-number');
    const source = fs.readFileSync(path.join(folder, 'preview.html'), 'utf8');
    assert(!source.includes('window.openai') && !source.includes('globalThis.Tweak'), 'No Codex host dependency');
    assert(!source.includes('COMPACT_ATLAS_ASSET'), 'No unresolved image placeholder');

    const assetChecks = await page.evaluate(async () => {
      const root = document.querySelector('#ep-dps-options');
      const values = ['--ep-atlas', '--ep-scene'].map(key => getComputedStyle(root).getPropertyValue(key).trim());
      return Promise.all(values.map(async (value, i) => {
        const url = value.slice(4, -1).replace(/^["']|["']$/g, '');
        const image = new Image(); image.src = url; await image.decode();
        return {name: i ? 'game-context' : 'effects-atlas', width: image.naturalWidth, height: image.naturalHeight};
      }));
    });
    assert(assetChecks.every(a => a.width > 0 && a.height > 0), 'Embedded images decode');

    const choose = name => page.getByRole('button', {name, exact: true}).click();
    const geometry = [];
    for (const width of [1024, 736, 390, 320]) {
      await page.setViewportSize({width, height: 950});
      for (const [index, name] of names.entries()) {
        await choose(name);
        await page.mouse.move(width - 1, 949);
        const measured = await page.evaluate(() => {
          const sections = [...document.querySelectorAll('[data-variant]')];
          const visible = sections.filter(s => !s.hidden);
          const meter = visible[0].querySelector('.meter'), m = meter.getBoundingClientRect();
          const overflow = [], collisions = [];
          for (const element of meter.querySelectorAll('*')) {
            if (!element.getClientRects().length || element.classList.contains('source-fill')) continue;
            const r = element.getBoundingClientRect();
            if (r.left < m.left - 1 || r.right > m.right + 1) overflow.push(element.className);
          }
          for (const row of meter.querySelectorAll('.source-row,.element-row,.total,.source-summary,.column-head')) {
            const children = [...row.children].filter(e => e.getClientRects().length && !e.classList.contains('source-fill'));
            for (let i = 0; i < children.length; i++) for (let j = i + 1; j < children.length; j++) {
              const a = children[i].getBoundingClientRect(), b = children[j].getBoundingClientRect();
              if (Math.min(a.right, b.right) - Math.max(a.left, b.left) > 1 && Math.min(a.bottom, b.bottom) - Math.max(a.top, b.top) > 1) collisions.push(row.className);
            }
          }
          return {visibleDesigns: visible.length, width: m.width, height: m.height,
            types: meter.querySelectorAll('[data-type]').length, overflow, collisions,
            total: meter.querySelector('.total-number').textContent,
            documentOverflow: document.documentElement.scrollWidth > innerWidth};
        });
        assert.equal(measured.visibleDesigns, 1, 'Design switch is exclusive');
        assert.equal(measured.types, 7, 'All canonical damage types are present');
        assert.deepEqual(measured.overflow, [], `${name} at ${width}: no clipping`);
        assert.deepEqual(measured.collisions, [], `${name} at ${width}: no collisions`);
        assert(!measured.documentOverflow, `${name} at ${width}: no horizontal page overflow`);
        assert.equal(measured.total, '54,240DPS');
        geometry.push({viewport: width, name, ...measured});
        if (width === 1024) await page.screenshot({path: path.join(folder, 'screenshots', `0${index + 1}-${name.toLowerCase().replaceAll(' ', '-')}.png`), fullPage: true});
      }
    }

    await page.setViewportSize({width: 1024, height: 950});
    const interactions = [];
    for (const name of names) {
      await choose(name);
      const meter = page.locator(`[data-variant="${name}"] .meter`);
      await meter.hover();
      if (name === 'Quiet HUD') {
        await meter.locator('.ally-toggle').click();
        assert(!(await meter.locator('.rows').isHidden()));
        interactions.push({name, check: 'Expand ally roster', passed: true});
      }
      for (const source of ['skeletons', 'golem', 'archers']) {
        // Split Meter initially inspects skeletons; a second click returns to You.
        if (await meter.locator(`[data-source="${source}"]`).getAttribute('aria-pressed') === 'true') {
          await meter.locator(`[data-source="${source}"]`).click();
          assert.equal(await meter.locator(`[data-source="${source}"]`).getAttribute('aria-pressed'), 'false');
        }
        await meter.locator(`[data-source="${source}"]`).click();
        assert.equal(await meter.locator('.total-number').textContent(), '54,240DPS');
        assert.equal(await meter.locator(`[data-source="${source}"]`).getAttribute('aria-pressed'), 'true');
        interactions.push({name, check: `Inspect ${source} without changing total`, passed: true});
      }
      await meter.locator('.range-picker').selectOption('60s');
      assert.equal(await meter.locator('.total-number').textContent(), '45,562DPS');
      interactions.push({name, check: 'Change mock averaging window', passed: true});
      await meter.locator('.collapse').click();
      assert(await meter.locator('.body').isHidden());
      await meter.locator('.collapse').click();
      assert(!(await meter.locator('.body').isHidden()));
      interactions.push({name, check: 'Collapse and restore', passed: true});
      await meter.locator('.stop').click();
      assert.equal(await meter.locator('.stop').getAttribute('aria-label'), 'Resume recording');
      interactions.push({name, check: 'Stop mock recording', passed: true});
      await meter.locator('.reset').click();
      assert.equal(await meter.locator('.total-number').textContent(), '0DPS');
      interactions.push({name, check: 'Reset mock values', passed: true});
    }
    assert.equal(interactions.length, 22);
    await page.getByRole('button', {name: 'Restore samples', exact: true}).click();
    await page.waitForSelector('[data-variant="Pact Bars"] .total-number');
    assert.equal(await page.locator('[data-variant="Pact Bars"] .total-number').textContent(), '54,240DPS');
    assert.deepEqual(errors, [], 'No browser page errors');
    assert.deepEqual(remoteRequests, [], 'No network requests');

    const report = {
      passed: true, checkedAtUtc: new Date().toISOString(), scope: 'Offline fixture preview only',
      browser: browser.version(), node: process.version,
      layoutCases: geometry.length, sampleInteractionChecks: interactions.length,
      assetChecks, restoreSamplesPassed: true, remoteRequests, pageErrors: errors, geometry, interactions,
      notTested: ['Real backend renderer', 'WebView2 host controls', '260 x 120 native window', 'All user preferences and DPI scales', 'Live Last Epoch damage', 'Native/EXE build and shutdown'],
    };
    fs.writeFileSync(path.join(__dirname, 'preview-results.json'), JSON.stringify(report, null, 2) + '\n');
    console.log(JSON.stringify({passed: true, layoutCases: geometry.length, sampleInteractionChecks: interactions.length, assets: assetChecks, remoteRequests, pageErrors: errors}, null, 2));
  } finally { await browser.close(); }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
