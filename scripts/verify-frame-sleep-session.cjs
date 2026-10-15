/* Native session round trip for explicit user protection, isolated profile. */
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),http=require('node:http');
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const root=path.resolve(__dirname,'..'),profile=fs.mkdtempSync(path.join(root,'profiles/sleep-session-'));
const results=[];let browser,handle;
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const native=(action,name)=>cp.execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(root,'scripts/frame-ui.ps1'),'-WindowHandle',String(handle),'-Action',action,...(name?['-Name',name]:[])],{encoding:'utf8',windowsHide:true});
const server=http.createServer((req,res)=>{res.setHeader('Content-Type','text/html');res.end(`<!doctype html><title>Sleep session ${req.url}</title><p>${req.url}</p>`)});
async function launch(){
 const file=path.join(profile,'DevToolsActivePort');if(fs.existsSync(file))fs.unlinkSync(file);
 cp.spawn(path.join(root,'chromium/src/out/FrameBaseline/Frame.exe'),[`--user-data-dir=${profile}`,'--remote-debugging-port=0','--no-first-run','--restore-last-session','--force-renderer-accessibility','--window-size=1280,900'],{stdio:'ignore'});
 for(let i=0;i<150&&!fs.existsSync(file);i++)await pause(100);
 assert(fs.existsSync(file));browser=await chromium.connectOverCDP(`http://127.0.0.1:${fs.readFileSync(file,'utf8').split('\n')[0]}`);
 handle=require('./frame-test-tools.cjs').frameWindowHandle(profile);return browser.contexts()[0];
}
async function close(){await browser.close();browser=null;native('close');await pause(1500);}
async function toggle(part){
 const controls=JSON.parse(cp.execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-Command',`& '${path.join(root,'scripts/frame-ui.ps1')}' -WindowHandle ${handle} -Action listall | ConvertTo-Json -Compress`],{encoding:'utf8',windowsHide:true}));
 const tab=controls.find(x=>x.Type==='ControlType.TabItem'&&x.Name.includes(`Sleep session /${part}`));assert(tab);
 let found=false;
 for(let attempt=0;attempt<3;attempt++){
  native('contextmenu',tab.Name);await pause(500);
  const menu=JSON.parse(cp.execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-Command',`& '${path.join(root,'scripts/frame-ui.ps1')}' -WindowHandle ${handle} -Action listall | ConvertTo-Json -Compress`],{encoding:'utf8',windowsHide:true}));
  if(menu.some(x=>x.Name==='Never sleep this tab')){found=true;break;}
  native('cancelmenu');await pause(500);
 }
 assert(found,'Native tab context menu did not open');native('invoke','Never sleep this tab');await pause(200);
}
async function diagnostic(context){
 let page=context.pages().find(p=>p.url()==='chrome://discards/');
 if(!page){page=await context.newPage();await page.goto('chrome://chrome-urls/');await page.getByRole('button',{name:/enable/i}).click();await page.goto('chrome://discards/');}
 await page.waitForLoadState();await page.bringToFront();await pause(1000);return page;
}
const infos=page=>page.evaluate(async()=>{const m=await import('chrome://discards/discards.js');return (await m.getOrCreateDetailsProvider().getTabDiscardsInfo()).infos;});
(async()=>{
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const origin=`http://127.0.0.1:${server.address().port}`;
 let context=await launch();for(const part of ['keep','clear','extension']){const page=await context.newPage();await page.goto(origin+'/'+part);}
 let inspector=await diagnostic(context);let states=await infos(inspector);
 await inspector.evaluate(async id=>{const m=await import('chrome://discards/discards.js');await m.getOrCreateDetailsProvider().setAutoDiscardable(id,false);},states.find(x=>x.tabUrl===origin+'/extension').id);
 await toggle('keep');await toggle('clear');await toggle('clear');
 states=await infos(inspector);assert.equal(states.find(x=>x.tabUrl===origin+'/keep').isAutoDiscardable,false);assert.equal(states.find(x=>x.tabUrl===origin+'/clear').isAutoDiscardable,true);assert.equal(states.find(x=>x.tabUrl===origin+'/extension').isAutoDiscardable,false);
 results.push({name:'Explicit user protection and removal coexist with independent extension protection',passed:true});
 await close();context=await launch();inspector=await diagnostic(context);
 for(let i=0;i<50;i++){states=await infos(inspector);if(['keep','clear','extension'].every(part=>states.some(x=>x.tabUrl===origin+'/'+part)))break;await pause(100);}
 assert.equal(states.find(x=>x.tabUrl===origin+'/keep')?.isAutoDiscardable,false,'Explicit never-sleep lost on native session restore');
 assert.equal(states.find(x=>x.tabUrl===origin+'/clear')?.isAutoDiscardable,true,'Removed preference restored incorrectly');
 assert.equal(states.find(x=>x.tabUrl===origin+'/extension')?.isAutoDiscardable,true,'Ephemeral extension flag was persisted as user preference');
 results.push({name:'Native browser restart restores explicit protection and excludes transient extension state',passed:true});
 await toggle('keep');assert.equal((await infos(inspector)).find(x=>x.tabUrl===origin+'/keep').isAutoDiscardable,true);
 await close();context=await launch();inspector=await diagnostic(context);
 for(let i=0;i<50;i++){states=await infos(inspector);if(states.some(x=>x.tabUrl===origin+'/keep'))break;await pause(100);}
 assert.equal(states.find(x=>x.tabUrl===origin+'/keep')?.isAutoDiscardable,true,'Restored preference removal failed to persist');
 results.push({name:'Removing a restored never-sleep preference survives a second native restart',passed:true});
})().catch(error=>{console.error(error);process.exitCode=1;results.push({error:String(error),passed:false});}).finally(async()=>{
 if(browser)await browser.close();if(handle)try{native('close')}catch{}server.close();
 fs.writeFileSync(path.join(root,'verification/2026-10-05/sleep-session.json'),JSON.stringify({profile,results},null,2));
});
