/* Acceptance against the actual Windows executable and native Views controls.
 * No HTML shell, injected chrome, production profile or global mouse input.
 * Run after building, once for each Windows display scale to accept.
 */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');
const http = require('node:http');
const {chromium} = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const root = path.resolve(__dirname, '..');
const evidence = path.join(root, 'verification', 'floating-dock',
    new Date().toISOString().replaceAll(':', '-'));
fs.mkdirSync(evidence, {recursive: true});
fs.mkdirSync(path.join(root, 'profiles'), {recursive: true});
const profile = fs.mkdtempSync(path.join(root, 'profiles', 'dock-'));
const results = [];
let context, handle;
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
const native = (action, name, extra = []) => cp.execFileSync('powershell.exe', [
  '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
  path.join(root, 'scripts', 'frame-ui.ps1'), '-WindowHandle', handle,
  '-Action', action, ...(name ? ['-Name', name] : []), ...extra,
], {encoding: 'utf8', windowsHide: true});
const controls = () => {
  const command = `& '${path.join(root, 'scripts/frame-ui.ps1').replaceAll("'", "''")}' -WindowHandle ${handle} -Action listall | ConvertTo-Json -Compress`;
  return [].concat(JSON.parse(cp.execFileSync('powershell.exe', [
    '-NoProfile', '-Command', command,
  ], {encoding: 'utf8', windowsHide: true})));
};
const tabs = () => controls().filter(c => c.Class === 'FrameDockTabButton');
const entry = title => tabs().find(c => c.Name.startsWith(title + '\n'));
const menu = labels => native('menu', null, ['-Value', JSON.stringify(labels)]);
async function waitFor(check, message) {
  for (let attempt = 0; attempt < 40; ++attempt) {
    if (await check()) return;
    await pause(100);
  }
  throw Error(message);
}
async function check(name, fn) {
  await fn();
  results.push({name, passed: true});
  console.log(`PASS: ${name}`);
}
function capture(name) {
  if (!process.env.FRAME_CAPTURE_PYTHON) return;
  cp.execFileSync(process.env.FRAME_CAPTURE_PYTHON, [
    path.join(root, 'scripts', 'capture_frame_window.py'), handle,
    path.join(evidence, name + '.png'),
  ], {windowsHide: true});
}
const server = http.createServer((request, response) => {
  if (request.url === '/icon.svg' || request.url === '/changed.svg') {
    response.writeHead(200, {'Content-Type': 'image/svg+xml'});
    response.end(`<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32"><rect width="32" height="32" rx="6" fill="${request.url === '/icon.svg' ? '#9f88e8' : '#63b8a4'}"/></svg>`);
    return;
  }
  if (request.url === '/favicon.ico') {
    response.writeHead(404); response.end(); return;
  }
  const title = `Dock ${request.url}`;
  response.writeHead(200, {'Content-Type': 'text/html; charset=utf-8'});
  response.end(`<!doctype html><title>${title}</title>${request.url === '/missing' ? '' : '<link rel="icon" href="/icon.svg">'}<style>body{font-family:serif;background:white;color:black}</style><input aria-label="Preserved form"><button id="edge" style="position:fixed;left:0;top:45%" onclick="this.textContent='Clicked'">Page edge</button>`);
});
(async () => {
  assert.equal(process.platform, 'win32', 'This suite requires the real Windows build');
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const origin = `http://127.0.0.1:${server.address().port}`;
  context = await chromium.launchPersistentContext(profile, {
    executablePath: path.join(root, 'chromium/src/out/FrameBaseline/Frame.exe'),
    headless: false, chromiumSandbox: true, colorScheme: null, viewport: null,
    args: ['--no-first-run', '--force-renderer-accessibility', '--window-size=1280,900'],
  });
  handle = require('./frame-test-tools.cjs').frameWindowHandle(profile);
  const first = context.pages()[0];
  await first.goto(origin + '/first');
  await first.getByRole('textbox').fill('Keep this unsaved');
  const duplicate = await context.newPage(); await duplicate.goto(origin + '/duplicate');
  const missing = await context.newPage(); await missing.goto(origin.replace('127.0.0.1', 'localhost') + '/missing');
  await check('Real model entries activate distinct tabs with duplicate and missing favicons', async () => {
    await waitFor(() => entry('Dock /missing'), 'Native dock did not track tab creation');
    assert.equal(tabs().length, context.pages().length);
    for (const [page, title] of [[first, 'Dock /first'], [duplicate, 'Dock /duplicate'], [missing, 'Dock /missing']]) {
      const tab = entry(title); assert(tab && tab.Name.includes(new URL(page.url()).hostname));
      native('invoke', tab.Name);
      await waitFor(() => page.evaluate(() => document.visibilityState === 'visible'), 'Native activation did not select the right page');
    }
    assert.equal(await first.getByRole('textbox').inputValue(), 'Keep this unsaved');
    capture('three-tabs');
  });
  await check('Navigation and title changes update the same dock entry', async () => {
    await duplicate.goto(origin + '/renamed');
    await duplicate.evaluate(() => {
      document.title = 'Changed native title';
      document.querySelector('link[rel=icon]').href = '/changed.svg';
    });
    await waitFor(() => entry('Changed native title'), 'Native tab title did not update');
    assert(!entry('Dock /duplicate'));
    assert.equal(tabs().length, context.pages().length);
    capture('favicon-changed'); // Compare native icon visually, not through DOM.
  });
  await check('Native right-click pin preserves order and per-tab state', async () => {
    native('contextmenu', entry('Dock /missing').Name, ['-MenuName', 'Pin']);
    await waitFor(() => entry('Dock /missing')?.Name.includes('Pinned'), 'Native pin state absent');
    assert.equal(tabs()[0].Name.split('\n')[0], 'Dock /missing');
  });
  await check('Middle click closes only its target; creation uses the dock plus', async () => {
    native('middleclick', entry('Changed native title').Name);
    await waitFor(() => duplicate.isClosed(), 'Middle click did not close the target tab');
    assert(!first.isClosed() && !missing.isClosed());
    const created = context.waitForEvent('page');
    native('invoke', 'New tab (Ctrl+T)');
    const page = await created;
    await page.waitForLoadState();
    await waitFor(() => tabs().length === context.pages().length, 'Dock plus did not update model entries');
    await page.close();
  });
  await check('Overflow is bounded, active tab is revealed and page edge remains usable', async () => {
    for (let i = 0; i < 24; ++i) {
      const page = await context.newPage(); await page.goto(origin + '/overflow-' + i);
    }
    native('resize', null, ['-Width', '780', '-Height', '420']);
    await waitFor(() => entry('Dock /overflow-23')?.Offscreen === false, 'Active overflow tab is unreachable');
    let dock = controls().find(c => c.Class === 'FrameTabDockView');
    assert(dock && dock.Height > 80 && dock.Height < 420 && dock.Width >= 40);
    const active = entry('Dock /overflow-23');
    assert(active.Y >= dock.Y && active.Y + active.Height <= dock.Y + dock.Height);
    native('reveal', 'Dock /first');
    native('invoke', entry('Dock /first').Name);
    await first.locator('#edge').click();
    assert.equal(await first.locator('#edge').textContent(), 'Clicked');
    assert.equal(await first.getByRole('textbox').inputValue(), 'Keep this unsaved');
    capture('short-overflow');
    native('maximise'); await pause(300); capture('maximised');
    native('restore'); await pause(300);
    assert.equal(await first.getByRole('textbox').inputValue(), 'Keep this unsaved');
    native('resize', null, ['-Width', '1280', '-Height', '900']);
  });
  await check('Moved native Copy Link follows activation and favourites reuse exact URLs', async () => {
    native('invoke', entry('Dock /first').Name);
    menu(['More tools', 'Copy current page link']);
    const clipboard = cp.execFileSync('powershell.exe', ['-NoProfile', '-Command', 'Get-Clipboard'], {encoding: 'utf8', windowsHide: true}).trim();
    assert.equal(clipboard, origin + '/first');
    menu(['More tools', 'Frame favourites', 'Add current page to Frame favourites']);
    const count = context.pages().length;
    menu(['More tools', 'Frame favourites', '127.0.0.1']);
    assert.equal(context.pages().length, count);
    assert.equal(await first.getByRole('textbox').inputValue(), 'Keep this unsaved');
  });
  await check('NTP shortcuts have no visible captions and retain native edit controls', async () => {
    const page = await context.newPage(); await page.goto('chrome://newtab/');
    await page.locator('#frameHeading').waitFor();
    const shortcuts = page.locator('cr-most-visited');
    await shortcuts.waitFor();
    assert(await shortcuts.evaluate(el => el.hasAttribute('hide-title')));
    const titles = shortcuts.locator('.tile-title');
    for (let i = 0; i < await titles.count(); ++i) assert(!(await titles.nth(i).isVisible()));
    assert(await page.locator('ntp-searchbox').isVisible());
    assert(await page.locator('#frameHeading').isVisible());
    capture('new-tab');
  });
  results.push({name: 'Manual acceptance still required', pending: [
    'Omnibox suggestions, alias editor, copy/paste and real keyboard focus/shortcuts',
    'Hover tooltip paint and loading/audio/sleeping indicators',
    'Actual pointer dragging, snapping, caption hit areas and Windows display scales',
    'Split-pane focus, sleeping protections/session restoration and extensions',
    'Favicon image appearance and shortcut editing through the native dialog',
  ]});
})().catch(error => {
  results.push({error: String(error)}); console.error(error); process.exitCode = 1;
}).finally(async () => {
  fs.writeFileSync(path.join(evidence, 'acceptance.json'), JSON.stringify({profile, results}, null, 2));
  if (context) await context.close();
  server.close();
});
