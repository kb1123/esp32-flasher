#pragma once
#include <Arduino.h>

static const char WEB_UI_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 OTA</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--ink:#16181d;--mute:#6b7280;--line:#e3e5e9;--acc:#2563eb;--ok:#15803d;--bad:#b91c1c;--warn:#a16207}
@media (prefers-color-scheme:dark){:root{--bg:#0f1115;--card:#181b21;--ink:#e8eaee;--mute:#9aa1ad;--line:#2a2e36;--acc:#60a5fa;--ok:#4ade80;--bad:#f87171;--warn:#facc15}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:520px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:8px 0 14px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin-bottom:14px}
h2{font-size:13px;letter-spacing:.06em;text-transform:uppercase;color:var(--mute);margin:0 0 10px}
dl{display:grid;grid-template-columns:auto 1fr;gap:6px 14px;margin:0;font-size:14px}
dt{color:var(--mute)}dd{margin:0;font-family:ui-monospace,monospace;word-break:break-all}
input[type=file]{width:100%;font-size:15px;margin-bottom:12px}
input[type=password]{width:100%;font-size:16px;padding:10px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--ink);margin-bottom:10px}
button{width:100%;font-size:16px;font-weight:600;padding:12px;border:0;border-radius:8px;background:var(--acc);color:#fff}
button:disabled{opacity:.45}
button.sec{background:transparent;color:var(--ink);border:1px solid var(--line);margin-top:8px}
.bar{height:10px;background:var(--line);border-radius:6px;overflow:hidden;margin:12px 0 6px}
.bar>i{display:block;height:100%;width:0;background:var(--acc);transition:width .15s}
.msg{font-size:14px;min-height:20px}.ok{color:var(--ok)}.bad{color:var(--bad)}.warn{color:var(--warn)}
</style></head><body><main>
<h1 id="title">ESP32 OTA</h1>
<div class="card" id="pwwarn" hidden><span class="warn">You're still using the default password. Change it below.</span></div>
<div class="card"><h2>Device</h2><dl id="info"><dt>Status</dt><dd>loading…</dd></dl></div>
<div class="card"><h2>Update firmware</h2>
<input type="file" id="file" accept=".bin">
<button id="go" disabled>Upload</button>
<div class="bar"><i id="prog"></i></div>
<div class="msg" id="msg"></div></div>
<div class="card"><h2>Change password</h2>
<input type="password" id="np" placeholder="New password (8+ characters)" autocomplete="new-password">
<button id="pw" class="sec">Save password</button>
<div class="msg" id="pwmsg"></div></div>
<div class="card"><button id="rb" class="sec" style="margin:0">Reboot device</button></div>
</main><script>
const $=id=>document.getElementById(id);let cur=null;
function row(k,v){return '<dt>'+k+'</dt><dd>'+v+'</dd>'}
function fmtUp(s){const h=Math.floor(s/3600),m=Math.floor(s%3600/60);return h+'h '+m+'m '+(s%60)+'s'}
async function status(){const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw 0;return r.json()}
async function refresh(){try{const s=await status();cur=s;
$('title').textContent=s.fw+' v'+s.ver;$('pwwarn').hidden=!s.defaultPass;
$('info').innerHTML=row('Firmware',s.fw+' '+s.ver)+row('Built',s.built)+row('Slot',s.partition+(s.trial?' (on trial)':''))+row('IP',s.ip)+row('Host',s.host+'.local')+row('MAC',s.mac)+row('Uptime',fmtUp(s.uptime))+row('Free heap',(s.heap/1024|0)+' KB')+row('Free PSRAM',(s.psram/1024|0)+' KB')+row('Max firmware',(s.maxApp/1048576).toFixed(1)+' MB');
}catch(e){$('info').innerHTML=row('Status','unreachable')}}
$('file').onchange=()=>{const f=$('file').files[0];$('go').disabled=!f;$('msg').textContent='';$('msg').className='msg';
if(f&&/merged/i.test(f.name)){$('msg').textContent='That looks like the full-flash image. OTA needs the app .bin.';$('msg').className='msg warn'}
else if(f&&cur&&f.size>cur.maxApp){$('msg').textContent='File is larger than the firmware slot.';$('msg').className='msg bad'}};
$('go').onclick=()=>{const f=$('file').files[0];if(!f)return;const before=cur?cur.uptime:0;
$('go').disabled=true;$('msg').className='msg';$('msg').textContent='Uploading…';
const fd=new FormData();fd.append('firmware',f,f.name);const x=new XMLHttpRequest();
x.upload.onprogress=e=>{if(e.lengthComputable)$('prog').style.width=(e.loaded/e.total*100)+'%'};
x.onload=()=>{let j={};try{j=JSON.parse(x.responseText)}catch(e){}
if(x.status==200&&j.ok){$('prog').style.width='100%';$('msg').textContent='Written. Rebooting…';waitBack()}
else{$('msg').textContent='Failed: '+(j.error||('HTTP '+x.status));$('msg').className='msg bad';$('go').disabled=false;$('prog').style.width='0'}};
x.onerror=()=>{$('msg').textContent='Connection lost during upload.';$('msg').className='msg bad';$('go').disabled=false};
x.open('POST','/api/update');x.send(fd)};
async function waitBack(){const t0=Date.now();await new Promise(r=>setTimeout(r,4000));
while(Date.now()-t0<120000){try{const s=await status();cur=s;await refresh();
$('msg').innerHTML='Back online running <b>'+s.fw+' '+s.ver+'</b>. It will be confirmed after ~15 s online; if it can’t get online it rolls back automatically.';$('msg').className='msg ok';$('prog').style.width='0';return}catch(e){}
await new Promise(r=>setTimeout(r,2000))}
$('msg').textContent='Device has not come back yet. If it stays offline, it will roll back to the previous firmware within ~3 minutes.';$('msg').className='msg warn'}
$('pw').onclick=async()=>{const v=$('np').value;const r=await fetch('/api/password',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'new='+encodeURIComponent(v)});
let j={};try{j=await r.json()}catch(e){}
if(j.ok){$('pwmsg').textContent='Saved. Your browser will ask you to log in again with the new password.';$('pwmsg').className='msg ok';$('np').value='';refresh()}
else{$('pwmsg').textContent=j.error||'Failed';$('pwmsg').className='msg bad'}};
$('rb').onclick=async()=>{if(!confirm('Reboot the device?'))return;await fetch('/api/reboot',{method:'POST'});$('msg').textContent='Rebooting…';$('msg').className='msg';waitBack()};
refresh();setInterval(()=>{if(!$('go').disabled||!$('file').files[0])refresh()},10000);
</script></body></html>)HTML";
