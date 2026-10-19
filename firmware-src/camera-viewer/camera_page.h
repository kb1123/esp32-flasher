#pragma once
#include <Arduino.h>

static const char CAMERA_PAGE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Camera</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--ink:#16181d;--mute:#6b7280;--line:#e3e5e9;--acc:#2563eb;--ok:#15803d;--bad:#b91c1c;--tint:#eef2ff}
@media (prefers-color-scheme:dark){:root{--bg:#0f1115;--card:#181b21;--ink:#e8eaee;--mute:#9aa1ad;--line:#2a2e36;--acc:#60a5fa;--ok:#4ade80;--bad:#f87171;--tint:#1c2433}}
[hidden]{display:none!important}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:720px;margin:0 auto;padding:12px 12px calc(20px + env(safe-area-inset-bottom))}
header{display:flex;align-items:baseline;justify-content:space-between;gap:10px;margin:4px 4px 10px}
h1{font-size:20px;margin:0}
header a{color:var(--acc);font-size:14px;text-decoration:none;white-space:nowrap}
.view{position:relative;background:#000;border-radius:14px;overflow:hidden;aspect-ratio:4/3;display:grid;place-items:center}
.view img{width:100%;height:100%;object-fit:contain;display:block}
.overlay{position:absolute;inset:0;display:grid;place-items:center;color:#cbd5e1;font-size:15px;text-align:center;padding:20px;background:#000}
.badge{position:absolute;left:10px;top:10px;background:rgba(0,0,0,.55);color:#fff;font:12px/1 ui-monospace,monospace;padding:6px 8px;border-radius:7px}
.live{position:absolute;right:10px;top:10px;display:flex;align-items:center;gap:6px;background:rgba(0,0,0,.55);color:#fff;font-size:12px;font-weight:600;padding:5px 8px;border-radius:7px}
.live i{width:8px;height:8px;border-radius:50%;background:#ef4444;animation:p 1.4s infinite}
@keyframes p{50%{opacity:.25}}
.bar{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin:10px 0}
button,.btn{font:inherit;font-weight:600;padding:12px;border-radius:10px;border:1px solid var(--line);background:var(--card);color:var(--ink);text-align:center;text-decoration:none;cursor:pointer}
button.primary{background:var(--acc);border-color:var(--acc);color:#fff}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px;margin-bottom:12px}
h2{font-size:13px;letter-spacing:.06em;text-transform:uppercase;color:var(--mute);margin:0 0 10px}
.seg{display:grid;grid-template-columns:repeat(3,1fr);gap:6px}
.seg button{padding:9px 4px;font-size:14px;font-weight:500}
.seg button.on{background:var(--tint);border-color:var(--acc);color:var(--acc);font-weight:700}
.row{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:8px 0}
.row+.row{border-top:1px solid var(--line)}
.sw{position:relative;width:48px;height:28px;flex:none}
.sw input{opacity:0;width:0;height:0}
.sw span{position:absolute;inset:0;background:var(--line);border-radius:14px;transition:.15s}
.sw span::after{content:"";position:absolute;left:3px;top:3px;width:22px;height:22px;border-radius:50%;background:#fff;transition:.15s}
.sw input:checked+span{background:var(--acc)}.sw input:checked+span::after{transform:translateX(20px)}
input[type=range]{width:55%;accent-color:var(--acc)}
.err{color:var(--bad)}.mute{color:var(--mute);font-size:14px}
.view:fullscreen{border-radius:0;aspect-ratio:auto}
</style></head><body><main>
<header><h1>Camera</h1><a href="/update">Firmware update &rarr;</a></header>
<div class="view" id="view">
  <img id="cam" alt="Live camera view">
  <div class="overlay" id="ov">Connecting to the camera…</div>
  <div class="badge" id="badge" hidden></div>
  <div class="live" id="live" hidden><i></i>LIVE</div>
</div>
<div class="bar">
  <button class="primary" id="snap">Snapshot</button>
  <button id="full">Full screen</button>
</div>
<div class="card"><h2>Resolution</h2><div class="seg" id="sizes"></div>
  <p class="mute" style="margin:10px 0 0">Larger sizes look sharper but update less often over Wi-Fi.</p></div>
<div class="card"><h2>Picture</h2>
  <div class="row"><span>Flip vertically</span><label class="sw"><input type="checkbox" id="vflip"><span></span></label></div>
  <div class="row"><span>Mirror</span><label class="sw"><input type="checkbox" id="mirror"><span></span></label></div>
  <div class="row"><span>Brightness</span><input type="range" id="bright" min="-2" max="2" step="1"></div>
  <div class="row"><span>JPEG quality</span><input type="range" id="quality" min="6" max="30" step="1" style="direction:rtl"></div>
</div>
<p class="mute" id="foot" style="text-align:center"></p>
</main><script>
const $=id=>document.getElementById(id);let st=null,retry=null,streamUrl='';
async function getCam(){const r=await fetch('/api/cam',{cache:'no-store'});return r.json()}
async function setCam(o){const r=await fetch('/api/cam',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(o)});st=await r.json();render()}
function render(){if(!st)return;
  $('sizes').innerHTML='';st.sizes.forEach(s=>{const b=document.createElement('button');b.textContent=s.label;b.className=s.id===st.size?'on':'';b.onclick=()=>setCam({size:s.id});$('sizes').appendChild(b)});
  $('vflip').checked=st.vflip;$('mirror').checked=st.mirror;$('bright').value=st.brightness;$('quality').value=st.quality;
  const lbl=(st.sizes.find(s=>s.id===st.size)||{}).label||'';
  $('badge').textContent=st.sensor+' · '+lbl+(st.fps>0?' · '+st.fps.toFixed(1)+' fps':'');$('badge').hidden=!st.ok;
  $('foot').textContent=st.fw+(st.viewers>1?' · '+st.viewers+' viewers':'')}
function startStream(){clearTimeout(retry);$('cam').src=streamUrl+'&n='+Date.now()}
function watch(){const img=$('cam');setInterval(()=>{const up=img.naturalWidth>0&&st&&st.ok;$('ov').hidden=up;$('live').hidden=!up},500)}
$('cam').onerror=()=>{if(!st||!st.ok)return;$('ov').hidden=false;$('ov').textContent='Stream interrupted. Reconnecting…';retry=setTimeout(startStream,2000)};
$('vflip').onchange=e=>setCam({vflip:e.target.checked?1:0});
$('mirror').onchange=e=>setCam({mirror:e.target.checked?1:0});
$('bright').onchange=e=>setCam({brightness:e.target.value});
$('quality').onchange=e=>setCam({quality:e.target.value});
$('snap').onclick=()=>{const a=document.createElement('a');a.href='/capture?n='+Date.now();a.download='snapshot-'+new Date().toISOString().slice(0,19).replace(/[:T]/g,'-')+'.jpg';document.body.appendChild(a);a.click();a.remove()};
$('full').onclick=()=>{const v=$('view');if(document.fullscreenElement)document.exitFullscreen();else (v.requestFullscreen||v.webkitRequestFullscreen||(()=>{})).call(v)};
(async()=>{try{st=await getCam()}catch(e){$('ov').textContent='Couldn’t reach the board.';return}
  render();
  if(!st.ok){$('ov').innerHTML='<div><b class="err">'+st.error.replace(/[<>&]/g,'')+'</b><br><br>Check the camera ribbon cable is seated (contacts facing the right way), then reboot the board from the <a href="/update" style="color:#93c5fd">update page</a>.</div>';return}
  streamUrl='http://'+location.hostname+':'+st.port+'/stream?t='+st.token;startStream();watch();
  setInterval(async()=>{try{st=Object.assign(st,await getCam());render()}catch(e){}},4000)})();
</script></body></html>)HTML";
