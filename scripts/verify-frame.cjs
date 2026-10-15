/* Exercise the actual Windows Frame executable using an isolated profile.
 * Set PLAYWRIGHT_MODULE to your installed playwright module when necessary.
 * Native rail actions use Windows UI Automation; no production profile edits.
 */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');
const {chromium} = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const root = path.resolve(__dirname, '..');
const evidence = path.join(root, 'verification', '2026-10-05');
fs.mkdirSync(path.join(root, 'profiles'), {recursive:true});
const profile = fs.mkdtempSync(path.join(root, 'profiles', 'acceptance-'));
fs.mkdirSync(evidence, {recursive:true});
const results = [];
let context;
async function check(name, fn) {
  await fn();
  results.push({name, passed:true});
  console.log(`PASS: ${name}`);
}
const ps = (args) => cp.execFileSync('powershell.exe', ['-NoProfile','-ExecutionPolicy','Bypass',...args], {encoding:'utf8'}).trim();
function native(name) {
  const handle = require('./frame-test-tools.cjs').frameWindowHandle(profile);
  assert(handle && handle !== '0', 'Native Frame test window missing');
  ps(['-File',path.join(root,'scripts/frame-ui.ps1'),'-WindowHandle',handle,'-Action','invoke','-Name',name]);
}
async function launch() {
  context = await chromium.launchPersistentContext(profile, {
    executablePath:path.join(root,'chromium/src/out/FrameBaseline/Frame.exe'),
    headless:false, chromiumSandbox:true, colorScheme:null, viewport:null,
    args:['--no-first-run','--force-renderer-accessibility','--window-size=1280,900'],
  });
  await context.route(/^https:\/\/(github\.com|en\.wikipedia\.org|www\.youtube\.com|www\.google\.com|frame-test\.test)\//,
    route => route.fulfill({contentType:'text/html', body:'<!doctype html><title>Frame test Ελληνικά 日本語</title><h1>Website typography</h1><input aria-label="Website form">'}));
  return context.pages()[0];
}
async function ntp(page) {
  await page.goto('chrome://newtab/');
  await page.locator('#frameHeading').waitFor();
  return page.locator('ntp-searchbox cr-searchbox-input').getByRole('combobox');
}
async function settings(page) {
  await page.goto('chrome://settings/search');
  await page.locator('settings-search-page').waitFor();
}
async function pref(page,key) {
  return page.evaluate(key => new Promise(resolve => chrome.settingsPrivate.getPref(key,resolve)),key);
}
(async()=>{
  const errors=[];
  let page=await launch();
  context.on('page',p=>p.on('pageerror',e=>errors.push(String(e))));
  page.on('pageerror',e=>errors.push(String(e)));
  await check('Ordinary typing focuses NTP search without losing characters',async()=>{
    const input=await ntp(page);
    await page.locator('body').click({position:{x:30,y:550}});
    await page.keyboard.type('hello frame');
    assert.equal(await input.inputValue(),'hello frame');
  });
  for(const [text,host,key,value] of [
    ['gh frame browser','github.com','q','frame browser'],
    ['!wiki frame & browser','en.wikipedia.org','search','frame & browser'],
    ['yt frame browser','www.youtube.com','search_query','frame browser'],
    ['!gh café 日本語','github.com','q','café 日本語'],
  ]) await check(`Pasted quick search: ${text}`,async()=>{
    const input=await ntp(page); await input.fill(text);
    // Wait for native asynchronous autocomplete before activating its match.
    await page.waitForFunction(host=>{
      const result=document.querySelector('ntp-app').shadowRoot.querySelector('ntp-searchbox').result;
      return result?.matches?.[0]?.destinationUrl?.includes(host);
    },host);
    await page.keyboard.press('Enter');
    await page.waitForURL(url=>url.hostname===host);
    assert.equal(new URL(page.url()).searchParams.get(key),value);
  });
  await check('Explicit URLs retain query and fragment; websites retain fonts',async()=>{
    const input=await ntp(page); await input.fill('https://frame-test.test/alpha?q=one#keep');
    await page.waitForFunction(()=>document.querySelector('ntp-app').shadowRoot.querySelector('ntp-searchbox').result?.matches?.[0]?.destinationUrl?.includes('frame-test.test'));
    await page.keyboard.press('Enter'); await page.waitForURL(/frame-test.test/);
    assert.equal(page.url(),'https://frame-test.test/alpha?q=one#keep');
    assert.match(await page.locator('h1').evaluate(e=>getComputedStyle(e).fontFamily),/Times/);
    await page.locator('input').fill('unsaved test text');
  });
  await check('Native favourites add and manage using the bookmark model',async()=>{
    native('Add current page to rail favourites');
    native('Edit, reorder or remove rail favourites');
    await page.waitForTimeout(150);
    const manager=context.pages().find(p=>p.url().startsWith('chrome://bookmarks'));
    assert(manager);
    await manager.waitForFunction(()=>!!window.chrome?.bookmarks);
    const found=await manager.evaluate(()=>new Promise(resolve=>chrome.bookmarks.getTree(nodes=>{
      const visit=ns=>ns.flatMap(n=>[...(n.title==='Frame favourites'?[n]:[]),...visit(n.children||[])]);
      resolve(visit(nodes));
    })));
    assert.equal(found.length,1); assert.equal(found[0].children[0].url,'https://frame-test.test/alpha?q=one#keep');
    // Setup uses the privileged native model; actual activation uses the rail.
    const renamed='Frame test favourite';
    await manager.evaluate(({id,title})=>new Promise(resolve=>chrome.bookmarks.update(id,{title},resolve)),{id:found[0].children[0].id,title:renamed});
    await page.waitForTimeout(150);
    const count=context.pages().length;
    native(`${renamed}\nhttps://frame-test.test/alpha?q=one#keep\nCtrl-click to open a duplicate`);
    assert.equal(context.pages().length,count);
    assert.equal(await page.locator('input').inputValue(),'unsaved test text');
    await manager.screenshot({path:path.join(evidence,'favourites-manager.png')});
  });
  await check('Copy Link copies authoritative current URL via native toolbar',async()=>{
    native('Copy current page link');
    assert.equal(ps(['-Command','Get-Clipboard']),'https://frame-test.test/alpha?q=one#keep');
  });
  await check('Native utilities reuse tabs on repeated activation',async()=>{
    for(const name of ['History (Ctrl+H)','Downloads (Ctrl+J)','Extensions','Settings and appearance','Bookmarks (Ctrl+Shift+O)']){
      native(name); await page.waitForTimeout(100); const count=context.pages().length;
      native(name); await page.waitForTimeout(100); assert.equal(context.pages().length,count,name);
    }
  });
  await check('Search preferences are discoverable and persistent',async()=>{
    await settings(page);
    assert.equal((await pref(page,'frame.search.empty_remote_suggestions')).value,false);
    const toggle=page.locator('settings-toggle-button[pref-key="frame.search.empty_remote_suggestions"]');
    await toggle.locator('cr-toggle').click();
    assert.equal((await pref(page,'frame.search.empty_remote_suggestions')).value,true);
    await toggle.locator('cr-toggle').click();
    assert.equal((await pref(page,'frame.search.empty_remote_suggestions')).value,false);
    await page.screenshot({path:path.join(evidence,'settings-search.png')});
    await context.close(); page=await launch(); await settings(page);
    assert.equal((await pref(page,'frame.search.empty_remote_suggestions')).value,false);
  });
  await check('Five utilities render navy backgrounds and bundled Geist offline',async()=>{
    for(const url of ['settings','extensions','history','downloads','bookmarks']){
      await page.goto(`chrome://${url}`);
      await page.evaluate(()=>document.fonts.load('14px Geist'));
      const style=await page.evaluate(()=>({background:getComputedStyle(document.documentElement).backgroundColor,font:getComputedStyle(document.body).fontFamily,dark:matchMedia('(prefers-color-scheme: dark)').matches}));
      assert.equal(style.background,'rgb(11, 23, 41)',url);
      assert.match(style.font,/Geist/,url);
      assert.equal(style.dark,true,`${url}: WebUI preferred scheme disagrees with dark shell`);
      await page.screenshot({path:path.join(evidence,`${url}.png`)});
    }
  });
  await check('Back/Forward and reload preserve real page navigation',async()=>{
    await page.goto('https://frame-test.test/one'); await page.goto('https://frame-test.test/two');
    await page.goBack(); assert.equal(new URL(page.url()).pathname,'/one');
    await page.goForward(); assert.equal(new URL(page.url()).pathname,'/two');
    await page.reload(); assert.equal(new URL(page.url()).pathname,'/two');
  });
  assert.deepEqual(errors,[],'WebUI JavaScript errors');
  fs.writeFileSync(path.join(evidence,'acceptance.json'),JSON.stringify({profile,results,errors},null,2));
})().catch(error=>{
  console.error(error);
  fs.writeFileSync(path.join(evidence,'acceptance.json'),JSON.stringify({profile,results,error:String(error),urls:context?.pages().map(p=>p.url())},null,2));
  process.exitCode=1;
}).finally(async()=>{if(context)await context.close();});
