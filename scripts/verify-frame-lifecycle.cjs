/* Native sleeping acceptance, with remote inspection detached during sleep.
 * All sites and profiles are disposable local fixtures; Chromium sandbox stays on.
 */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');
const http = require('node:http');
const {chromium} = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const root = path.resolve(__dirname,'..');
const evidence = path.join(root,'verification/2026-10-05');
const profile = fs.mkdtempSync(path.join(root,'profiles/lifecycle-'));
const results=[];
let browser, processHandle, handle;
const ps = cmd=>cp.execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-Command',cmd],{encoding:'utf8',windowsHide:true}).trim();
const native = (action,name)=>cp.execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(root,'scripts/frame-ui.ps1'),'-WindowHandle',handle,'-Action',action,...(name?['-Name',name]:[])],{encoding:'utf8',windowsHide:true});
const pause = ms=>new Promise(resolve=>setTimeout(resolve,ms));
function tabAction(part,command){
  native('cancelmenu');
  const controls=JSON.parse(ps(`& '${path.join(root,'scripts/frame-ui.ps1')}' -WindowHandle ${handle} -Action listall | ConvertTo-Json -Compress`));
  const tab=controls.find(item=>item.Type==='ControlType.TabItem'&&item.Name.includes(`Lifecycle /${part}`));assert(tab);
  cp.execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(root,'scripts/frame-ui.ps1'),'-WindowHandle',handle,'-Action','contextmenu','-Name',tab.Name,'-MenuName',command],{encoding:'utf8',windowsHide:true});
}
function mruControls(window){
  return JSON.parse(ps(`& '${path.join(root,'scripts/frame-ui.ps1')}' -WindowHandle ${window} -Action list | ConvertTo-Json -Compress`));
}
async function sleepingPreview(){
  native('invoke','Switch recent tabs (Ctrl+Tab)');
  await pause(200);
  let bubble;
  for(let attempt=0;attempt<5&&!bubble;attempt++){
    const windows=[].concat(JSON.parse(ps(`& '${path.join(root,'scripts/frame-ui.ps1')}' -WindowHandle ${handle} -Action windows | ConvertTo-Json -Compress`)));
    bubble=windows.find(item=>item.Name.includes('Recent tabs'));
    if(!bubble&&mruControls(handle).some(item=>item.Name.startsWith('Selected: Lifecycle')))bubble={Handle:handle,inline:true};
    if(!bubble)await pause(100);
  }
  assert(bubble,'Native sleeping MRU popup missing');
  for(let attempt=0;attempt<10;attempt++){
    const selected=mruControls(bubble.Handle).find(item=>item.Name.startsWith('Selected: Lifecycle /clean'));
    if(selected){assert(selected.Name.includes('Sleeping'),'Discarded tab lacks native sleeping label');return {bubble,selected}};
    cp.execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(root,'scripts/frame-ui.ps1'),'-WindowHandle',String(bubble.Handle),'-Action','invoke','-Name','Preview next recent tab'],{encoding:'utf8',windowsHide:true});
    await pause(100);
  }
  throw Error('Native preview navigation did not reach sleeping clean tab');
}
async function tabSleepMenu(part){
  const controls=JSON.parse(ps(`& '${path.join(root,'scripts/frame-ui.ps1')}' -WindowHandle ${handle} -Action listall | ConvertTo-Json -Compress`));
  const tab=controls.find(item=>item.Type==='ControlType.TabItem'&&item.Name.includes(`Lifecycle /${part}`));
  assert(tab,`Native tab ${part} missing`);
  let menu=[];
  for(let attempt=0;attempt<3;attempt++){
    native('contextmenu',tab.Name);await pause(500);
    menu=JSON.parse(ps(`& '${path.join(root,'scripts/frame-ui.ps1')}' -WindowHandle ${handle} -Action listall | ConvertTo-Json -Compress`));
    if(menu.some(item=>item.Name==='Sleep tab now'))break;
    native('cancelmenu');await pause(500);
  }
  fs.writeFileSync(path.join(evidence,'tab-sleep-menu.json'),JSON.stringify({part,menu},null,2));
  const sleep=menu.find(item=>item.Name==='Sleep tab now');
  assert(sleep,'Native tab context menu has no sleep command');return sleep;
}
const server = http.createServer((request,response)=>{
  response.setHeader('Content-Type','text/html');
  response.end(`<!doctype html><title>Lifecycle ${request.url}</title><h1>${request.url}</h1><input aria-label="Unsaved form"><script>window.buffer=new Uint8Array(32*1024*1024);window.buffer.fill(31)</script>`);
});
const snapshot = page=>page.evaluate(async()=>{
  const module=await import('chrome://discards/discards.js');
  return (await module.getOrCreateDetailsProvider().getTabDiscardsInfo()).infos;
});
const resources = ()=>JSON.parse(ps('Get-Process Frame | Select-Object Id,WorkingSet64,PrivateMemorySize64,CPU | ConvertTo-Json -Compress'));
async function inspectSleeping(endpoint, expression="import('chrome://discards/discards.js').then(m=>m.getOrCreateDetailsProvider().getTabDiscardsInfo()).then(r=>r.infos)") {
  // Playwright initializes every page on reconnect, which wakes sleeping tabs.
  // Attach only to the diagnostic page to measure state before that happens.
  const targets=await (await fetch(endpoint+'/json/list')).json();
  const target=targets.find(item=>item.url==='chrome://discards/');
  const socket=new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((resolve,reject)=>{socket.onopen=resolve;socket.onerror=reject});
  const result=new Promise((resolve,reject)=>{
    socket.onmessage=event=>{
      const message=JSON.parse(event.data);
      if(message.id===1)message.error?reject(Error(JSON.stringify(message.error))):resolve(message.result.result.value);
    };
  });
  socket.send(JSON.stringify({id:1,method:'Runtime.evaluate',params:{expression,awaitPromise:true,returnByValue:true}}));
  try{return await result}finally{socket.close()}
}
(async()=>{
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const origin=`http://127.0.0.1:${server.address().port}`;
  processHandle=cp.spawn(path.join(root,'chromium/src/out/FrameBaseline/Frame.exe'),[
    `--user-data-dir=${profile}`,'--remote-debugging-port=0','--no-first-run',
    '--force-renderer-accessibility','--window-size=1280,900'],{stdio:'ignore'});
  const portFile=path.join(profile,'DevToolsActivePort');
  for(let i=0;i<100&&!fs.existsSync(portFile);i++)await pause(100);
  assert(fs.existsSync(portFile),'Remote inspection unavailable');
  const endpoint=`http://127.0.0.1:${fs.readFileSync(portFile,'utf8').split('\n')[0]}`;
  browser=await chromium.connectOverCDP(endpoint);
  handle=require('./frame-test-tools.cjs').frameWindowHandle(profile);
  const context=browser.contexts()[0];
  const clean=await context.newPage();await clean.goto(origin+'/clean');
  const dirty=await context.newPage();await dirty.goto(origin+'/dirty');
  await dirty.getByRole('textbox').fill('Unsaved form must survive');
  const protectedPage=await context.newPage();await protectedPage.goto(origin+'/protected');
  const inspector=await context.newPage();await inspector.goto('chrome://chrome-urls/');
  await inspector.getByRole('button',{name:/enable/i}).click();
  await inspector.goto('chrome://discards/');
  const info=await snapshot(inspector);
  await inspector.evaluate(async id=>{
    const module=await import('chrome://discards/discards.js');
    await module.getOrCreateDetailsProvider().setAutoDiscardable(id,false);
  },info.find(item=>item.tabUrl.endsWith('/protected')).id);
  const active=await context.newPage();await active.goto(origin+'/active');
  await active.bringToFront();
  handle=require('./frame-test-tools.cjs').frameWindowHandle(profile);
  const before=resources();
  await browser.close();browser=null; // Disconnect: leave the real process running.
  await pause(750);
  assert.equal((await tabSleepMenu('clean')).Enabled,true);
  tabAction('clean','Never sleep this tab');await pause(200);
  assert.equal((await inspectSleeping(endpoint)).find(item=>item.tabUrl===origin+'/clean').isAutoDiscardable,false);
  const cleanId=(await inspectSleeping(endpoint)).find(item=>item.tabUrl===origin+'/clean').id;
  await inspectSleeping(endpoint,`import('chrome://discards/discards.js').then(m=>m.getOrCreateDetailsProvider().setAutoDiscardable(${cleanId},true)).then(()=>true)`);
  assert.equal((await inspectSleeping(endpoint)).find(item=>item.tabUrl===origin+'/clean').isAutoDiscardable,false,'Extension/native auto-discard request overrode explicit never-sleep');
  assert.equal((await tabSleepMenu('clean')).Enabled,false);
  tabAction('clean','Never sleep this tab');await pause(200);
  assert.equal((await inspectSleeping(endpoint)).find(item=>item.tabUrl===origin+'/clean').isAutoDiscardable,true);
  for(const part of ['dirty','protected','active']){
    assert.equal((await tabSleepMenu(part)).Enabled,false,`${part} context menu permits unsafe sleeping`);
    if(part==='protected'){
      tabAction(part,'Never sleep this tab');await pause(200);
      assert.equal((await inspectSleeping(endpoint)).find(item=>item.tabUrl===origin+'/protected').isAutoDiscardable,false);
      await tabSleepMenu(part);tabAction(part,'Never sleep this tab');await pause(200);
      assert.equal((await inspectSleeping(endpoint)).find(item=>item.tabUrl===origin+'/protected').isAutoDiscardable,false,'Removing user preference removed extension protection');
    }else{native('cancelmenu');await pause(200);}
  }
  results.push({name:'Native per-tab protection uses authoritative discard state; edited, protected and active menu actions are disabled',passed:true});
  assert.equal((await tabSleepMenu('clean')).Enabled,true);tabAction('clean','Sleep tab now');await pause(500);
  assert.equal((await inspectSleeping(endpoint)).find(item=>item.tabUrl===origin+'/clean').state,5,'Native per-tab sleep did not discard the real page');
  results.push({name:'Native tab-menu sleep releases the eligible background page',passed:true});
  native('invoke','Sleep eligible background tabs');
  await pause(1500);
  const after=resources();
  const states=await inspectSleeping(endpoint);
  const sleepingState=states.find(item=>item.tabUrl===origin+'/clean');
  assert.equal(sleepingState.state,5,'Eligible clean background page did not sleep');
  let preview=await sleepingPreview();
  const previewStates=await inspectSleeping(endpoint);
  assert.equal(previewStates.find(item=>item.tabUrl===origin+'/clean').state,5,'MRU preview woke sleeping renderer');
  if(process.env.FRAME_CAPTURE_PYTHON&&!preview.bubble.inline)cp.execFileSync(process.env.FRAME_CAPTURE_PYTHON,[path.join(root,'scripts/capture_frame_window.py'),String(preview.bubble.Handle),path.join(evidence,'native-sleeping-preview.png')]);
  cp.execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(root,'scripts/frame-ui.ps1'),'-WindowHandle',String(preview.bubble.Handle),'-Action','invoke','-Name','Close',...(preview.bubble.inline?['-MinY','90']:[])],{encoding:'utf8',windowsHide:true});
  await pause(200);
  assert.equal((await inspectSleeping(endpoint)).find(item=>item.tabUrl===origin+'/clean').state,5,'Cancelling MRU woke sleeping renderer');
  results.push({name:'Native selected cached preview and cancellation preserve actual sleeping state',passed:true});
  preview=await sleepingPreview();
  cp.execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(root,'scripts/frame-ui.ps1'),'-WindowHandle',String(preview.bubble.Handle),'-Action','invoke','-Name',preview.selected.Name],{encoding:'utf8',windowsHide:true});
  await pause(750);
  const awakeStates=await inspectSleeping(endpoint);
  assert.equal(awakeStates.find(item=>item.tabUrl===origin+'/clean').state,0,'Native activation did not wake tab');
  results.push({name:'Explicit native MRU commit wakes selected sleeping tab',passed:true});
  browser=await chromium.connectOverCDP(endpoint);
  const live=browser.contexts()[0].pages();
  fs.writeFileSync(path.join(evidence,'lifecycle-states.json'),JSON.stringify({states,before,after},null,2));
  const state=name=>states.find(item=>item.tabUrl===origin+'/'+name);
  assert.equal(state('clean').state,5,'Eligible clean background page did not sleep');
  for(const name of ['dirty','protected','active'])assert.notEqual(state(name).state,5,`${name} incorrectly slept`);
  assert.equal(await live.find(page=>page.url().endsWith('/dirty')).getByRole('textbox').inputValue(),'Unsaved form must survive');
  results.push({name:'Clean background sleeps; active, dirty and extension-protected pages survive',passed:true});
  const waking=live.find(page=>page.url().endsWith('/clean'));
  await waking.bringToFront();await waking.waitForLoadState();
  assert.equal(await waking.evaluate(()=>document.wasDiscarded),true);
  results.push({name:'Sleeping tab wakes through native tab activation',passed:true});
  for(const action of ['maximise','restore','minimise','restore'])native(action);
  results.push({name:'Native maximise, restore, minimise and restore',passed:true});
  fs.writeFileSync(path.join(evidence,'lifecycle.json'),JSON.stringify({profile,results,states,before,after,measurement:'Summed process working/private bytes; shared pages may be double counted. One fixture run, not a baseline comparison.'},null,2));
})().catch(error=>{
  fs.writeFileSync(path.join(evidence,'lifecycle.json'),JSON.stringify({profile,results,error:String(error)},null,2));
  console.error(error);process.exitCode=1;
}).finally(async()=>{
  if(browser)await browser.close();
  if(handle){try{native('cancelmenu');await pause(200);native('close')}catch{}}
  server.close();
});
