/* Native explicit archive recovery actions; remote archive responses are fixtures. */
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process');
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const root=path.resolve(__dirname,'..'),evidence=path.join(root,'verification/2026-10-05');
const profile=fs.mkdtempSync(path.join(root,'profiles/archive-')),results=[];let context;
(async()=>{
 context=await chromium.launchPersistentContext(profile,{executablePath:path.join(root,'chromium/src/out/FrameBaseline/Frame.exe'),headless:false,chromiumSandbox:true,colorScheme:null,viewport:null,args:['--no-first-run','--force-renderer-accessibility','--window-size=1280,900']});
 const requests=[];
 await context.route('https://web.archive.org/**',route=>{requests.push(route.request().url());return route.fulfill({contentType:'text/html',body:'<!doctype html><title>Archive response fixture</title>No archived copy fixture'})});
 await context.route('https://frame-test.test/**',route=>route.request().url().includes('/network-failure')?route.abort('connectionrefused'):route.fulfill({status:route.request().url().includes('/gone')?410:404,contentType:'text/html',body:'<!doctype html><title>Site error content</title><p id="error" style="margin:80px">Original site error content</p><input aria-label="Retained form"><a id="link" href="https://frame-test.test/article?x=1&amp;y=2#chapter">Article</a><a id="unsupported" href="mailto:test@example.test">Email</a>'}));
 const page=context.pages()[0],handle=require('./frame-test-tools.cjs').frameWindowHandle(profile);
 const native=(action,name)=>new Promise((resolve,reject)=>cp.execFile('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(root,'scripts/frame-ui.ps1'),'-WindowHandle',handle,'-Action',action,...(name?['-Name',name]:[])],{encoding:'utf8',windowsHide:true},(error,stdout)=>error?reject(error):resolve(stdout)));
 const controls=()=>[].concat(JSON.parse(cp.execFileSync('powershell.exe',['-NoProfile','-Command',`& '${path.join(root,'scripts/frame-ui.ps1')}' -WindowHandle ${handle} -Action listall | ConvertTo-Json -Compress`],{encoding:'utf8',windowsHide:true})));
 async function activate(name,original){
  await page.waitForTimeout(150);assert(controls().some(item=>item.Name===name&&item.Enabled));
  const newPage=context.waitForEvent('page');await native('invoke',name);
  const archived=await newPage;await archived.waitForLoadState();
  // The initial browser-owned navigation can reach Wayback before attachment,
  // which may normalise '*' to its current calendar-year selector.
  assert(archived.url().startsWith('https://web.archive.org/web/'));
  assert(archived.url().endsWith('/'+original),archived.url());
  await archived.close();await page.bringToFront();
 }
 for(const pathname of ['/missing?q=a%26b#part','/gone']){
  const original='https://frame-test.test'+pathname;await page.goto(original);
  await page.getByRole('textbox').fill('Preserve error context');
  assert(!context.pages().some(item=>item.url().startsWith('https://web.archive.org/')));
  await page.locator('#error').click({button:'right'});
  await activate('View archived version of page',original);
  assert.equal(page.url(),original);assert.equal(await page.getByRole('textbox').inputValue(),'Preserve error context');
  results.push({name:`Native archive page action on ${pathname} retains original site content, URL and edited form`,archiveAction:true,passed:true});
 }
 await page.locator('#link').click({button:'right'});
 await activate('View archived version of link','https://frame-test.test/article?x=1&y=2#chapter');
 results.push({name:'Native link action explicitly opens the linked original with query and fragment',archiveAction:true,passed:true});
 await page.locator('#unsupported').click({button:'right'});await page.waitForTimeout(150);
 assert(!controls().some(item=>item.Name.startsWith('View archived version')));await native('cancelmenu');
 results.push({name:'Unsupported link scheme exposes no archive action',passed:true});
 const failed='https://frame-test.test/network-failure';await page.goto(failed).catch(()=>{});
 await page.waitForTimeout(500);
 const originalErrorDocument=page.url();
 await page.mouse.click(1050,600,{button:'right'});
 await page.waitForTimeout(200);
 fs.writeFileSync(path.join(evidence,'archive-failure-menu.json'),JSON.stringify({url:page.url(),document:await page.evaluate(()=>document.documentURI),controls:controls()},null,2));
 await activate('View archived version of page',failed);
 assert.equal(page.url(),originalErrorDocument);
 await native('invoke','Copy current page link');
 assert.equal(cp.execFileSync('powershell.exe',['-NoProfile','-Command','Get-Clipboard'],{encoding:'utf8',windowsHide:true}).trim(),failed);
 results.push({name:'Native failed-navigation context action preserves original failure URL',archiveAction:true,passed:true});
 console.log(`${results.filter(item=>item.passed).length} archive checks passed`);
})().catch(error=>{console.error(error);process.exitCode=1;results.push({error:String(error),passed:false})}).finally(async()=>{
 if(context)await context.close();fs.writeFileSync(path.join(evidence,'archive.json'),JSON.stringify({profile,results,limitation:'Browser-owned initial navigation may precede route attachment and reach Wayback. Later archive responses use a fixture. Checks cover explicit action and context preservation, not remote snapshot availability.'},null,2));
});
