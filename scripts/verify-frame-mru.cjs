/* Native Ctrl+Tab acceptance; real Windows keys target only this Frame process. */
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process');
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const root=path.resolve(__dirname,'..'),evidence=path.join(root,'verification/2026-10-05');
fs.mkdirSync(evidence,{recursive:true});
const profile=fs.mkdtempSync(path.join(root,'profiles/mru-'));let context,handle;
const results=[];
const ps=cmd=>cp.execFileSync('powershell.exe',['-NoProfile','-Command',cmd],{encoding:'utf8'}).trim();
const ui=(action,name,window=handle)=>cp.execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(root,'scripts/frame-ui.ps1'),'-WindowHandle',window,'-Action',action,...(name?['-Name',name]:[])],{encoding:'utf8'});
const key=(name,activate=false)=>cp.execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(root,'scripts/frame-test-key.ps1'),'-WindowHandle',handle,'-Key',name,...(activate?['-Activate']:[])],{encoding:'utf8'});
const popup=()=>{
 const windows=JSON.parse(ps(`& '${path.join(root,'scripts/frame-ui.ps1')}' -WindowHandle ${handle} -Action windows | ConvertTo-Json -Compress`));
 const own=[].concat(windows).find(item=>item.Name.includes('Recent tabs'));
 if(own)return own;
 const other=[].concat(windows).filter(item=>String(item.Handle)!==String(handle)&&item.Width>200&&item.Width<650&&item.Height>250);
 const buttons=JSON.parse(ps(`& '${path.join(root,'scripts/frame-ui.ps1')}' -WindowHandle ${handle} -Action list | ConvertTo-Json -Compress`));
 fs.writeFileSync(path.join(evidence,'mru-popup-debug.json'),JSON.stringify({windows,buttons},null,2));
 for(const candidate of other){
  try{
   const ownedButtons=JSON.parse(ps(`& '${path.join(root,'scripts/frame-ui.ps1')}' -WindowHandle ${candidate.Handle} -Action list | ConvertTo-Json -Compress`));
   if(ownedButtons.some(item=>item.Name.startsWith('Selected: MRU')))return candidate;
  }catch{}
 }
 return buttons.some(item=>item.Name.startsWith('Selected: MRU'))?{Handle:handle,inline:true,CaptureHandle:other[0]?.Handle}:null;
};
const activeTitle=()=>ps(`(Get-Process Frame | Where-Object MainWindowHandle -eq ${handle}).MainWindowTitle`);
const waitPopup=async page=>{
 const deadline=Date.now()+5000;
 do{const found=popup();if(found)return found;await page.waitForTimeout(100)}while(Date.now()<deadline);
 return null;
};
(async()=>{
 context=await chromium.launchPersistentContext(profile,{executablePath:path.join(root,'chromium/src/out/FrameBaseline/Frame.exe'),headless:false,chromiumSandbox:true, colorScheme:null,viewport:null,args:['--no-first-run','--force-renderer-accessibility','--window-size=1280,900']});
 await context.route('https://frame-test.test/**',route=>route.fulfill({contentType:'text/html',body:`<!doctype html><title>MRU ${new URL(route.request().url()).pathname.slice(1)}</title><input aria-label="Form">`}));
 const a=context.pages()[0];await a.goto('https://frame-test.test/A');
 const b=await context.newPage();await b.goto('https://frame-test.test/B');
 const c=await context.newPage();await c.goto('https://frame-test.test/C');
 await a.bringToFront();await c.bringToFront();await b.bringToFront();
 handle=require('./frame-test-tools.cjs').frameWindowHandle(profile);
 if(process.env.FRAME_MRU_UI_ONLY==='1') {
  ui('invoke','Switch recent tabs (Ctrl+Tab)');await b.waitForTimeout(300);
  let bubble=await waitPopup(b);assert(bubble,'Native rail did not open recent-tab bubble');
  let controls=JSON.parse(ps(`& '${path.join(root,'scripts/frame-ui.ps1')}' -WindowHandle ${bubble.Handle} -Action list | ConvertTo-Json -Compress`));
  assert(controls.some(item=>item.Name.startsWith('Selected: MRU C')),'MRU ordering disagrees with native recency');
  if(process.env.FRAME_CAPTURE_PYTHON)cp.execFileSync(process.env.FRAME_CAPTURE_PYTHON,[path.join(root,'scripts/capture_frame_window.py'),String(bubble.CaptureHandle||bubble.Handle),path.join(evidence,'mru-popup.png')]);
  if(bubble.inline)cp.execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(root,'scripts/frame-ui.ps1'),'-WindowHandle',handle,'-Action','invoke','-Name','Close','-MinY','90']);
  else ui('invoke','Close',String(bubble.Handle));
  await b.waitForTimeout(200);assert(activeTitle().startsWith('MRU B'));
  ui('invoke','Switch recent tabs (Ctrl+Tab)');bubble=await waitPopup(b);assert(bubble);
  controls=JSON.parse(ps(`& '${path.join(root,'scripts/frame-ui.ps1')}' -WindowHandle ${bubble.Handle} -Action list | ConvertTo-Json -Compress`));
  const older=controls.find(item=>item.Name.startsWith('MRU A'));assert(older);
  ui('invoke',older.Name,String(bubble.Handle));await a.waitForTimeout(200);assert(activeTitle().startsWith('MRU A'));
  assert.equal(context.pages().length,3);
  results.push({name:'Native recent-tab ordering, cancellation and mouse activation without extra pages',passed:true});
  fs.writeFileSync(path.join(evidence,'mru-ui.json'),JSON.stringify({profile,results,limitation:'Real Ctrl+Tab/release/Escape sequence is a separate acceptance test.'},null,2));
  return;
 }
 key('CtrlDown',true);key('Tab');
 let bubble=await waitPopup(b);assert(bubble,'Real Ctrl+Tab did not open native recent-tab bubble');
 let controls=JSON.parse(ps(`& '${path.join(root,'scripts/frame-ui.ps1')}' -WindowHandle ${bubble.Handle} -Action list | ConvertTo-Json -Compress`));
 assert(controls.some(item=>item.Name.startsWith('Selected: MRU C')),'Initial Ctrl+Tab did not select previous MRU C');
 if(process.env.FRAME_CAPTURE_PYTHON)cp.execFileSync(process.env.FRAME_CAPTURE_PYTHON,[path.join(root,'scripts/capture_frame_window.py'),String(bubble.Handle),path.join(evidence,'mru-popup.png')]);
 key('Escape');key('CtrlUp');assert(activeTitle().startsWith('MRU B'),'Escape changed active tab');
 results.push({name:'Native Ctrl+Tab selects MRU and Escape cancels without activation',passed:true});
 key('CtrlDown',true);key('Tab');key('CtrlUp');
 await c.waitForTimeout(300);assert(activeTitle().startsWith('MRU C'),'Ctrl release did not commit previous MRU');
 results.push({name:'Releasing Ctrl commits selected native tab',passed:true});
 key('CtrlDown',true);key('ShiftDown');key('Tab');
 bubble=await waitPopup(c);assert(bubble,'Reverse switcher did not open');
 key('ShiftUp');key('Enter');key('CtrlUp');
 assert(activeTitle().startsWith('MRU A'),'Reverse cycling/Enter did not commit the older MRU tab');
 results.push({name:'Ctrl+Shift+Tab reverses; Enter commits',passed:true});
 assert.equal(context.pages().length,3,'Switcher created extra renderers/pages');
 fs.writeFileSync(path.join(evidence,'mru.json'),JSON.stringify({profile,results},null,2));
})().catch(error=>{console.error(error);fs.writeFileSync(path.join(evidence,'mru.json'),JSON.stringify({profile,results,error:String(error)},null,2));process.exitCode=1}).finally(async()=>{
 if(handle){try{key('ShiftUp');key('CtrlUp')}catch{}}
 if(context)await context.close();
});
