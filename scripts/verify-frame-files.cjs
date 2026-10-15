/* Native Windows file dialogs and bookmark HTML import/export in an isolated profile. */
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process');
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const root=path.resolve(__dirname,'..'),evidence=path.join(root,'verification/2026-10-05'),profile=fs.mkdtempSync(path.join(root,'profiles/files-')),results=[];
const imported=path.join(profile,'nested.html'),malformed=path.join(profile,'malformed.html'),exported=path.join(profile,'exported.html'),uploaded=path.join(profile,'upload-fixture.txt');
fs.writeFileSync(imported,`<!DOCTYPE NETSCAPE-Bookmark-file-1>
<META HTTP-EQUIV="Content-Type" CONTENT="text/html; charset=UTF-8">
<TITLE>Bookmarks</TITLE>
<H1>Bookmarks</H1>
<DL><p>
    <DT><H3>Imported test</H3>
    <DL><p>
        <DT><H3>Nested Ω</H3>
        <DL><p>
            <DT><A HREF="https://frame-test.test/nested?q=1&amp;b=2#fragment">Unicode café</A>
        </DL><p>
    </DL><p>
</DL><p>
`);
fs.writeFileSync(malformed,'<!doctype html><title>Not bookmarks</title><p>Malformed inert import fixture');
fs.writeFileSync(uploaded,'Native upload fixture; contains no user files.');
let context,handle;
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const native=(window,action,name,value)=>cp.execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(root,'scripts/frame-ui.ps1'),'-WindowHandle',String(window),'-Action',action,...(name?['-Name',name]:[]),...(value!==undefined?['-Value',value]:[])],{encoding:'utf8'}).trim();
const fileNative=(dialog,action,value)=>cp.execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(root,'scripts/frame-file-dialog.ps1'),'-FrameHandle',handle,'-DialogHandle',String(dialog),'-Action',action,...(value!==undefined?['-Value',value]:[])],{encoding:'utf8'}).trim();
const read=(window,action)=>[].concat(JSON.parse(cp.execFileSync('powershell.exe',['-NoProfile','-Command',`& '${path.join(root,'scripts/frame-ui.ps1')}' -WindowHandle ${window} -Action ${action} | ConvertTo-Json -Compress`],{encoding:'utf8'})));
async function fileDialog(filename,save=false,cancel=false){
 let dialog;
 for(let attempt=0;attempt<20&&!dialog;attempt++){
  dialog=read(handle,'windows').find(item=>String(item.Handle)!==handle&&/open|save|import|export/i.test(item.Name));
  if(!dialog)await pause(150);
 }
 if(!dialog)fs.writeFileSync(path.join(evidence,'file-dialog-windows.json'),JSON.stringify(read(handle,'windows'),null,2));
 assert(dialog,'Owned native Windows file dialog missing');
 fs.writeFileSync(path.join(evidence,'file-dialog-controls.json'),JSON.stringify({dialog,controls:JSON.parse(fileNative(dialog.Handle,'list'))},null,2));
 if(cancel){fileNative(dialog.Handle,'cancel');return;}
 fileNative(dialog.Handle,'filename',filename);
 fileNative(dialog.Handle,save?'save':'open');await pause(350);
}
function flatten(nodes){return nodes.flatMap(node=>[node,...flatten(node.children||[])])}
(async()=>{
 context=await chromium.launchPersistentContext(profile,{executablePath:path.join(root,'chromium/src/out/FrameBaseline/Frame.exe'),headless:false,chromiumSandbox:true,colorScheme:null,viewport:null,args:['--no-first-run','--force-renderer-accessibility','--window-size=1280,900']});
 handle=require('./frame-test-tools.cjs').frameWindowHandle(profile);
 const manager=context.pages()[0];await manager.goto('chrome://bookmarks/');
 const existing=await manager.evaluate(()=>chrome.bookmarks.create({title:'Existing preserved',url:'https://frame-test.test/existing'}));
 async function command(id){await manager.locator('bookmarks-toolbar #menuButton').click();await manager.locator(`button[data-command="${id}"]`).click();}
 await command(18);await fileDialog(imported);
 let nodes;
 for(let attempt=0;attempt<100;attempt++){
  nodes=flatten(await manager.evaluate(()=>chrome.bookmarks.getTree()));
  if(nodes.some(node=>node.title==='Unicode café'))break;
  await pause(100);
 }
 fs.writeFileSync(path.join(evidence,'imported-bookmark-tree.json'),JSON.stringify(nodes,null,2));
 assert(nodes.some(node=>node.title==='Unicode café'),'Native bookmark import did not finish');
 const entry=nodes.find(node=>node.title==='Unicode café'),nested=nodes.find(node=>node.id===entry.parentId);
 assert.equal(entry.url,'https://frame-test.test/nested?q=1&b=2#fragment');assert.equal(nested.title,'Nested Ω');assert(nodes.some(node=>node.id===existing.id));
 results.push({name:'Native HTML import preserves nested Unicode folders, URL query/fragment and existing bookmarks',passed:true});
 await command(18);await fileDialog(malformed);
 await pause(1500);
 nodes=flatten(await manager.evaluate(()=>chrome.bookmarks.getTree()));assert(nodes.some(node=>node.id===entry.id));assert(nodes.some(node=>node.id===existing.id));
 results.push({name:'Malformed non-bookmark HTML leaves existing native bookmark data intact',passed:true});
 await command(19);await fileDialog(exported,true);
 for(let attempt=0;attempt<30&&!fs.existsSync(exported);attempt++)await pause(100);
 assert(fs.existsSync(exported));const html=fs.readFileSync(exported,'utf8');assert(html.includes('Nested Ω'));assert(html.includes('Unicode café'));
 const page=await context.newPage();await context.route('https://frame-test.test/upload',route=>route.fulfill({contentType:'text/html',body:'<!doctype html><title>Native file fixture</title><input type="file" aria-label="Upload fixture">'}));await page.goto('https://frame-test.test/upload');
 // Parse the exported file in an ordinary fixture document; privileged WebUI
 // Trusted Types enforcement remains intact.
 assert.equal(await page.evaluate(html=>Array.from(new DOMParser().parseFromString(html,'text/html').querySelectorAll('a')).find(link=>link.textContent==='Unicode café').getAttribute('href'),html),'https://frame-test.test/nested?q=1&b=2#fragment');
 results.push({name:'Native export writes the imported nested folder and canonical URL to selected HTML file',passed:true});
 await page.getByLabel('Upload fixture').click();await fileDialog(uploaded);
 assert.deepEqual(await page.getByLabel('Upload fixture').evaluate(async element=>({name:element.files[0].name,text:await element.files[0].text()})),{name:'upload-fixture.txt',text:'Native upload fixture; contains no user files.'});
 results.push({name:'Real Windows upload chooser delivers only the selected fixture file to the page',passed:true});
 await page.getByLabel('Upload fixture').click();await fileDialog(undefined,false,true);
 assert.equal(await page.getByLabel('Upload fixture').evaluate(element=>element.files[0].name),'upload-fixture.txt');
 results.push({name:'Cancelling native upload selection preserves the previous selected file',passed:true});
 console.log(`${results.length} file checks passed`);
})().catch(error=>{console.error(error);process.exitCode=1;results.push({error:String(error),passed:false})}).finally(async()=>{
 if(handle)try{
  const dialog=read(handle,'windows').find(item=>String(item.Handle)!==handle&&/open|save|import|export/i.test(item.Name));
  if(dialog)fileNative(dialog.Handle,'cancel');
 }catch{}
 if(context)await context.close();fs.writeFileSync(path.join(evidence,'files.json'),JSON.stringify({profile,results},null,2));
});
