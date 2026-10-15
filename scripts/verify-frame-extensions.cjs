/* Exercise the cached official uBOL MV3 package in a disposable profile. */
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process');
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const root=path.resolve(__dirname,'..'),evidence=path.join(root,'verification/2026-10-05');
fs.mkdirSync(evidence,{recursive:true});
const extension=path.join(root,'profiles/uBOL-test'),manifest=JSON.parse(fs.readFileSync(path.join(extension,'manifest.json')));
const profile=fs.mkdtempSync(path.join(root,'profiles/extensions-'));
let context;const results=[];
const ps=cmd=>cp.execFileSync('powershell.exe',['-NoProfile','-Command',cmd],{encoding:'utf8'}).trim();
const launch=()=>chromium.launchPersistentContext(profile,{executablePath:path.join(root,'chromium/src/out/FrameBaseline/Frame.exe'),headless:false,chromiumSandbox:true,colorScheme:null,viewport:null,ignoreDefaultArgs:['--disable-extensions'],args:['--no-first-run','--force-renderer-accessibility','--window-size=1280,900',`--load-extension=${extension}`]});
const info=page=>page.evaluate(()=>new Promise(resolve=>chrome.developerPrivate.getExtensionsInfo({includeDisabled:true,includeTerminated:true},resolve)));
(async()=>{
 context=await launch();const manager=context.pages()[0];await manager.goto('chrome://extensions');
 await manager.waitForFunction(()=>!!window.chrome?.developerPrivate);
 let installed;
 for(let attempt=0;attempt<50;attempt++){
  installed=(await info(manager)).find(item=>item.name==='uBlock Origin Lite');
  if(installed?.state==='ENABLED')break;await manager.waitForTimeout(200);
 }
 assert.equal(installed?.state,'ENABLED');
 results.push({name:'Official cached MV3 extension installs enabled in native manager',passed:true,id:installed.id,version:manifest.version});
 let worker=context.serviceWorkers().find(item=>item.url().includes(installed.id));
 if(!worker)worker=await context.waitForEvent('serviceworker',{predicate:item=>item.url().includes(installed.id)});
 await worker.evaluate(async()=>{for(let i=0;i<100;i++){if((await chrome.declarativeNetRequest.getEnabledRulesets()).includes('easylist'))return;await new Promise(resolve=>setTimeout(resolve,100))}throw Error('Ruleset not ready')});
 const site=await context.newPage();
 await context.route('https://frame-test.test/**',route=>route.fulfill({contentType:'text/html',body:'<!doctype html><title>Extension test</title><h1>Controlled extension fixture</h1>'}));
 const ad='https://pagead2.googlesyndication.com/pagead/js/adsbygoogle.js';
 let fixtureRequests=0;
 await context.route(ad,route=>{fixtureRequests++;return route.fulfill({contentType:'application/javascript',body:'window.fixtureAdLoaded=true;'})});
 async function probe(){
  await site.goto(`https://frame-test.test/probe?run=${Date.now()}`);
  return site.evaluate(url=>new Promise(resolve=>{const script=document.createElement('script');script.src=url;script.onload=()=>resolve({loaded:!!window.fixtureAdLoaded});script.onerror=()=>resolve({loaded:!!window.fixtureAdLoaded});document.body.append(script)}),ad);
 }
 assert.equal((await probe()).loaded,false);assert.equal(fixtureRequests,0);
 results.push({name:'Enabled native DNR rules intercept controlled advertising script before fixture response',passed:true});
 await manager.bringToFront();
 const toggle=manager.locator(`extensions-item[id="${installed.id}"] #enableToggle`);
 await toggle.click();await manager.waitForTimeout(300);
 assert.equal((await info(manager)).find(item=>item.id===installed.id).state,'DISABLED');
 assert.equal((await probe()).loaded,true);assert.equal(fixtureRequests,1);
 results.push({name:'Native manager disable changes actual blocking behaviour',passed:true});
 await manager.bringToFront();await toggle.click();await manager.waitForTimeout(700);
 assert.equal((await info(manager)).find(item=>item.id===installed.id).state,'ENABLED');
 assert.equal((await probe()).loaded,false);
 results.push({name:'Re-enabling restores native request blocking',passed:true});
 await context.close();context=await launch();
 const after=context.pages()[0];await after.goto('chrome://extensions');await after.waitForFunction(()=>!!window.chrome?.developerPrivate);
 assert.equal((await info(after)).find(item=>item.id===installed.id).state,'ENABLED');
 results.push({name:'Native extension identity and enabled state persist on restart',passed:true});
 await after.screenshot({path:path.join(evidence,'extensions-enabled.png')});
 await context.route('https://frame-test.test/**',route=>route.fulfill({contentType:'text/html',body:'<!doctype html><title>Extension popup fixture</title><h1>Current extension destination</h1>'}));
 await after.goto('https://frame-test.test/popup');
 const handle=require('./frame-test-tools.cjs').frameWindowHandle(profile);
 const native=(name)=>cp.execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(root,'scripts/frame-ui.ps1'),'-WindowHandle',handle,'-Action','invoke','-Name',name,'-MinX','650'],{encoding:'utf8'});
 const controls=()=>JSON.parse(ps(`& '${path.join(root,'scripts/frame-ui.ps1')}' -WindowHandle ${handle} -Action listall | ConvertTo-Json -Compress`));
 fs.writeFileSync(path.join(evidence,'extension-toolbar-controls.json'),JSON.stringify(controls().filter(item=>item.Name==='Extensions'),null,2));
 native('Extensions');await after.waitForTimeout(200);
 const action=controls().find(item=>item.Name.includes('uBlock Origin Lite')&&item.X>650&&(item.Type==='ControlType.Button'||item.Type==='ControlType.MenuItem'));
 assert(action,'Native extensions toolbar did not expose installed action');native(action.Name);
 await after.waitForTimeout(500);
 const popupControls=controls();
 fs.writeFileSync(path.join(evidence,'extension-popup-controls.json'),JSON.stringify(popupControls,null,2));
 assert(popupControls.some(item=>item.Name.toLowerCase()==='filtering mode'),'Real uBOL popup did not render its filtering mode');
 assert(popupControls.some(item=>item.Name.includes('frame-test.test')),'Popup did not identify the real active website');
 if(process.env.FRAME_CAPTURE_PYTHON){
  const windows=[].concat(JSON.parse(ps(`& '${path.join(root,'scripts/frame-ui.ps1')}' -WindowHandle ${handle} -Action windows | ConvertTo-Json -Compress`)));
  const popup=windows.find(item=>String(item.Handle)!==handle&&item.Width>100&&item.Width<650&&item.Height>100);
  if(popup)cp.execFileSync(process.env.FRAME_CAPTURE_PYTHON,[path.join(root,'scripts/capture_frame_window.py'),String(popup.Handle),path.join(evidence,'native-extension-popup.png')]);
 }
 results.push({name:'Native toolbar opens actual installed extension popup for active website',passed:true});
 fs.writeFileSync(path.join(evidence,'extensions.json'),JSON.stringify({profile,results,limitations:'Cached official uBOL MV3 unpacked package only; no Web Store, MV2 or permission prompt compatibility claim.'},null,2));
 console.log(`PASS: ${results.length} native MV3 extension checks`);
})().catch(error=>{console.error(error);fs.writeFileSync(path.join(evidence,'extensions.json'),JSON.stringify({profile,results,error:String(error)},null,2));process.exitCode=1}).finally(async()=>{if(context)await context.close()});
