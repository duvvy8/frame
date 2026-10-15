/* Record real native-window utility transitions in an isolated sandboxed run. */
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process');
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const root=path.resolve(__dirname,'..'),evidence=path.join(root,'verification/2026-10-05');
const profile=fs.mkdtempSync(path.join(root,'profiles/transitions-'));
const python=process.env.FRAME_CAPTURE_PYTHON;
if(!python)throw Error('Set FRAME_CAPTURE_PYTHON to a Python runtime with Pillow and PyAV');
const ps=cmd=>cp.execFileSync('powershell.exe',['-NoProfile','-Command',cmd],{encoding:'utf8'}).trim();
let context;
(async()=>{
 context=await chromium.launchPersistentContext(profile,{executablePath:path.join(root,'chromium/src/out/FrameBaseline/Frame.exe'),headless:false,chromiumSandbox:true, colorScheme:null,viewport:null,args:['--no-first-run','--force-renderer-accessibility','--window-size=1280,900']});
 const page=context.pages()[0];await page.goto('chrome://newtab/');
 const handle=require('./frame-test-tools.cjs').frameWindowHandle(profile);
 const child=cp.spawn(python,[path.join(root,'scripts/capture_frame_window.py'),handle,path.join(evidence,'native-transitions.mp4'),'--seconds','25','--fps','30'],{stdio:'pipe'});
 const finished=new Promise((resolve,reject)=>{let error='';child.stderr.on('data',data=>error+=data);child.on('exit',code=>code===0?resolve():reject(Error(error)));});
 const start=Date.now(),events=[];
 await page.waitForTimeout(1000);
 for(const name of ['Settings and appearance','Extensions','History (Ctrl+H)','Downloads (Ctrl+J)','Bookmarks (Ctrl+Shift+O)','New tab (Ctrl+T)','Settings and appearance','Extensions','History (Ctrl+H)','Downloads (Ctrl+J)','Bookmarks (Ctrl+Shift+O)']){
  events.push({name,seconds:(Date.now()-start)/1000});
  cp.execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(root,'scripts/frame-ui.ps1'),'-WindowHandle',handle,'-Action','invoke','-Name',name],{encoding:'utf8'});
  await page.waitForTimeout(400);
 }
 await finished;
 fs.writeFileSync(path.join(evidence,'transitions.json'),JSON.stringify({profile,events,limitations:'25 seconds of PrintWindow snapshots, nominal 30 fps. Consult timing sidecar. Does not prove every renderer/GPU/theme transition.'},null,2));
})().catch(error=>{console.error(error);process.exitCode=1}).finally(async()=>{if(context)await context.close()});
