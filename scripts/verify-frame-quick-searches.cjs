/* Native keyword database/editor/restoration acceptance, isolated profile. */
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const root=path.resolve(__dirname,'..'),profile=fs.mkdtempSync(path.join(root,'profiles/quick-searches-')),results=[];let context,page;
async function launch(){
 context=await chromium.launchPersistentContext(profile,{executablePath:path.join(root,'chromium/src/out/FrameBaseline/Frame.exe'),headless:false,chromiumSandbox:true,colorScheme:null,viewport:null,args:['--no-first-run','--force-renderer-accessibility','--window-size=1280,900']});
 await context.route(/^https?:\/\/(www\.reddit\.com|github\.com|en\.wikipedia\.org|www\.google\.com|www\.youtube\.com|frame-test\.test|localhost:3000)\//,r=>r.fulfill({contentType:'text/html',body:'<!doctype html><title>Quick search fixture</title><p>Destination fixture</p>'}));
 page=context.pages()[0];await page.goto('chrome://settings/searchEngines');
}
async function engines(){
 if(!page.url().endsWith('/searchEngines'))await page.goto('chrome://settings/searchEngines');
 await page.locator('settings-search-engine-entry').first().waitFor();
 return page.locator('settings-search-engine-entry').evaluateAll(rows=>rows.map(row=>row.engine));
}
const entry=keyword=>page.locator('settings-search-engine-entry').filter({has:page.getByText(keyword,{exact:true})});
async function visibleEntry(keyword){
 const row=entry(keyword);
 await row.waitFor({state:'attached'});
 if(!await row.isVisible()){
  const button=page.locator('#activeEngines cr-expand-button');await button.waitFor();
  if(!await button.evaluate(e=>e.expanded))await button.click();
 }
 await row.waitFor();return row;
}
async function save(name,keyword,url){
 const dialog=page.locator('settings-search-engine-edit-dialog');await dialog.locator('#searchEngine input').fill(name);await dialog.locator('#keyword input').fill(keyword);await dialog.locator('#queryUrl input').fill(url);await dialog.locator('#actionButton').click();await dialog.waitFor({state:'detached'});
}
async function edit(keyword,name,newKeyword,url){
 const row=await visibleEntry(keyword);const icon=row.locator('#editIconButton');
 if(await icon.isVisible())await icon.click();else{await row.locator('.icon-more-vert').click();await row.locator('#editOption').click();}
 await save(name,newKeyword,url);
}
async function remove(keyword){
 const row=await visibleEntry(keyword);await row.locator('.icon-more-vert').click();await row.locator('#delete:visible, #deleteOption:visible').click();
 const confirmation=page.locator('#deleteConfirmDialog');if(await confirmation.isVisible())await confirmation.locator('.action-button').click();
 await row.waitFor({state:'detached'});
}
(async()=>{
 await launch();let list=await engines();for(const key of ['yt','!yt','gh','!gh','wiki','!wiki','r','!r','maps','!maps'])assert(list.some(e=>e.keyword===key),`Missing built-in ${key}`);
 results.push({name:'All five built-in engines expose editable ordinary and bang aliases',passed:true});
 await page.locator('#addSearchEngine').click();await save('User fixture','mine','https://frame-test.test/custom?q=%s');
 await edit('gh','Edited built-in','gh-renamed','https://frame-test.test/edited?q=%s');
 await remove('wiki');
 await context.close();context=null;await launch();list=await engines();
 assert(!list.some(e=>e.keyword==='wiki'),'Deleted alias returned on restart');assert(list.some(e=>e.keyword==='gh-renamed'));assert(list.some(e=>e.keyword==='mine'));
 results.push({name:'Native add/edit/delete and browser restart retain user additions and deletion',passed:true});
 await page.goto('chrome://settings/search');await page.locator('#restoreFrameQuickSearches').click();await page.locator('#confirmationToast').waitFor({state:'visible'});
 list=await engines();assert(list.some(e=>e.keyword==='wiki'));assert(list.some(e=>e.keyword==='gh'&&e.name==='GitHub'));assert(!list.some(e=>e.keyword==='gh-renamed'));assert(list.some(e=>e.keyword==='mine'&&e.url==='https://frame-test.test/custom?q=%s'));
 results.push({name:'Actual restore button restores renamed/deleted built-ins by native identity and retains user addition',passed:true});
 await page.goto('chrome://settings/searchEngines');await remove('yt');await page.locator('#addSearchEngine').click();await save('Keyword collision','yt','https://frame-test.test/collision?q=%s');
 await page.goto('chrome://settings/search');await page.locator('#restoreFrameQuickSearches').click();list=await engines();
 assert.equal(list.find(e=>e.keyword==='yt').name,'Keyword collision');assert.equal(list.filter(e=>e.keyword==='yt').length,1);
 results.push({name:'Restore preserves conflicting user keywords without duplicates',passed:true});
 await page.goto('chrome://newtab/');await page.locator('#frameHeading').waitFor();const input=page.locator('ntp-searchbox cr-searchbox-input').getByRole('combobox');await input.fill('!r café & 日本語');
 await page.waitForFunction(()=>document.querySelector('ntp-app').shadowRoot.querySelector('ntp-searchbox').result?.matches?.[0]?.destinationUrl?.includes('reddit.com'));
 await page.keyboard.press('Enter');await page.waitForURL(u=>u.hostname==='www.reddit.com');assert.equal(new URL(page.url()).searchParams.get('q'),'café & 日本語');
 results.push({name:'Shared native bang resolver encodes Reddit query and Unicode correctly',passed:true});
 for(const [alias,expected] of [['r','https://www.reddit.com/'],['!gh','https://github.com/'],['maps','https://www.google.com/maps'],['mine','https://frame-test.test/'],['http://localhost:3000/','http://localhost:3000/'],['https://frame-test.test/path?q=one#two','https://frame-test.test/path?q=one#two']]){
  await page.goto('chrome://newtab/');await page.locator('#frameHeading').waitFor();
  await page.locator('ntp-searchbox cr-searchbox-input').getByRole('combobox').fill(alias);
  await page.waitForFunction(expected=>document.querySelector('ntp-app').shadowRoot.querySelector('ntp-searchbox').result?.matches?.[0]?.destinationUrl===expected,expected);
  await page.keyboard.press('Enter');await page.waitForURL(expected);assert.equal(page.url(),expected);
  results.push({name:`Native bare alias or URL precedence: ${alias}`,passed:true});
 }
})().catch(error=>{console.error(error);process.exitCode=1;results.push({error:String(error),passed:false});}).finally(async()=>{if(context)await context.close();fs.writeFileSync(path.join(root,'verification/2026-10-05/quick-searches.json'),JSON.stringify({profile,results},null,2));});
