/* Real native proactive discard safety using disposable sites and fake capture
 * devices. Permission grants are test-scoped; no physical camera or mic used.
 */
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),http=require('node:http');
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const root=path.resolve(__dirname,'..'),profile=fs.mkdtempSync(path.join(root,'profiles/sleep-safety-')),results=[];
let browser,handle,endpoint,loadingResponse,downloadResponse;
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const native=(action,name)=>cp.execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(root,'scripts/frame-ui.ps1'),'-WindowHandle',String(handle),'-Action',action,...(name?['-Name',name]:[])],{encoding:'utf8',windowsHide:true});
const server=http.createServer((req,res)=>{
 if(req.url==='/download'){downloadResponse=res;res.writeHead(200,{'Content-Type':'application/octet-stream','Content-Disposition':'attachment; filename="safety-fixture.bin"','Content-Length':'1048576'});res.write(Buffer.alloc(1024));return;}
 res.setHeader('Content-Type','text/html');
 res.write(`<!doctype html><title>Sleep safety ${req.url}</title><p>${req.url}</p><button id="start">Start fixture</button><a href="/download">Download fixture</a><video muted autoplay playsinline></video><script>
 start.onclick=async()=>{
  if(location.pathname==='/capture'){window.stream=await navigator.mediaDevices.getUserMedia({audio:true,video:true});document.querySelector('video').srcObject=stream;await document.querySelector('video').play();}
  if(location.pathname==='/audio'){window.audio=new AudioContext();window.osc=audio.createOscillator();const gain=audio.createGain();gain.gain.value=0.015;osc.frequency.value=100;osc.connect(gain).connect(audio.destination);osc.start();await audio.resume();}
  if(location.pathname==='/unload'){window.onbeforeunload=e=>{e.preventDefault();e.returnValue='';};}
  document.body.dataset.started='true';
 };</script>`);
 if(req.url==='/loading'){loadingResponse=res;return;}res.end();
});
async function inspect(endpoint){
 const targets=await (await fetch(endpoint+'/json/list')).json(),target=targets.find(x=>x.url==='chrome://discards/');assert(target);
 const socket=new WebSocket(target.webSocketDebuggerUrl);await new Promise((resolve,reject)=>{socket.onopen=resolve;socket.onerror=reject;});
 const result=new Promise((resolve,reject)=>{socket.onmessage=e=>{const m=JSON.parse(e.data);if(m.id===1)m.error?reject(Error(JSON.stringify(m.error))):resolve(m.result.result.value);};});
 socket.send(JSON.stringify({id:1,method:'Runtime.evaluate',params:{expression:"import('chrome://discards/discards.js').then(m=>m.getOrCreateDetailsProvider().getTabDiscardsInfo()).then(r=>r.infos)",awaitPromise:true,returnByValue:true}}));
 try{return await result;}finally{socket.close();}
}
(async()=>{
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const origin=`http://127.0.0.1:${server.address().port}`;
 cp.spawn(path.join(root,'chromium/src/out/FrameBaseline/Frame.exe'),[`--user-data-dir=${profile}`,'--remote-debugging-port=0','--no-first-run','--force-renderer-accessibility','--window-size=1280,900','--use-fake-device-for-media-stream'],{stdio:'ignore'});
 const file=path.join(profile,'DevToolsActivePort');for(let i=0;i<150&&!fs.existsSync(file);i++)await pause(100);assert(fs.existsSync(file));
 endpoint=`http://127.0.0.1:${fs.readFileSync(file,'utf8').split('\n')[0]}`;
 browser=await chromium.connectOverCDP(endpoint);handle=require('./frame-test-tools.cjs').frameWindowHandle(profile);const context=browser.contexts()[0];
 for(const part of ['clean','capture','audio','unload','pinned','loading']){
  const page=await context.newPage();await page.goto(origin+'/'+part,{waitUntil:part==='loading'?'commit':'load'});
  if(['capture','audio','unload'].includes(part)){
   await page.locator('#start').click();
   if(part==='capture'){
    await pause(500);
    const controls=JSON.parse(cp.execFileSync('powershell.exe',['-NoProfile','-Command',`& '${path.join(root,'scripts/frame-ui.ps1')}' -WindowHandle ${handle} -Action list | ConvertTo-Json -Compress`],{encoding:'utf8',windowsHide:true}));
    fs.writeFileSync(path.join(root,'verification/2026-10-05/sleep-safety-permission.json'),JSON.stringify(controls,null,2));
    const allow=controls.find(x=>/^Allow while visiting|^Allow on every visit|^Allow this time|^Allow$/.test(x.Name));
    assert(allow,'Native permission approval button missing');native('invoke',allow.Name);
   }
   await page.waitForFunction(()=>document.body.dataset.started==='true');
  }
  if(part==='capture')assert.equal(await page.evaluate(()=>stream.getTracks().every(t=>t.readyState==='live')),true);
  if(part==='audio')assert.equal(await page.evaluate(()=>audio.state),'running');
  if(part==='pinned'){
   const name='Sleep safety /pinned';native('contextmenu',name);await pause(500);native('invoke','Pin');await pause(500);
  }
 }
 const diagnostic=await context.newPage();await diagnostic.goto('chrome://chrome-urls/');await diagnostic.getByRole('button',{name:/enable/i}).click();await diagnostic.goto('chrome://discards/');
 fs.writeFileSync(path.join(root,'verification/2026-10-05/sleep-safety-capture-before.json'),JSON.stringify(await inspect(endpoint),null,2));
 const active=await context.newPage();await active.goto(origin+'/active');
 const event=active.waitForEvent('download');await active.getByRole('link').click();await event;await active.bringToFront();
 await browser.close();browser=null;await pause(1000);native('invoke','Sleep eligible background tabs');await pause(1000);
 let states=await inspect(endpoint);assert.notEqual(states.find(x=>x.tabUrl===origin+'/clean').state,5,'In-progress download did not conservatively block bulk sleep');
 assert(states.find(x=>x.tabUrl===origin+'/capture').cannotDiscardReasons.some(reason=>/captur/i.test(reason)),'Capture protection was not present in native lifecycle after detaching automation');
 results.push({name:'Actual in-progress download prevents bulk sleeping',passed:true});
 downloadResponse.end(Buffer.alloc(1048576-1024));await pause(2000);
 native('invoke','Sleep eligible background tabs');await pause(1500);states=await inspect(endpoint);
 assert.equal(states.find(x=>x.tabUrl===origin+'/clean').state,5,'Clean control failed to discard after completed download');
 for(const part of ['capture','audio','unload','pinned','loading','active'])assert.notEqual(states.find(x=>x.tabUrl===origin+'/'+part)?.state,5,`Unsafe discard of ${part}`);
 results.push({name:'Native playback, live camera/microphone capture, unload risk, pinned, loading and active pages survive while clean control sleeps',passed:true});
 fs.writeFileSync(path.join(root,'verification/2026-10-05/sleep-safety-states.json'),JSON.stringify(states,null,2));
})().catch(error=>{console.error(error);process.exitCode=1;results.push({error:String(error),passed:false});}).finally(async()=>{
 if(loadingResponse)loadingResponse.end();if(downloadResponse&&!downloadResponse.writableEnded)downloadResponse.end();
 // Measurements are finished. Reconnect only for isolated fixture cleanup,
 // removing the intentionally registered unload handler before native close.
 try{
  if(!browser&&endpoint)browser=await chromium.connectOverCDP(endpoint);
  const unload=browser?.contexts()[0].pages().find(p=>p.url().endsWith('/unload'));
  if(unload)await unload.evaluate(()=>window.onbeforeunload=null);
 }catch{}
 if(browser)await browser.close();
 if(handle)try{
  native('close');await pause(500);
  const controls=JSON.parse(cp.execFileSync('powershell.exe',['-NoProfile','-Command',`& '${path.join(root,'scripts/frame-ui.ps1')}' -WindowHandle ${handle} -Action list | ConvertTo-Json -Compress`],{encoding:'utf8',windowsHide:true}));
  // The unload fixture intentionally prompts. Accept only this isolated
  // browser's actual native Leave button during test cleanup.
  if(controls.some(x=>x.Name==='Leave'))native('invoke','Leave');
 }catch{}
 server.closeAllConnections();server.close();
 fs.writeFileSync(path.join(root,'verification/2026-10-05/sleep-safety.json'),JSON.stringify({profile,results,capture:'Synthetic Chromium device with actual native permission approval in the isolated browser; native lifecycle capture reason asserted after detaching. No physical hardware acceptance.'},null,2));
});
