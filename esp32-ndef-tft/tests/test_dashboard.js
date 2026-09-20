const fs=require('fs'),vm=require('vm'),assert=require('assert');
let script=fs.readFileSync('Dashboard.h','utf8').split('<script>')[1].split('</script>')[0];
script=script.slice(0,script.indexOf('setInterval(poll,3000)'));
const elements=new Map();
const element=id=>{if(!elements.has(id))elements.set(id,{textContent:'',disabled:false,value:'',replaceChildren(){},append(){}});return elements.get(id);};
let jobs=[],postCount=0,jobId=37,now=1000,rejectPost=false;
const context=vm.createContext({
 document:{hidden:false,getElementById:element,querySelectorAll:()=>[],createElement:()=>({})},
 Date:{now:()=>now},console,
 fetch:async(url,options)=>{
  if(options&&options.method==='POST'){
   postCount++;assert.equal(url,'/api/refresh_token');assert.equal(options.headers['X-Requested-With'],'RFIDPlayer');
   if(rejectPost)return {ok:false,status:503,json:async()=>({message:'Queue full'})};
   return {ok:true,json:async()=>({job:jobId})};
  }
  if(url==='/api/status')return {ok:true,json:async()=>({jobs,has_display:false,certificate_days:-1,certificate_issuer_days:-1})};
  if(url==='/api/devices')return {ok:true,json:async()=>({devices:[]})};
  return {ok:true,text:async()=>''};
 }
});
vm.runInContext(script,context);
const run=code=>vm.runInContext(code,context);
(async()=>{
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
 console.log('Dashboard refresh-token test: queued/success/error/unknown-result checks passed');
})().catch(e=>{console.error(e);process.exitCode=1;});
