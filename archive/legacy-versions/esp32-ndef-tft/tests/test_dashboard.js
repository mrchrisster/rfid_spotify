const fs=require('fs'),vm=require('vm'),assert=require('assert');
const html=fs.readFileSync('Dashboard.h','utf8');
let script=html.split('<script>')[1].split('</script>')[0];
const ids=[...html.split('<script>')[0].matchAll(/id="([^"]+)"/g)].map(m=>m[1]);
assert.equal(new Set(ids).size,ids.length,'No duplicate IDs');

const elements=new Map();
const element=id=>{assert(ids.includes(id),'Missing HTML element: '+id);if(!elements.has(id))elements.set(id,{textContent:'',disabled:false,value:'',dataset:{},setAttribute(k,v){this[k]=v;},removeAttribute(k){delete this[k];},replaceChildren(){},append(){}});return elements.get(id);};
const views=['player','settings','diagnostics'].map(view=>Object.assign(element('view-'+view),{dataset:{view}}));
const links=views.map(v=>({dataset:{page:v.dataset.view},setAttribute(k,v){this[k]=v;},removeAttribute(k){delete this[k];}}));
const displayOnly=[element('displaySetup')];
let expectedSeconds=0,savedSeconds=600,hasDisplay=false,logReads=0,failStatus=false,copied='',jobs=[],postCount=0,jobId=37,now=1000,rejectPost=false;
const context=vm.createContext({
 window:{location:{hash:''},addEventListener(){},scrollTo(){},isSecureContext:true},
 navigator:{clipboard:{writeText:async s=>{copied=s;}}},
 setInterval:()=>{},
 document:{addEventListener:()=>{},hidden:false,getElementById:element,querySelectorAll:s=>s==='[data-view]'?views:s==='[data-page]'?links:s==='[data-display-only]'?displayOnly:[],createElement:()=>({})},
 Date:{now:()=>now},console,
 fetch:async(url,options)=>{
  if(options&&options.method==='POST'){
   if(url==='/api/display_settings') {
    assert.equal(options.headers['X-Requested-With'],'RFIDPlayer');
    if(rejectPost)return {ok:false,status:507,json:async()=>({message:'Display settings could not be saved'})};
    assert.deepEqual(JSON.parse(options.body),{debug:true,cover_seconds:expectedSeconds});
    return {ok:true,json:async()=>({message:'Display settings saved'})};
   }
   if(url==='/api/language') {
    assert.equal(options.headers['X-Requested-With'],'RFIDPlayer');
    if(rejectPost)return {ok:false,status:507,json:async()=>({message:'Language could not be saved'})};
    return {ok:true,json:async()=>({language:JSON.parse(options.body).language,message:'Screen language saved'})};
   }
   postCount++;assert.equal(url,'/api/refresh_token');assert.equal(options.headers['X-Requested-With'],'RFIDPlayer');
   if(rejectPost)return {ok:false,status:503,json:async()=>({message:'Queue full'})};
   return {ok:true,json:async()=>({job:jobId})};
  }
  if(url==='/api/status'&&failStatus)throw Error('Offline');
  if(url==='/api/status')return {ok:true,json:async()=>({jobs,device_name:"Living room Echo",cover_seconds:savedSeconds,has_display:hasDisplay,wifi_connected:true,token_valid:true,rfid_ok:true,certificate_days:-1,certificate_issuer_days:-1})};
  if(url==='/api/devices')return {ok:true,json:async()=>({devices:[]})};
  if(url==='/api/logs')logReads++;
  return {ok:true,text:async()=>'Example log'};
 }
});
vm.runInContext(script,context);
const run=code=>vm.runInContext(code,context);
(async()=>{
 await run('poll()');assert.equal(postCount,0,'Page load and polling must not submit commands');
 await run('testToken()');assert(element('testRefresh').disabled);
 assert(!element('tokenTestResult').textContent.includes('Passed'));
 await run('testToken()');assert.equal(postCount,1);
 jobs=[{id:37,code:202}];await run('poll()');assert(element('testRefresh').disabled);
 jobs=[{id:38,code:200}];await run('poll()');assert(!element('tokenTestResult').textContent.includes('Passed'));
 jobs=[{id:37,code:200}];await run('poll()');assert(element('tokenTestResult').textContent.startsWith('Passed:'));
 assert(!element('testRefresh').disabled);
 for(const [code,expected] of [[400,'rejected'],[429,'cooldown'],[507,'Do not reboot']]){
  await run('testToken()');jobs=[{id:37,code}];await run('poll()');assert(element('tokenTestResult').textContent.includes(expected));
 }
 await run('testToken()');jobs=[];now+=91000;await run('poll()');assert(element('tokenTestResult').textContent.includes('result unavailable'));
 assert(!element('testRefresh').disabled);
 rejectPost=true;await run('testToken()');assert(element('tokenTestResult').textContent.includes('Queue full'));assert(!element('testRefresh').disabled);
 rejectPost=false;
 element('language').value='de';element('language').onchange();
 await run('poll()');assert.equal(element('language').value,'de'); // Preserve unsaved choice.
 await element('languageForm').onsubmit({preventDefault(){}});
 assert.equal(element('language').value,'de');assert(!element('saveLanguage').disabled);
 assert(element('message').textContent.includes('saved'));
 rejectPost=true;element('language').value='fr';element('language').onchange();
 await element('languageForm').onsubmit({preventDefault(){}});
 assert(element('message').textContent.includes('could not be saved'));
 await run('poll()');assert.equal(element('language').value,'fr');
 await run("pending.set(99,'/api/select_device')");jobs=[{id:99,code:204}];await run('poll()');
 assert.equal(element('message').textContent,'Default speaker saved: Living room Echo');
 assert.equal(element('defaultSpeaker').textContent,'Default speaker: Living room Echo');
 await run("pending.set(100,'/api/select_device')");jobs=[{id:100,code:507}];await run('poll()');
 assert(element('message').textContent.includes('previous default is retained'));
 rejectPost=false;element('displayDebug').checked=true;element('coverMinutes').value='0';element('coverMinutes').oninput();
 await run('poll()');assert.equal(element('coverMinutes').value,'0');assert(element('displayDebug').checked);
 await element('displaySettingsForm').onsubmit({preventDefault(){}});assert.equal(element('message').textContent,'Display settings saved');assert(!element('saveDisplaySettings').disabled);
 element('coverMinutes').value='0.01';await element('displaySettingsForm').onsubmit({preventDefault(){}});assert(element('message').textContent.includes('Minimum nonzero duration'));
 element('coverMinutes').value='0';element('coverMinutes').oninput();rejectPost=true;
 await element('displaySettingsForm').onsubmit({preventDefault(){}});assert(element('message').textContent.includes('could not be saved'));
 await run('poll()');assert.equal(element('coverMinutes').value,'0');
 await run('poll()');assert.equal(logReads,0,'Player and settings do not fetch logs');
 assert(element('displayControls').hidden);assert(element('displaySetup').hidden);
 hasDisplay=true;await run('poll()');assert(!element('displayControls').hidden);
 assert.equal(element('spotifyBadge').textContent,'Spotify · Ready');assert(element('attention').hidden);
 run("window.location.hash='#speakerSetup';navigate()");await new Promise(resolve=>setImmediate(resolve));
 assert(!element('view-settings').hidden);assert(element('view-player').hidden);assert(element('speakerSetup').open);
 assert.equal(links[1]['aria-current'],'page');assert(!links[0]['aria-current']);
 run("window.location.hash='#diagnostics';navigate()");await new Promise(resolve=>setImmediate(resolve));
 assert(!element('view-diagnostics').hidden);assert.equal(element('logs').textContent,'Example log');assert(logReads>0);
 element('pauseLogs').onclick();const reads=logReads;await run('poll()');assert.equal(reads,logReads);
 assert.equal(element('pauseLogs')['aria-pressed'],'true');
 await element('copyLogs').onclick();assert.equal(copied,'Example log');
 failStatus=true;await run('poll()');assert(!element('attention').hidden);assert(element('attention').textContent.includes('Cannot reach'));
 failStatus=false;await run('poll()');assert(element('attention').hidden);
 run("window.location.hash='#unknown';navigate()");assert(!element('view-player').hidden);
 rejectPost=false;expectedSeconds=630;element('displayDebug').checked=true;element('coverMinutes').value='10.5';
 await element('displaySettingsForm').onsubmit({preventDefault(){}});assert.equal(element('message').textContent,'Display settings saved');
 savedSeconds=630;await run('poll()');assert.equal(element('coverMinutes').value,10.5);
 savedSeconds=10;await run('poll()');expectedSeconds=10;await element('displaySettingsForm').onsubmit({preventDefault(){}});
 element('coverMinutes').value='';await element('displaySettingsForm').onsubmit({preventDefault(){}});assert(element('message').textContent.includes('Enter minutes'));
 console.log('Minute conversion, fractional durations and legacy saved duration checks passed');
 console.log('Dashboard navigation, headless mode, status recovery, on-demand logs, pause and copy passed');
 console.log('Display settings UI save, validation, edit preservation and failure tests passed');
 console.log('Dashboard language selection/save/failure checks passed');
 console.log('Dashboard refresh-token test: queued/success/error/unknown-result checks passed');
})().catch(e=>{console.error(e);process.exitCode=1;});
