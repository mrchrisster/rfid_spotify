const fs=require('fs'),vm=require('vm'),assert=require('assert');
const script=fs.readFileSync('WifiSetup.cpp','utf8').split('<script>')[1].split('</script>')[0];
const elements=new Map();
const element=id=>{if(!elements.has(id))elements.set(id,{value:id==='lang'?'de':'',disabled:false,textContent:'',children:[],replaceChildren(x){this.children=[x];},append(x){this.children.push(x);}});return elements.get(id);};
let state=1,fail=false,timers=0,posts=[];
const context=vm.createContext({document:{getElementById:element},Option:function(text,value){this.text=text;this.value=value;},setTimeout(){timers++;},fetch:async(url,options)=>{
 if(options&&options.method==='POST'){assert.equal(options.headers['X-Requested-With'],'RFIDPlayer');posts.push({url,body:JSON.parse(options.body)});return {ok:!fail,status:400,json:async()=>({message:'Invalid Wi-Fi request'})};}
 return {ok:true,json:async()=>url==='/wifi/networks'?{scanning:false,networks:[{ssid:'<script>not markup</script>',rssi:-45}]}:{state,ip:'192.168.0.25',language:'de'}};
}});
vm.runInContext(script.replace(/poll\(\);\s*$/,''),context);
const run=s=>vm.runInContext(s,context);
(async()=>{
 await run('poll()');assert.equal(element('lang').value,'de');assert.equal(element('save').disabled,false);
 for(const lang of ['de','en','fr','es']){element('lang').value=lang;element('lang').onchange();assert(element('save').textContent);}
 element('ssid').value='New Wi-Fi';element('password').value='secret-value';
 await element('form').onsubmit({preventDefault(){}});assert.equal(posts.at(-1).body.ssid,'New Wi-Fi');assert.equal(element('password').value,'');assert(element('save').disabled);
 state=4;await run('poll()');assert(!element('save').disabled);
 state=5;await run('poll()');assert(!element('save').disabled);
 await element('scan').onclick();await run('poll()');assert.equal(element('networks').children[1].text,'<script>not markup</script> (-45 dBm)');
 state=3;let before=timers;await run('poll()');assert.equal(timers,before);assert(element('save').disabled&&element('cancel').disabled);assert(element('result').textContent.includes('http://192.168.0.25/'));assert(!element('chooseSpeaker').hidden);assert.equal(element('chooseSpeaker').href,'http://192.168.0.25/?setup=speaker#speakerSetup');assert(element('chooseSpeaker').textContent);
 console.log('Wi-Fi portal translations, credential submission, retry and successful-close UI passed');
})().catch(e=>{console.error(e);process.exitCode=1;});
