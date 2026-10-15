/* Real native inactivity timer and domain exclusions, isolated sandboxed profile.
 * Disconnect automation from fixture tabs before waiting for renderer release.
 */
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),http=require('node:http');
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const root=path.resolve(__dirname,'..'),evidence=path.join(root,'verification/2026-10-05');
const profile=fs.mkdtempSync(path.join(root,'profiles/timeout-'));
const results=[];let browser,handle;
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const server=http.createServer((req,res)=>{res.setHeader('Content-Type','text/html');res.end(`<!doctype html><title>Timeout ${req.url}</title><p>${req.url}</p><input aria-label="Edited form"><script>window.fixtureMemory=new Uint8Array(16*1024*1024);fixtureMemory.fill(73)</script>`)});
async function inspect(endpoint){
 const targets=await (await fetch(endpoint+'/json/list')).json();
 const target=targets.find(item=>item.url==='chrome://discards/');assert(target);
 const socket=new WebSocket(target.webSocketDebuggerUrl);
 await new Promise((resolve,reject)=>{socket.onopen=resolve;socket.onerror=reject});
 const result=new Promise((resolve,reject)=>{socket.onmessage=e=>{const m=JSON.parse(e.data);if(m.id===1)m.error?reject(Error(JSON.stringify(m.error))):resolve(m.result.result.value)}});
 socket.send(JSON.stringify({id:1,method:'Runtime.evaluate',params:{expression:"import('chrome://discards/discards.js').then(m=>m.getOrCreateDetailsProvider().getTabDiscardsInfo()).then(r=>r.infos)",awaitPromise:true,returnByValue:true}}));
 try{return await result}finally{socket.close()}
}
(async()=>{
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
 const origin=`http://127.0.0.1:${server.address().port}`,excludedOrigin=`http://excluded.frame-test.test:${server.address().port}`;
 cp.spawn(path.join(root,'chromium/src/out/FrameBaseline/Frame.exe'),[`--user-data-dir=${profile}`,'--remote-debugging-port=0','--no-first-run','--force-renderer-accessibility','--window-size=1280,900','--host-resolver-rules=MAP excluded.frame-test.test 127.0.0.1'],{stdio:'ignore'});
 const portFile=path.join(profile,'DevToolsActivePort');
 for(let i=0;i<100&&!fs.existsSync(portFile);i++)await pause(100);
 assert(fs.existsSync(portFile));
 const endpoint=`http://127.0.0.1:${fs.readFileSync(portFile,'utf8').split('\n')[0]}`;
 browser=await chromium.connectOverCDP(endpoint);
 const context=browser.contexts()[0];
 handle=require('./frame-test-tools.cjs').frameWindowHandle(profile);
 const settings=await context.newPage();await settings.goto('chrome://settings/performance');
 const dropdown=settings.locator('#frameSleepTimeout select');await dropdown.waitFor();
 assert.equal(await dropdown.inputValue(),'0');await dropdown.selectOption('1');
 await settings.locator('tab-discard-exception-list #addButton').click();
 const dialog=settings.locator('tab-discard-exception-tabbed-add-dialog');
 await dialog.getByRole('tab',{name:/manually/i}).click();
 await dialog.locator('tab-discard-exception-add-input input').fill('excluded.frame-test.test');
 await dialog.locator('#actionButton').click();
 await settings.locator('tab-discard-exception-entry').filter({hasText:'excluded.frame-test.test'}).waitFor();
 const clean=await context.newPage();await clean.goto(origin+'/clean');
 const dirty=await context.newPage();await dirty.goto(origin+'/dirty');await dirty.getByRole('textbox').fill('Retain timed form');
 const excluded=await context.newPage();await excluded.goto(excludedOrigin+'/excluded');
 const inspector=await context.newPage();await inspector.goto('chrome://chrome-urls/');await inspector.getByRole('button',{name:/enable/i}).click();await inspector.goto('chrome://discards/');
 await settings.bringToFront();
 const enabled=settings.locator('settings-memory-page #toggleButton cr-toggle');
 if(!await enabled.evaluate(element=>element.checked))await enabled.click();
 const active=await context.newPage();await active.goto(origin+'/active');await active.bringToFront();
 const started=Date.now();await browser.close();browser=null;
 let states;
 for(let i=0;i<12;i++){
  await pause(10000);states=await inspect(endpoint);
  if(states.find(item=>item.tabUrl===origin+'/clean')?.state===5)break;
 }
 fs.writeFileSync(path.join(evidence,'timeout-states.json'),JSON.stringify({elapsedSeconds:(Date.now()-started)/1000,states},null,2));
 assert.equal(states.find(item=>item.tabUrl===origin+'/clean').state,5,'Configured native timeout did not discard eligible background tab');
 for(const url of [origin+'/dirty',excludedOrigin+'/excluded',origin+'/active'])assert.notEqual(states.find(item=>item.tabUrl===url).state,5,`Unsafe automatic sleep: ${url}`);
 results.push({name:'Configured one-minute native timer sleeps eligible background page while edited, excluded and active pages survive',passed:true});
 browser=await chromium.connectOverCDP(endpoint);
 const pages=browser.contexts()[0].pages();
 assert.equal(await pages.find(p=>p.url()===origin+'/dirty').getByRole('textbox').inputValue(),'Retain timed form');
 const prefsPage=pages.find(p=>p.url()==='chrome://settings/performance');
 const persisted=await prefsPage.evaluate(async()=>{
  const read=key=>new Promise(resolve=>chrome.settingsPrivate.getPref(key,resolve));
  return {timeout:await read('frame.tabs.sleep_timeout_minutes'),exclusions:await read('performance_tuning.tab_discarding.exceptions_with_time')};
 });
 assert.equal(persisted.timeout.value,1);
 fs.writeFileSync(path.join(evidence,'timeout-prefs.json'),JSON.stringify(persisted,null,2));
 results.push({name:'Actual Memory settings timer and domain exclusion controls retain native preferences and edited text',passed:true});
 await prefsPage.locator('#frameSleepTimeout select').selectOption('0');
 assert.equal(await prefsPage.locator('#frameSleepTimeout select').inputValue(),'0');
 results.push({name:'Adaptive timing remains selectable through the native settings control',passed:true});
 await prefsPage.locator('#frameSleepTimeout select').selectOption('5');
 await browser.close();browser=null;
 cp.execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(root,'scripts/frame-ui.ps1'),'-WindowHandle',handle,'-Action','close'],{encoding:'utf8'});
 await pause(1000);
 // Preserve launch flags: Playwright's default --disable-field-trial-config
 // would roll back this native profile's search-provider migration state.
 fs.unlinkSync(portFile);
 cp.spawn(path.join(root,'chromium/src/out/FrameBaseline/Frame.exe'),[`--user-data-dir=${profile}`,'--remote-debugging-port=0','--no-first-run','--force-renderer-accessibility','--window-size=1280,900','--host-resolver-rules=MAP excluded.frame-test.test 127.0.0.1'],{stdio:'ignore'});
 for(let i=0;i<100&&!fs.existsSync(portFile);i++)await pause(100);
 assert(fs.existsSync(portFile));
 browser=await chromium.connectOverCDP(`http://127.0.0.1:${fs.readFileSync(portFile,'utf8').split('\n')[0]}`);
 handle=require('./frame-test-tools.cjs').frameWindowHandle(profile);
 const restored=await browser.contexts()[0].newPage();await restored.goto('chrome://settings/performance');
 assert.equal(await restored.locator('#frameSleepTimeout select').inputValue(),'5');
 await restored.locator('tab-discard-exception-entry').filter({hasText:'excluded.frame-test.test'}).waitFor();
 results.push({name:'Timeout choice and excluded domain survive actual browser restart',passed:true});
})().catch(error=>{console.error(error);process.exitCode=1;results.push({error:String(error),passed:false})}).finally(async()=>{
 if(browser)await browser.close();
 if(handle)try{cp.execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(root,'scripts/frame-ui.ps1'),'-WindowHandle',handle,'-Action','close'],{encoding:'utf8'})}catch{}
 server.close();fs.writeFileSync(path.join(evidence,'timeout.json'),JSON.stringify({profile,results},null,2));
});
