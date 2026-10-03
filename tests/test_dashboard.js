const fs=require('fs'),vm=require('vm'),assert=require('assert');
const html=fs.readFileSync('Dashboard.h','utf8');
let script=html.split('<script>')[1].split('</script>')[0];
const ids=[...html.split('<script>')[0].matchAll(/id="([^"]+)"/g)].map(m=>m[1]);
assert.equal(new Set(ids).size,ids.length,'No duplicate IDs');

const elements=new Map();
const element=id=>{assert(ids.includes(id),'Missing HTML element: '+id);if(!elements.has(id))elements.set(id,{textContent:'',disabled:false,value:'',dataset:{},setAttribute(k,v){this[k]=v;},removeAttribute(k){delete this[k];},children:[],replaceChildren(){this.children=[];},append(...items){this.children.push(...items);}});return elements.get(id);};
const views=['player','settings','diagnostics'].map(view=>Object.assign(element('view-'+view),{dataset:{view}}));
const links=views.map(v=>({dataset:{page:v.dataset.view},setAttribute(k,v){this[k]=v;},removeAttribute(k){delete this[k];}}));
const displayOnly=[element('displaySetup')];
let firmwareRequests=0,uploadStatus=200;
let speakerStatus="Unknown";let libraryCards=[],libraryPlays=0,libraryShuffles=0;
let expectedSeconds=0,savedSeconds=600,hasDisplay=false,logReads=0,failStatus=false,copied='',jobs=[],postCount=0,jobId=37,now=1000,rejectPost=false;
const context=vm.createContext({
 window:{location:{hash:''},addEventListener(){},scrollTo(){},isSecureContext:true},
 navigator:{clipboard:{writeText:async s=>{copied=s;}}},
 setInterval:()=>{},
 FormData:class {append(name,file){this.file=file;}},
 XMLHttpRequest:class {
  constructor(){this.upload={};this.headers={};}
  open(method,url){assert.equal(method,'POST');assert.equal(url,'/api/firmware/upload');}
  setRequestHeader(key,value){this.headers[key]=value;}
  send(form){assert.equal(this.headers['X-Requested-With'],'RFIDPlayer');assert.equal(this.headers['X-Firmware-Session'],'test-session');assert(form.file);this.upload.onprogress({lengthComputable:true,loaded:100,total:100});this.status=uploadStatus;this.responseText=JSON.stringify({message:uploadStatus===200?'Firmware installed. Restarting.':'Firmware validation failed'});this.onload();}
 },
 document:{addEventListener:()=>{},hidden:false,getElementById:element,querySelectorAll:s=>s==='[data-view]'?views:s==='[data-page]'?links:s==='[data-display-only]'?displayOnly:[],createElement:()=>({children:[],append(...items){this.children.push(...items);},setAttribute(k,v){this[k]=v;}})},
 Date:{now:()=>now},console,
 fetch:async(url,options)=>{
  if(url==='/api/firmware/prepare'){firmwareRequests++;assert.equal(JSON.parse(options.body).size,100);return {ok:true,json:async()=>({session:'test-session'})};}
  if(url==='/api/firmware/status')return {ok:true,json:async()=>({active:true,ready:true})};
  if(options&&options.method==='POST'){
   if(url==='/api/library/play'){libraryPlays++;const body=JSON.parse(options.body);if(body.shuffle){libraryShuffles++;assert.equal(body.uri,libraryCards[2].uri);}else assert.equal(body.uri,libraryCards[0].uri);return {ok:true,json:async()=>({job:123})};}
   if(url==='/api/display_settings') {
    assert.equal(options.headers['X-Requested-With'],'RFIDPlayer');
    if(rejectPost)return {ok:false,status:507,json:async()=>({message:'Display settings could not be saved'})};
    assert.deepEqual(JSON.parse(options.body),{debug:true,cover_seconds:expectedSeconds,cover_fit:"height"});
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
  if(url==='/api/status')return {ok:true,json:async()=>({speaker_status:speakerStatus,jobs,device_name:"Living room Echo",cover_seconds:savedSeconds,has_display:hasDisplay,wifi_connected:true,token_valid:true,rfid_ok:true,certificate_days:-1,certificate_issuer_days:-1})};
  if(url==='/api/library')return {ok:true,json:async()=>({cards:libraryCards})};
  if(url==='/api/devices')return {ok:true,json:async()=>({devices:[]})};
  if(url==='/api/logs')logReads++;
  return {ok:true,text:async()=>'Example log'};
 }
});
vm.runInContext(script,context);
const run=code=>vm.runInContext(code,context);
(async()=>{
 await new Promise(resolve=>setImmediate(resolve));
 await run('poll()');assert.equal(postCount,0,'Page load and polling must not submit commands');
 libraryCards=[{uri:'spotify:album:0123456789ABCDEFGHIJKL',artist:'Artist <safe>',title:'Book & story',used:2},{uri:'spotify:show:0123456789ABCDEFGHIJKL',used:1},{uri:'spotify:artist:0123456789ABCDEFGHIJKL',artist:'Artist',title:'Album',used:0}];
 await run('poll()');assert(element('libraryEmpty').hidden);assert.equal(element('cardLibrary').children.length,3);
 assert.equal(element('cardLibrary').children[0].children[0].children[0].textContent,'Book & story');
 assert.equal(element('cardLibrary').children[0].children[0].children[1].textContent,'Artist <safe>');
 assert.equal(element('cardLibrary').children[1].children[0].children[0].textContent,libraryCards[1].uri);
 await element('cardLibrary').children[0].children[1].children[0].onclick();assert.equal(libraryPlays,1);
 await run('poll()');assert.equal(libraryPlays,1,'Polling must not replay cards');
 assert.equal(element('cardLibrary').children[0].children[1].children.length,1);assert.equal(element('cardLibrary').children[1].children[1].children.length,1);
 assert.equal(element('cardLibrary').children[2].children[1].children.length,2);
 await element('cardLibrary').children[2].children[1].children[1].onclick();assert.equal(libraryShuffles,1);
 await run('poll()');assert.equal(libraryShuffles,1);
 jobs=[{id:123,code:422}];await run('poll()');assert(element('message').textContent.includes('No different album'));jobs=[];
 await run('testToken()');assert(element('testRefresh').disabled);
 assert(!element('tokenTestResult').textContent.includes('Passed'));
 await run('testToken()');assert.equal(postCount,1);
 jobs=[{id:37,code:202}];await run('poll()');assert(element('testRefresh').disabled);
 jobs=[{id:38,code:200}];await run('poll()');assert(!element('tokenTestResult').textContent.includes('Passed'));
 jobs=[{id:37,code:200}];await run('poll()');assert(element('tokenTestResult').textContent.startsWith('Passed:'));
 for(const state of ['Unavailable','Paused','Playing','Available','Unknown']){speakerStatus=state;await run('poll()');assert.equal(element('speakerBadge').textContent,'Speaker · '+state);assert.equal(element('speakerBadge').dataset.ok,String(['Paused','Playing','Available'].includes(state)));}

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
 await run("pending.set(101,'/api/restart')");jobs=[{id:101,code:507}];await run('poll()');assert(element('message').textContent.includes('Restart cancelled'));
 await run("pending.set(102,'/api/restart')");jobs=[{id:102,code:200}];await run('poll()');assert(element('message').textContent.includes('Progress saved'));
 rejectPost=false;element('coverFit').value='height';element('coverFit').onchange();element('displayDebug').checked=true;element('coverMinutes').value='0';element('coverMinutes').oninput();
 await run('poll()');assert.equal(element('coverMinutes').value,'0');assert.equal(element('coverFit').value,'height');assert(element('displayDebug').checked);
 await element('displaySettingsForm').onsubmit({preventDefault(){}});assert.equal(element('message').textContent,'Display settings saved');assert(!element('saveDisplaySettings').disabled);
 element('coverMinutes').value='0.01';await element('displaySettingsForm').onsubmit({preventDefault(){}});assert(element('message').textContent.includes('Minimum nonzero duration'));
 element('coverMinutes').value='0';element('coverMinutes').oninput();rejectPost=true;
 await element('displaySettingsForm').onsubmit({preventDefault(){}});assert(element('message').textContent.includes('could not be saved'));
 await run('poll()');assert.equal(element('coverMinutes').value,'0');
 await run('poll()');assert.equal(logReads,0,'Player and settings do not fetch logs');
 assert(element('displayControls').hidden);assert(element('displaySetup').hidden);assert(element('firmwareVariant').textContent.includes('Headless'));
 hasDisplay=true;await run('poll()');assert(!element('displayControls').hidden);assert(element('firmwareVariant').textContent.includes('Display (TFT)'));
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
 rejectPost=false;expectedSeconds=630;element('coverFit').value='height';element('displayDebug').checked=true;element('coverMinutes').value='10.5';
 await element('displaySettingsForm').onsubmit({preventDefault(){}});assert.equal(element('message').textContent,'Display settings saved');
 savedSeconds=630;await run('poll()');assert.equal(element('coverMinutes').value,10.5);
 savedSeconds=10;await run('poll()');expectedSeconds=10;await element('displaySettingsForm').onsubmit({preventDefault(){}});
 element('coverMinutes').value='';await element('displaySettingsForm').onsubmit({preventDefault(){}});assert(element('message').textContent.includes('Enter minutes'));
 element('firmwareFile').files=[{name:'bad.bin',size:100,slice:()=>({arrayBuffer:async()=>new Uint8Array(36).buffer})}];
 await element('firmwareForm').onsubmit({preventDefault(){}});assert.equal(firmwareRequests,0);assert(element('firmwareMessage').textContent.includes('application binary'));
 const appHeader=new Uint8Array(36);appHeader[0]=233;appHeader[12]=0;appHeader[32]=50;appHeader[33]=84;appHeader[34]=205;appHeader[35]=171;
 element('firmwareFile').files=[{name:'player.ino.bin',size:100,slice:()=>({arrayBuffer:async()=>appHeader.buffer})}];
 uploadStatus=400;await element('firmwareForm').onsubmit({preventDefault(){}});assert.equal(element('firmwareMessage').textContent,'Firmware validation failed');assert(!element('uploadFirmware').disabled);
 uploadStatus=200;await element('firmwareForm').onsubmit({preventDefault(){}});assert(element('firmwareMessage').textContent.includes('Restarting'));assert.equal(element('firmwareProgress').value,100);assert(element('uploadFirmware').disabled);
 console.log('Firmware UI validates image, reports errors and confirms successful upload');
 console.log('Minute conversion, fractional durations and legacy saved duration checks passed');
 console.log('Dashboard navigation, headless mode, status recovery, on-demand logs, pause and copy passed');
 console.log('Display settings UI save, validation, edit preservation and failure tests passed');
 console.log('Dashboard language selection/save/failure checks passed');
 console.log('Dashboard refresh-token test: queued/success/error/unknown-result checks passed');
})().catch(e=>{console.error(e);process.exitCode=1;});
