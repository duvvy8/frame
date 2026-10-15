/* Real native context-menu conversions; isolated sandboxed profile. */
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process');
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const root=path.resolve(__dirname,'..'),evidence=path.join(root,'verification/2026-10-05');
fs.mkdirSync(evidence,{recursive:true});
const profile=fs.mkdtempSync(path.join(root,'profiles/conversions-'));
const results=[];let context;
const ps=cmd=>cp.execFileSync('powershell.exe',['-NoProfile','-Command',cmd],{encoding:'utf8'}).trim();
(async()=>{
 context=await chromium.launchPersistentContext(profile,{executablePath:path.join(root,'chromium/src/out/FrameBaseline/Frame.exe'),headless:false,chromiumSandbox:true,colorScheme:null,viewport:null,args:['--no-first-run','--force-renderer-accessibility','--window-size=1280,900']});
 const page=context.pages()[0];
 await context.route('https://frame-test.test/**',route=>route.fulfill({contentType:'text/html',body:'<!doctype html><title>Native measurement tests</title><p id="value" style="display:inline-block;font-size:24px;margin:70px">1 mi</p><input aria-label="Unsaved form">'}));
 await page.goto('https://frame-test.test/measurements');
 await page.getByRole('textbox').fill('Keep this form');
 const handle=require('./frame-test-tools.cjs').frameWindowHandle(profile);
 const native=name=>cp.execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(root,'scripts/frame-ui.ps1'),'-WindowHandle',handle,'-Action','invoke','-Name',name],{encoding:'utf8'});
 const controls=()=>JSON.parse(ps(`& '${path.join(root,'scripts/frame-ui.ps1')}' -WindowHandle ${handle} -Action listall | ConvertTo-Json -Compress`));
 const settings=await context.newPage();await settings.goto('chrome://settings/appearance');
 await settings.waitForFunction(()=>!!window.chrome?.settingsPrivate);
 const enabled=settings.locator('settings-toggle-button[pref-key="frame.selection.conversions"] cr-toggle');
 const metric=settings.locator('settings-toggle-button[pref-key="frame.selection.metric"] cr-toggle');
 assert.equal(await enabled.evaluate(element=>element.checked),true);
 async function measurement(text,expected){
  await page.bringToFront();
  await page.evaluate(value=>{const node=document.querySelector('#value');node.textContent=value;const range=document.createRange();range.selectNodeContents(node);getSelection().removeAllRanges();getSelection().addRange(range)},text);
  await page.locator('#value').click({button:'right'});
  await page.waitForTimeout(200);
  const items=controls(),conversion=items.find(item=>item.Name.startsWith('Copy conversion: '));
  fs.writeFileSync(path.join(evidence,'conversion-last-menu.json'),JSON.stringify({text,expected,selection:await page.evaluate(()=>getSelection().toString()),items},null,2));
  if(expected){
   assert(conversion,`Missing conversion for ${text}`);
   assert.equal(conversion.Name,`Copy conversion: ${expected}`);
   if(text==='1 mi'&&process.env.FRAME_CAPTURE_PYTHON){
    const windows=[].concat(JSON.parse(ps(`& '${path.join(root,'scripts/frame-ui.ps1')}' -WindowHandle ${handle} -Action windows | ConvertTo-Json -Compress`)));
    const menu=windows.find(item=>String(item.Handle)!==handle&&item.Width>150&&item.Width<700&&item.Height>100);
    if(menu)cp.execFileSync(process.env.FRAME_CAPTURE_PYTHON,[path.join(root,'scripts/capture_frame_window.py'),String(menu.Handle),path.join(evidence,'native-conversion-menu.png')]);
   }
   native(conversion.Name);assert.equal(ps('Get-Clipboard'),expected);
  }else{
   assert.equal(conversion,undefined,`Unexpected conversion for ${text}`);
   const copy=items.find(item=>item.Type==='ControlType.MenuItem'&&/^Copy(?:\s|$)/.test(item.Name));
   fs.writeFileSync(path.join(evidence,'conversion-menu-controls.json'),JSON.stringify(items,null,2));
   assert(copy,'Native selection Copy command missing');native(copy.Name);
  }
  assert.equal(await page.getByRole('textbox').inputValue(),'Keep this form');
  results.push({text,expected:expected||null,passed:true});
  await page.waitForTimeout(250); // Native menu widget closes asynchronously.
 }
 await measurement('1 mi','1.60934 km');
 await measurement('32°F','0 °C');
 await measurement('1 lb','453.592 g');
 await measurement('1 US gallon','3.78541 L');
 await measurement('1 UK gallon','4.54609 L');
 await measurement('1e309 m',null);
 await measurement('2 gallons',null);
 await measurement('Meet at 10 PST',null);
 await measurement('The distance is 2 mi',null);
 await measurement('1,000 mi',null);
 await measurement('-500°F',null);
 await settings.bringToFront();await metric.click();
 await measurement('1 km','3280.84 ft');
 await measurement('0°C','32 °F');
 await measurement('1 L','33.814 US fl oz');
 await settings.bringToFront();await enabled.click();
 await measurement('1 mi',null);
 await context.close();
 context=await chromium.launchPersistentContext(profile,{executablePath:path.join(root,'chromium/src/out/FrameBaseline/Frame.exe'),headless:false,chromiumSandbox:true,colorScheme:null,viewport:null,args:['--no-first-run']});
 const after=context.pages()[0];await after.goto('chrome://settings/appearance');
 await after.waitForFunction(()=>!!window.chrome?.settingsPrivate);
 const prefs=await after.evaluate(()=>new Promise(resolve=>chrome.settingsPrivate.getAllPrefs(resolve)));
 assert.equal(prefs.find(pref=>pref.key==='frame.selection.conversions').value,false);
 assert.equal(prefs.find(pref=>pref.key==='frame.selection.metric').value,false);
 results.push({name:'Both discoverable conversion settings persist through restart',passed:true});
 fs.writeFileSync(path.join(evidence,'conversions.json'),JSON.stringify({profile,results},null,2));
 console.log(`PASS: ${results.length} native conversion and preference checks`);
})().catch(error=>{console.error(error);fs.writeFileSync(path.join(evidence,'conversions.json'),JSON.stringify({profile,results,error:String(error)},null,2));process.exitCode=1}).finally(async()=>{if(context)await context.close()});
