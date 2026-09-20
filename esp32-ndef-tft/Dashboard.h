#pragma once
#include <Arduino.h>
static const char dashboard[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Spotify RFID Player</title>
<style>body{font:16px system-ui;background:#121212;color:#eee;max-width:760px;margin:30px auto;padding:16px}section{background:#242424;padding:18px;margin:16px 0;border-radius:12px}h1{color:#65da91}button,input,select{font:inherit;padding:10px;margin:5px;max-width:100%;box-sizing:border-box}button{cursor:pointer}pre{white-space:pre-wrap;overflow-wrap:anywhere;max-height:320px;overflow:auto}#message{color:#f3d07c}input{width:95%}</style></head><body>
<h1>Spotify RFID Player</h1><p id="message" role="status">Loading…</p>
<section><h2>Device status</h2><pre id="status"></pre><button data-url="/api/reset_rfid">Reset reader</button><button id="restart">Restart ESP32</button></section>
<section><h2>Speaker</h2><select id="devices"></select><button id="discover">Find speakers</button><button id="select">Select speaker</button><p>If the Echo is absent, wake Spotify on it and search again.</p>
<button data-url="/api/playpause">Play / pause</button><button data-url="/api/next">Next</button><button data-url="/api/volup">Volume +10</button><button data-url="/api/voldown">Volume −10</button></section>
<section id="displayControls"><h2>Display</h2><button data-url="/api/show_image">Last cover</button><button data-url="/api/show_info">Device info</button><button data-url="/api/clear_screen">Clear</button></section>
<section><h2>Spotify connection</h2><button id="testRefresh">Test saved refresh token</button><p id="tokenTestResult" role="status">Checks that the latest saved refresh token can obtain a new access token, including after reconnecting Spotify.</p><p><a id="reconnect" hidden>Reconnect Spotify</a></p><p id="reconnectHint">Checking reconnect availability…</p></section>
<section><h2>Replace refresh token</h2><form id="tokenForm"><input id="token" type="password" autocomplete="off" maxlength="1024" required placeholder="New refresh token"><button>Validate and save</button></form><p>The existing token is kept if validation fails.</p></section>
<section><h2>Recent logs</h2><pre id="logs"></pre></section>
<script>
const el=id=>document.getElementById(id);let busy=false,pending=new Map(),knownDevices='',tokenTestJob=null,tokenTestStarted=0;
async function request(url,body){let r=await fetch(url,body===undefined?{}:{method:'POST',headers:{'Content-Type':'application/json','X-Requested-With':'RFIDPlayer'},body:JSON.stringify(body)});let d=await r.json();if(!r.ok)throw Error(d.message||('HTTP '+r.status));return d;}
async function post(url,body={}){try{let d=await request(url,body);if(d.job){pending.set(d.job,url);el('message').textContent='Command queued ('+d.job+')';}else el('message').textContent=d.message||'Requested';}catch(e){el('message').textContent=e.message;}}
function finishTokenTest(message){el('tokenTestResult').textContent=message;el('testRefresh').disabled=false;tokenTestJob=null;}
async function testToken(){
if(el('testRefresh').disabled)return;el('testRefresh').disabled=true;el('tokenTestResult').textContent='Requesting a fresh access token from Spotify…';
try{let d=await request('/api/refresh_token',{});if(!d.job)throw Error('No test job returned');tokenTestJob=d.job;tokenTestStarted=Date.now();}
catch(e){finishTokenTest('Test could not start: '+e.message);}
}
function checkTokenTest(status){
if(tokenTestJob===null)return;
let job=status.jobs.find(j=>j.id===tokenTestJob);
if(job&&job.code!==202){
const errors={400:'Spotify rejected the refresh request or no refresh token is saved. Reconnect Spotify.',401:'Spotify authorization was rejected. Reconnect Spotify.',403:'Spotify denied access. Check the account and app permissions.',429:'Spotify requests are temporarily paused. Wait for the retry cooldown, then test again.',507:'The token works, but saving it failed. Do not reboot until the storage warning clears.',502:'Spotify returned an incomplete or invalid token response. Try again.',[-1]:'Wi-Fi or network connection failed. Reconnect and try again.',[-2]:'Waiting for the device clock to synchronize. Try again shortly.'};
finishTokenTest(job.code===200?'Passed: Spotify accepted the refresh token and issued a new access token.':(errors[job.code]||('Test failed (code '+job.code+'). Check the device logs and try again.')));
}else if(Date.now()-tokenTestStarted>90000){finishTokenTest('Test result unavailable. It may still complete; check the device logs before retrying.');}
}
async function poll(){if(busy||document.hidden)return;busy=true;try{let d=await request('/api/status');checkTokenTest(d);el('displayControls').hidden=!d.has_display;el('reconnect').hidden=!d.reconnect_ready;el('reconnect').href=d.reconnect_url;el('reconnectHint').textContent=d.reconnect_ready?(d.web_auth_required?'Open the secure reconnect page and sign in with your player admin password.':'Open the secure reconnect page to sign in to Spotify.'):'HTTPS reconnect unavailable. Check certificate provisioning and device logs.';if(d.certificate_days>=0)el('reconnectHint').textContent+=' HTTPS certificate: '+d.certificate_days+' days remaining. '+d.certificate_renewal+(d.certificate_issuer_days>=0?' (signing authority: '+d.certificate_issuer_days+' days remaining).':'');el('status').textContent='Firmware: '+d.firmware_build+'\nSpeaker: '+d.device_name+'\nDevice ID: '+(d.device_id||'Unavailable / undiscovered')+'\nSpotify authentication: '+(d.token_unsaved?'Storage failed — do not reboot':d.revoked?'Reconnect Spotify':d.token_valid?'Ready':'Waiting / refresh needed')+'\nRFID communication: '+(d.rfid_ok?'OK':'Failure')+'; version '+d.rfid_version+'\nScans: '+d.scans+'; failed reads: '+d.read_failures+'\nUptime: '+d.uptime_seconds+' seconds; last reset: '+d.reset_reason_name+' ('+d.reset_reason+'); Wi-Fi '+d.wifi_rssi+' dBm\nFree heap: '+d.free_heap+'; minimum: '+d.min_heap+'; largest block: '+d.largest_heap+'\nMaximum polling gap: '+d.max_poll_gap_ms+' ms; last poll '+d.poll_age_ms+' ms ago\nRetry cooldown: '+d.retry_ms+' ms';
for(let j of d.jobs){if(pending.has(j.id)&&j.code!==202){el('message').textContent=(j.code===200||j.code===204?'Completed: ':'Failed ('+j.code+'): ')+pending.get(j.id);pending.delete(j.id);}}
let devices=await request('/api/devices');let key=JSON.stringify(devices);if(key!==knownDevices){knownDevices=key;el('devices').replaceChildren();for(let device of devices.devices||[]){let option=document.createElement('option');option.value=JSON.stringify(device);option.textContent=device.name;el('devices').append(option);}}
let r=await fetch('/api/logs');if(r.ok)el('logs').textContent=await r.text();
}catch(e){el('message').textContent='Connection: '+e.message;}finally{busy=false;}}
for(let button of document.querySelectorAll('[data-url]'))button.onclick=()=>post(button.dataset.url);
el('testRefresh').onclick=testToken;
el('discover').onclick=()=>post('/api/rescan');el('select').onclick=()=>{if(el('devices').value)post('/api/select_device',JSON.parse(el('devices').value));};
el('tokenForm').onsubmit=e=>{e.preventDefault();let token=el('token').value;el('token').value='';post('/api/token',{token});};
el('restart').onclick=()=>{if(confirm('Restart ESP32?'))post('/api/restart');};
setInterval(poll,3000);document.addEventListener('visibilitychange',poll);poll();post('/api/rescan');
</script></body></html>)HTML";
