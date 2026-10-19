#pragma once
#include <Arduino.h>

static const char OTA_WEB_UI_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Firmware update</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--ink:#16181d;--mute:#6b7280;--line:#e3e5e9;--acc:#2563eb;--ok:#15803d;--bad:#b91c1c;--warn:#a16207;--tint:#eef2ff}
@media (prefers-color-scheme:dark){:root{--bg:#0f1115;--card:#181b21;--ink:#e8eaee;--mute:#9aa1ad;--line:#2a2e36;--acc:#60a5fa;--ok:#4ade80;--bad:#f87171;--warn:#facc15;--tint:#1c2433}}
[hidden]{display:none!important}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:520px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:8px 0 14px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin-bottom:14px}
h2{font-size:13px;letter-spacing:.06em;text-transform:uppercase;color:var(--mute);margin:0 0 10px}
dl{display:grid;grid-template-columns:auto 1fr;gap:6px 14px;margin:0;font-size:14px}
dt{color:var(--mute)}dd{margin:0;font-family:ui-monospace,monospace;overflow-wrap:anywhere}
input[type=file]{width:100%;font-size:15px;margin-bottom:12px}
input[type=password],input[type=text]{width:100%;font-size:16px;padding:10px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--ink);margin-bottom:10px}
button{width:100%;font-size:16px;font-weight:600;padding:12px;border:0;border-radius:8px;background:var(--acc);color:#fff}
button:disabled{opacity:.45}
button.sec{background:transparent;color:var(--ink);border:1px solid var(--line);margin-top:8px}
.bar{height:10px;background:var(--line);border-radius:6px;overflow:hidden;margin:12px 0 6px}
.bar>i{display:block;height:100%;width:0;background:var(--acc);transition:width .15s}
.msg{font-size:14px;min-height:20px}.ok{color:var(--ok)}.bad{color:var(--bad)}.warn{color:var(--warn)}
.tip{font-size:14px;background:var(--tint);border-radius:10px;padding:10px 12px}
.nets{display:grid;gap:6px;margin-bottom:10px}
.net{display:flex;justify-content:space-between;align-items:center;gap:10px;text-align:left;font-weight:500;background:var(--bg);color:var(--ink);border:1px solid var(--line);padding:10px 12px}
.net span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.net small{color:var(--mute);flex:none}
.row{display:flex;gap:8px}.row button{margin-top:0}
</style></head><body><main>
<p id="back" hidden style="margin:4px 0 0"><a href="/" style="color:var(--acc)">&larr; Back to the app</a></p>
<h1 id="title">Firmware update</h1>
<div class="card tip" id="hotspotTip" hidden>You're connected through the board's own hotspot. If this page stops loading, turn off mobile data, or tap "Stay connected" when Android says the network has no internet. If the file picker doesn't open in the sign-in window, open <b>http://192.168.4.1</b> in Chrome instead.</div>
<div class="card" id="pwwarn" hidden><span class="warn">You're still using the default password (<b>esp32ota</b>). It protects the hotspot and this page, so change it below.</span></div>
<div class="card"><h2>Device</h2><dl id="info"><dt>Status</dt><dd>loading…</dd></dl></div>
<div class="card"><h2>Other firmware slot</h2><p class="msg" id="other" style="margin:0 0 10px">checking…</p>
<button id="switch" class="sec" style="margin:0" hidden>Switch to it</button><div class="msg" id="swmsg"></div></div>
<div class="card"><h2>Network</h2><dl id="net"><dt>Status</dt><dd>loading…</dd></dl></div>
<div class="card"><h2>Update firmware</h2>
<input type="file" id="file" accept=".bin">
<button id="go" disabled>Upload</button>
<div class="bar"><i id="prog"></i></div>
<div class="msg" id="msg"></div></div>
<div class="card"><h2>Join a Wi-Fi network</h2>
<p class="msg" style="margin:0 0 10px;color:var(--mute)">Optional. The board keeps its own hotspot either way; joining your home Wi-Fi lets you reach it without switching networks.</p>
<div class="nets" id="nets"></div>
<button id="scan" class="sec" style="margin:0 0 10px">Scan for networks</button>
<input type="text" id="ssid" placeholder="Network name" autocomplete="off" autocapitalize="off" spellcheck="false">
<input type="password" id="wpass" placeholder="Wi-Fi password (blank if open)" autocomplete="off">
<div class="row"><button id="join">Join</button><button id="forget" class="sec">Forget</button></div>
<div class="msg" id="wmsg"></div></div>
<div class="card"><h2>Change password</h2>
<input type="password" id="np" placeholder="New password (8+ characters)" autocomplete="new-password">
<button id="pw" class="sec">Save password</button>
<div class="msg" id="pwmsg"></div></div>
<div class="card"><button id="rb" class="sec" style="margin:0">Reboot device</button></div>
</main><script>
const $=id=>document.getElementById(id);let cur=null;
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
function row(k,v){return '<dt>'+k+'</dt><dd>'+v+'</dd>'}
function fmtUp(s){const h=Math.floor(s/3600),m=Math.floor(s%3600/60);return h+'h '+m+'m '+(s%60)+'s'}
function bars(r){return r>=-55?'strong':r>=-67?'good':r>=-78?'fair':'weak'}
function setMsg(id,t,k){$(id).textContent=t;$(id).className='msg'+(k?' '+k:'')}
async function status(){const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw 0;return r.json()}
async function post(url,body){const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(body||{})});let j={};try{j=await r.json()}catch(e){}return j}
async function refresh(){try{const s=await status();cur=s;
$('title').textContent=s.fw+' v'+s.ver;$('back').hidden=!s.appRoot;
const o=s.other;$('other').innerHTML=!o.valid?'Empty. Nothing to switch to yet.':o.name?'<b>'+esc(o.name+' v'+o.ver)+'</b><br><span style="color:var(--mute)">built '+esc(o.built)+'</span>':'Firmware without a name tag (an older build).';
$('switch').hidden=!o.valid;$('switch').textContent=o.name?'Switch to '+o.name+' v'+o.ver:'Switch to it';$('pwwarn').hidden=!s.defaultPass;$('hotspotTip').hidden=s.via!=='hotspot';
$('info').innerHTML=row('Firmware',esc(s.fw+' '+s.ver))+row('Built',esc(s.built))+row('Slot',esc(s.partition)+(s.trial?' (on trial)':''))+row('Uptime',fmtUp(s.uptime))+row('Free heap',(s.heap/1024|0)+' KB')+row('Free PSRAM',(s.psram/1024|0)+' KB')+row('Max firmware',(s.maxApp/1048576).toFixed(1)+' MB');
const st=s.sta,sta=st.state==='connected'?esc(st.ssid)+' · '+esc(st.ip)+' ('+bars(st.rssi)+')':st.state==='connecting'?'joining '+esc(st.ssid)+'…':st.state==='failed'?'<span class="bad">couldn’t join '+esc(st.ssid)+'</span>':'not set up';
$('net').innerHTML=row('Hotspot',esc(s.ap.ssid)+' · '+esc(s.ap.ip)+' · '+s.ap.clients+' connected')+row('Home Wi-Fi',sta)+(s.eth?row('Ethernet',s.eth.up?esc(s.eth.ip):'unplugged'):'')+row('Name',esc(s.host)+'.local');
}catch(e){$('info').innerHTML=row('Status','unreachable')}}
$('file').onchange=()=>{const f=$('file').files[0];$('go').disabled=!f;setMsg('msg','');
if(f&&/merged|full.?flash/i.test(f.name))setMsg('msg','That looks like the full-flash image. OTA needs the app .bin.','warn');
else if(f&&cur&&f.size>cur.maxApp)setMsg('msg','File is larger than the firmware slot.','bad')};
$('go').onclick=()=>{const f=$('file').files[0];if(!f)return;
$('go').disabled=true;setMsg('msg','Uploading…');
const fd=new FormData();fd.append('firmware',f,f.name);const x=new XMLHttpRequest();
x.upload.onprogress=e=>{if(e.lengthComputable)$('prog').style.width=(e.loaded/e.total*100)+'%'};
x.onload=()=>{let j={};try{j=JSON.parse(x.responseText)}catch(e){}
if(x.status==200&&j.ok){$('prog').style.width='100%';setMsg('msg',cur&&cur.via==='hotspot'?'Written. Rebooting… your phone may drop off the hotspot for a moment; rejoin it if it doesn’t reconnect by itself.':'Written. Rebooting…');waitBack()}
else{setMsg('msg','Failed: '+(j.error||('HTTP '+x.status)),'bad');$('go').disabled=false;$('prog').style.width='0'}};
x.onerror=()=>{setMsg('msg','Connection lost during upload.','bad');$('go').disabled=false};
x.open('POST','/api/update');x.send(fd)};
async function waitBack(){const t0=Date.now();await new Promise(r=>setTimeout(r,4000));
while(Date.now()-t0<150000){try{const s=await status();cur=s;await refresh();
$('switch').disabled=false;setMsg('swmsg','');$('msg').innerHTML='Back online running <b>'+esc(s.fw+' '+s.ver)+'</b>. It’s confirmed after ~15 s; if it can’t start networking it rolls back automatically.';$('msg').className='msg ok';$('prog').style.width='0';return}catch(e){}
await new Promise(r=>setTimeout(r,2000))}
setMsg('msg','The device hasn’t answered yet. Check you’re on its hotspot or network. If the new firmware is broken, the old one comes back within ~3 minutes.','warn')}
// ---- Wi-Fi join
$('scan').onclick=async()=>{$('scan').disabled=true;$('scan').textContent='Scanning…';setMsg('wmsg','The hotspot may blink for a second while scanning.');
try{let r=await (await fetch('/api/wifi/scan?start=1',{cache:'no-store'})).json();const t0=Date.now();
while(r.scanning&&Date.now()-t0<15000){await new Promise(z=>setTimeout(z,1200));r=await (await fetch('/api/wifi/scan',{cache:'no-store'})).json()}
const nets=(r.networks||[]).sort((a,b)=>b.rssi-a.rssi);$('nets').innerHTML='';
nets.forEach(n=>{const b=document.createElement('button');b.className='net';b.innerHTML='<span></span><small></small>';b.firstChild.textContent=(n.open?'':'🔒 ')+n.ssid;b.lastChild.textContent=bars(n.rssi);b.onclick=()=>{$('ssid').value=n.ssid;$('wpass').focus()};$('nets').appendChild(b)});
setMsg('wmsg',nets.length?'Tap a network, then enter its password.':'No networks found.')}catch(e){setMsg('wmsg','Scan failed. Try again.','bad')}
$('scan').disabled=false;$('scan').textContent='Scan again'};
$('join').onclick=async()=>{const ssid=$('ssid').value.trim();if(!ssid){setMsg('wmsg','Enter or pick a network name.','bad');return}
const j=await post('/api/wifi/join',{ssid,pass:$('wpass').value}).catch(()=>({}));
if(!j.ok){setMsg('wmsg',j.error||'Failed','bad');return}
setMsg('wmsg','Joining '+ssid+'… The hotspot may drop your phone briefly while the board switches channel.');$('wpass').value='';
const t0=Date.now();while(Date.now()-t0<60000){await new Promise(z=>setTimeout(z,2500));try{await refresh();const st=cur.sta;
if(st.state==='connected'){$('wmsg').innerHTML='Joined. On that network, open <b>http://'+esc(st.ip)+'</b> (or '+esc(cur.host)+'.local).';$('wmsg').className='msg ok';return}
if(st.state==='failed'){setMsg('wmsg','Couldn’t join. Check the password and that it’s a 2.4 GHz network.','bad');return}}catch(e){}}
setMsg('wmsg','Still trying. Check the Network section in a moment.','warn')};
$('forget').onclick=async()=>{if(!confirm('Forget the saved Wi-Fi network?'))return;const j=await post('/api/wifi/forget');setMsg('wmsg',j.ok?'Forgotten. The hotspot stays on.':'Failed',j.ok?'ok':'bad');refresh()};
// ---- switch slot
$('switch').onclick=async()=>{const o=cur&&cur.other;if(!confirm('Reboot into '+(o&&o.name?o.name+' v'+o.ver:'the other firmware')+'? You can switch back the same way from its update page.'))return;
$('switch').disabled=true;const j=await post('/api/switch').catch(()=>({}));
if(j.ok){setMsg('swmsg','Rebooting into the other firmware…','ok');waitBack()}else{setMsg('swmsg',j.error||'Failed','bad');$('switch').disabled=false}};
// ---- password / reboot
$('pw').onclick=async()=>{const j=await post('/api/password',{new:$('np').value}).catch(()=>({}));
if(j.ok){setMsg('pwmsg',cur&&cur.via==='hotspot'?'Saved. The hotspot restarts with the new password, so rejoin it with that password.':'Saved. Your browser will ask you to log in again with the new password.','ok');$('np').value='';refresh()}
else setMsg('pwmsg',j.error||'Failed','bad')};
$('rb').onclick=async()=>{if(!confirm('Reboot the device?'))return;await post('/api/reboot');setMsg('msg','Rebooting…');waitBack()};
refresh();setInterval(()=>{if(!$('go').disabled||!$('file').files[0])refresh()},10000);
</script></body></html>)HTML";
