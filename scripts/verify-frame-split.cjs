/* Verify existing native split views and focused-pane routing. */
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process');
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const root=path.resolve(__dirname,'..'),evidence=path.join(root,'verification/2026-10-05');
fs.mkdirSync(evidence,{recursive:true});
const profile=fs.mkdtempSync(path.join(root,'profiles/split-'));let context;
const ps=cmd=>cp.execFileSync('powershell.exe',['-NoProfile','-Command',cmd],{encoding:'utf8'}).trim();
const results=[];
(async()=>{
 context=await chromium.launchPersistentContext(profile,{executablePath:path.join(root,'chromium/src/out/FrameBaseline/Frame.exe'),headless:false,chromiumSandbox:true, colorScheme:null,viewport:null,args:['--no-first-run','--force-renderer-accessibility','--window-size=1280,900']});
 await context.route('https://frame-test.test/**',route=>route.fulfill({contentType:'text/html',body:`<!doctype html><title>${route.request().url()}</title><h1>${route.request().url()}</h1><input aria-label="${new URL(route.request().url()).pathname} form">`}));
 const left=context.pages()[0];await left.goto('https://frame-test.test/left');
 const spare=await context.newPage();await spare.goto('https://frame-test.test/spare');await left.bringToFront();
 const handle=require('./frame-test-tools.cjs').frameWindowHandle(profile);
 const native=name=>cp.execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(root,'scripts/frame-ui.ps1'),'-WindowHandle',handle,'-Action','invoke','-Name',name],{encoding:'utf8'});
 const focusPane=part=>{
  cp.execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(root,'scripts/frame-ui.ps1'),'-WindowHandle',handle,'-Action','focus','-Name',`${part} form`]);
 };
 native('Split current page with a new tab');
 await left.waitForTimeout(1000);
 const controls=JSON.parse(ps(`& '${path.join(root,'scripts/frame-ui.ps1')}' -WindowHandle ${handle} -Action listall | ConvertTo-Json -Compress`));
 if(process.env.FRAME_CAPTURE_PYTHON)cp.execFileSync(process.env.FRAME_CAPTURE_PYTHON,[path.join(root,'scripts/capture_frame_window.py'),handle,path.join(evidence,'split-debug.png')]);
 const option=controls.find(item=>item.X>650&&item.Name.includes('frame-test.test/spare'));
 assert(option,'Native split chooser did not expose the existing page');native(option.Name);
 const right=spare;await right.goto('https://frame-test.test/right');
 const dimensions=await Promise.all([left,right].map(page=>page.evaluate(()=>({width:innerWidth,visibility:document.visibilityState}))));
 assert(dimensions.every(item=>item.visibility==='visible'&&item.width<1000),'Two real visible panes were not created');
 results.push({name:'Native rail command creates two visible real website panes',passed:true,dimensions});
 await left.getByRole('textbox').fill('Left unsaved');focusPane('/left');native('Copy current page link');
 assert.equal(ps('Get-Clipboard'),'https://frame-test.test/left');
 await right.waitForTimeout(1100);await right.getByRole('textbox').fill('Right unsaved');focusPane('/right');native('Copy current page link');
 assert.equal(ps('Get-Clipboard'),'https://frame-test.test/right');
 assert.equal(await left.getByRole('textbox').inputValue(),'Left unsaved');
 results.push({name:'Copy Link follows focused pane; both forms retain independent state',passed:true});
 await right.close();assert.equal(await left.getByRole('textbox').inputValue(),'Left unsaved');
 results.push({name:'Closing one split pane preserves surviving real page',passed:true});
 await left.goto('chrome://settings/');
 const palette=await left.evaluate(()=>{
  const sample=document.createElement('span');sample.style.color='var(--cr-primary-text-color)';sample.style.backgroundColor='var(--cr-card-background-color)';document.body.append(sample);
  const result={dark:matchMedia('(prefers-color-scheme:dark)').matches,card:getComputedStyle(sample).backgroundColor,text:getComputedStyle(sample).color};sample.remove();return result;
 });
 results.push({name:'Internal palette diagnostic',palette});
 assert.equal(palette.dark,true);assert.equal(palette.card,'rgb(20, 38, 61)');assert.equal(palette.text,'rgb(234, 240, 250)');
 results.push({name:'Native Frame scheme, navy cards and readable foreground agree without colour emulation',passed:true,palette});
 const python=process.env.FRAME_CAPTURE_PYTHON;
 if(python)cp.execFileSync(python,[path.join(root,'scripts/capture_frame_window.py'),handle,path.join(evidence,'native-settings-final.png')]);
 fs.writeFileSync(path.join(evidence,'split.json'),JSON.stringify({profile,results},null,2));
})().catch(error=>{console.error(error);fs.writeFileSync(path.join(evidence,'split.json'),JSON.stringify({profile,results,error:String(error)},null,2));process.exitCode=1}).finally(async()=>{if(context)await context.close()});
