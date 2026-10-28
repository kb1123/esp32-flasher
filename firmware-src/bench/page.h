#pragma once
#include <Arduino.h>

static const char PAGE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Bench</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--ink:#16181d;--mute:#6b7280;--line:#e3e5e9;--acc:#2563eb;--ok:#15803d;--bad:#b91c1c;--tint:#eef2ff;--q:rgba(37,99,235,.13);--f:rgba(217,119,6,.16);--fl:#d97706;--stop:#dc2626}
@media (prefers-color-scheme:dark){:root{--bg:#0f1115;--card:#181b21;--ink:#e8eaee;--mute:#9aa1ad;--line:#2a2e36;--acc:#60a5fa;--ok:#4ade80;--bad:#f87171;--tint:#1c2433;--q:rgba(96,165,250,.16);--f:rgba(251,191,36,.15);--fl:#fbbf24;--stop:#ef4444}}
[hidden]{display:none!important}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:720px;margin:0 auto;padding:10px 12px calc(20px + env(safe-area-inset-bottom))}
header{position:sticky;top:0;z-index:5;background:var(--bg);display:flex;align-items:center;gap:10px;padding:6px 0 10px}
.tabs{flex:1;display:grid;grid-template-columns:1fr 1fr 1fr;gap:4px;background:var(--card);border:1px solid var(--line);border-radius:12px;padding:4px}
.tabs button{border:0;background:transparent;padding:9px 6px;border-radius:9px;font-weight:600;color:var(--mute)}
.tabs button.on{background:var(--tint);color:var(--acc)}
header a{color:var(--acc);font-size:14px;text-decoration:none;white-space:nowrap;padding:6px 2px}
button{font:inherit;font-weight:600;padding:12px;border-radius:10px;border:1px solid var(--line);background:var(--card);color:var(--ink);cursor:pointer;-webkit-tap-highlight-color:transparent}
button:disabled{opacity:.45;cursor:default}
button.primary{background:var(--acc);border-color:var(--acc);color:#fff}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px;margin-bottom:12px}
h2{font-size:13px;letter-spacing:.06em;text-transform:uppercase;color:var(--mute);margin:0}
.head{display:flex;justify-content:space-between;align-items:baseline;gap:10px;margin-bottom:10px}
.val{font-weight:700;font-variant-numeric:tabular-nums}
.mute{color:var(--mute);font-size:14px}
.err{color:var(--bad)}
.seg{display:grid;grid-template-columns:repeat(3,1fr);gap:6px}
.seg.two{grid-template-columns:1fr 1fr}
.seg button{padding:9px 4px;font-size:14px;font-weight:500;line-height:1.25}
.seg button small{display:block;font-size:12px;color:var(--mute);font-weight:400}
.seg button.on{background:var(--tint);border-color:var(--acc);color:var(--acc);font-weight:700}
.seg button.on small{color:var(--acc)}
input[type=range]{width:100%;accent-color:var(--acc);margin:6px 0 0}
.row{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:8px 0}
.row+.row{border-top:1px solid var(--line)}
.row input[type=range]{width:55%;margin:0}
/* camera */
.view{position:relative;background:#000;border-radius:14px;overflow:hidden;aspect-ratio:4/3;display:grid;place-items:center}
.view img{width:100%;height:100%;object-fit:contain;display:block}
.overlay{position:absolute;inset:0;display:grid;place-items:center;color:#cbd5e1;font-size:15px;text-align:center;padding:20px;background:#000}
.overlay button{margin-top:14px;background:#1f2937;border-color:#374151;color:#e5e7eb}
.badge{position:absolute;left:10px;top:10px;background:rgba(0,0,0,.55);color:#fff;font:12px/1 ui-monospace,monospace;padding:6px 8px;border-radius:7px}
.live{position:absolute;right:10px;top:10px;display:flex;align-items:center;gap:6px;background:rgba(0,0,0,.55);color:#fff;font-size:12px;font-weight:600;padding:5px 8px;border-radius:7px}
.live i{width:8px;height:8px;border-radius:50%;background:#ef4444;animation:p 1.4s infinite}
@keyframes p{50%{opacity:.25}}
.bar{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin:10px 0}
.sw{position:relative;width:48px;height:28px;flex:none}
.sw input{opacity:0;width:0;height:0}
.sw span{position:absolute;inset:0;background:var(--line);border-radius:14px;transition:.15s}
.sw span::after{content:"";position:absolute;left:3px;top:3px;width:22px;height:22px;border-radius:50%;background:#fff;transition:.15s}
.sw input:checked+span{background:var(--acc)}.sw input:checked+span::after{transform:translateX(20px)}
.view:fullscreen{border-radius:0;aspect-ratio:auto}
/* motor */
.hero{padding-bottom:10px}
.readout{display:flex;align-items:baseline;gap:8px}
.readout b{font-size:52px;line-height:1;font-weight:750;font-variant-numeric:tabular-nums;letter-spacing:-.02em}
.readout span{color:var(--mute);font-weight:600}
.state{display:flex;flex-wrap:wrap;align-items:center;gap:6px 10px;margin:8px 0 10px;font-size:14px}
.pill{display:inline-flex;align-items:center;gap:6px;padding:3px 9px;border-radius:999px;font-weight:650;font-size:13px}
.pill.q{background:var(--q);color:var(--acc)}.pill.f{background:var(--f);color:var(--fl)}
canvas{display:block;width:100%;height:150px}
.legend{display:flex;flex-wrap:wrap;gap:4px 14px;font-size:12px;color:var(--mute);margin-top:6px}
.legend i{display:inline-block;width:12px;height:10px;border-radius:3px;vertical-align:-1px;margin-right:5px}
.go{width:100%;font-size:18px;padding:15px;margin-bottom:12px;border:0;color:#fff;background:var(--acc)}
.go.stop{background:var(--stop)}
.go.wait{background:var(--line);color:var(--mute)}
.num{display:grid;grid-template-columns:48px 1fr 48px;gap:8px;align-items:center}
.num button{padding:10px 0;font-size:20px;line-height:1}
.num label{display:flex;align-items:baseline;justify-content:center;gap:6px;border:1px solid var(--line);border-radius:10px;padding:6px 10px;background:var(--bg)}
.num input{width:5ch;font:700 26px/1.1 system-ui,sans-serif;text-align:right;border:0;background:transparent;color:var(--ink);font-variant-numeric:tabular-nums;padding:0}
.num input:focus{outline:none}
.chips{display:flex;flex-wrap:wrap;gap:6px;margin-top:10px}
.chips button{padding:6px 11px;font-size:13px;font-weight:500;border-radius:999px}
details.card summary{cursor:pointer;font-weight:650;list-style:none;display:flex;justify-content:space-between}
details.card summary::after{content:"▾";color:var(--mute)}
details.card[open] summary::after{content:"▴"}
details.card[open] summary{margin-bottom:10px}
.field{padding:10px 0;border-top:1px solid var(--line)}
.field>div:first-child{font-size:14px;font-weight:600;margin-bottom:8px}
/* signals */
.ohead{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:10px}
.ohead b{font-size:18px}
.tag{display:inline-block;font:600 12px/1 ui-monospace,monospace;background:var(--tint);color:var(--acc);padding:5px 7px;border-radius:6px;margin-left:8px;vertical-align:2px}
.out.off .body{opacity:.45}
.prev{height:96px;margin-bottom:10px;background:var(--bg);border-radius:10px}
.seg.five{grid-template-columns:repeat(5,1fr)}
.seg.five button{padding:9px 2px;font-size:13px}
.ctl{display:grid;grid-template-columns:86px 1fr 74px;align-items:center;gap:8px;padding:6px 0}
.ctl span:first-child{font-size:14px;color:var(--mute)}
.ctl input[type=range]{margin:0}
.ctl .v{text-align:right;font-weight:650;font-variant-numeric:tabular-nums;font-size:14px}
.hz{display:flex;align-items:baseline;gap:4px;justify-content:flex-end}
.hz input{width:100%;min-width:0;text-align:right;font:650 14px system-ui,sans-serif;border:1px solid var(--line);border-radius:7px;padding:4px 6px;background:var(--bg);color:var(--ink)}
.onote{font-size:13px;color:var(--mute);margin:6px 0 0}
.onote.warn{color:var(--bad)}
.blocked{font-size:14px;background:var(--tint);border-radius:10px;padding:10px 12px;margin-top:4px}
table.pins{width:100%;border-collapse:collapse;font-size:14px;margin:4px 0 8px}
table.pins td{padding:6px 4px;border-top:1px solid var(--line)}
table.pins td:first-child{font-weight:700}
.toast{position:fixed;left:50%;bottom:calc(18px + env(safe-area-inset-bottom));transform:translateX(-50%);background:#111827;color:#f9fafb;padding:10px 14px;border-radius:10px;font-size:14px;max-width:calc(100% - 32px);box-shadow:0 6px 20px rgba(0,0,0,.25);z-index:20}
.cover{position:fixed;inset:0;background:rgba(0,0,0,.6);display:grid;place-items:center;z-index:30;color:#fff;text-align:center;padding:24px;font-size:17px}
</style></head><body><main>
<header>
  <nav class="tabs" role="tablist">
    <button id="tb-camera" data-tab="camera" role="tab">Camera</button>
    <button id="tb-motor" data-tab="motor" role="tab">Motor</button>
    <button id="tb-signals" data-tab="signals" role="tab">Signals</button>
  </nav>
  <a href="/update">Firmware&nbsp;&rarr;</a>
</header>

<section id="tab-camera" hidden>
  <div class="view" id="view">
    <img id="cam" alt="Live camera view">
    <div class="overlay" id="ov">Connecting to the camera…</div>
    <div class="badge" id="badge" hidden></div>
    <div class="live" id="live" hidden><i></i>LIVE</div>
  </div>
  <div id="camControls">
    <div class="bar"><button class="primary" id="snap">Snapshot</button><button id="full">Full screen</button></div>
    <div class="card"><div class="head"><h2>Resolution</h2></div><div class="seg" id="sizes"></div>
      <p class="mute" style="margin:10px 0 0">Larger sizes look sharper but update less often over Wi-Fi.</p></div>
    <div class="card"><div class="head"><h2>Picture</h2></div>
      <div class="row"><span>Flip vertically</span><label class="sw"><input type="checkbox" id="vflip"><span></span></label></div>
      <div class="row"><span>Mirror</span><label class="sw"><input type="checkbox" id="mirror"><span></span></label></div>
      <div class="row"><span>Brightness</span><input type="range" id="bright" min="-2" max="2" step="1"></div>
      <div class="row"><span>JPEG quality</span><input type="range" id="quality" min="6" max="30" step="1" style="direction:rtl"></div>
    </div>
  </div>
</section>

<section id="tab-motor" hidden>
  <div class="card hero">
    <div class="readout"><b id="rpm">0</b><span>rpm</span></div>
    <div class="state"><span class="pill" id="modePill">–</span><span id="stateText" class="mute"></span></div>
    <canvas id="chart" aria-label="Speed over the last 30 seconds"></canvas>
    <div class="legend"><span><i style="background:var(--acc)"></i>measured</span><span><i style="background:var(--mute);height:2px;vertical-align:3px"></i>commanded</span><span><i style="background:var(--q)"></i>¼ step</span><span><i style="background:var(--f)"></i>full step</span></div>
  </div>
  <button class="go" id="go">Start</button>

  <div class="card">
    <div class="head"><h2>Speed</h2><span class="mute" id="pulse"></span></div>
    <div class="num"><button id="minus" aria-label="Slower">−</button>
      <label><input id="speedIn" inputmode="numeric" pattern="[0-9]*" aria-label="Speed in rpm"><span class="mute">rpm</span></label>
      <button id="plus" aria-label="Faster">+</button></div>
    <input type="range" id="speed" min="20" max="1000" step="10">
    <div class="chips" id="speedChips"></div>
  </div>

  <div class="card">
    <div class="head"><h2>Acceleration limit</h2><span class="val" id="accVal"></span></div>
    <input type="range" id="acc" min="0" max="1000" step="1">
    <p class="mute" id="accHint" style="margin:6px 0 0"></p>
  </div>

  <div class="card">
    <div class="head"><h2>Microstepping</h2></div>
    <div class="seg" id="msSeg">
      <button data-ms="off">Off<small>full step</small></button>
      <button data-ms="on">On<small>¼ step</small></button>
      <button data-ms="auto">Auto<small>switch by speed</small></button>
    </div>
    <div id="autoBox" hidden style="margin-top:12px">
      <div class="head" style="margin-bottom:0"><span>Full step at or above</span><span class="val" id="autoVal"></span></div>
      <input type="range" id="autoRpm" min="20" max="1000" step="5">
      <p class="mute" style="margin:6px 0 0">Drops back to ¼ step below 95% of that speed, so it doesn't flip-flop.</p>
    </div>
    <p class="mute" id="swInfo" style="margin:12px 0 0"></p>
  </div>

  <details class="card" id="wiring"><summary>Wiring &amp; motor</summary>
    <div class="field"><div>STEP pin</div>
      <div class="seg two" id="pinSeg">
        <button data-pin="3">IO3<small>header pin 14</small></button>
        <button data-pin="17">IO17<small>header pin 7</small></button>
      </div>
      <p class="mute" style="margin:8px 0 0">IO3 is also the camera's clock, so the camera is off while STEP uses it. IO17 is also signal output OUT3. So: STEP on IO3 = motor + 3 signal outputs, no camera; STEP on IO17 = motor + camera + OUT1/OUT2. Changing this reboots the board.</p>
    </div>
    <div class="field"><div>MS2 pin</div><span class="mute">IO16 (header pin 9): high = ¼ step, low = full step. MS1 and MS3 stay unconnected.</span></div>
    <div class="field"><div>Motor steps per revolution</div>
      <div class="seg two" id="sprSeg">
        <button data-spr="200">200<small>1.8° motor</small></button>
        <button data-spr="400">400<small>0.9° motor</small></button>
      </div>
    </div>
    <div class="field" style="padding-bottom:0"><span class="mute">Direction is fixed (DIR isn't connected). Firmware updates and Wi-Fi changes are refused while the motor turns, because writing to flash pauses the step pulses.</span></div>
  </details>
  <p class="mute" id="foot" style="text-align:center"></p>
</section>
<section id="tab-signals" hidden>
  <div class="card"><div class="head"><h2>Quick setups</h2></div>
    <div class="chips" id="presets">
      <button data-p="3phase">3-phase sine, 50 Hz</button>
      <button data-p="quadrature">Sine + cosine, 1 kHz</button>
      <button data-p="test1k">1 kHz sine / square / triangle</button>
      <button data-p="off">All off</button>
    </div>
    <div class="bar" style="margin:12px 0 0"><button id="sigSync">Re-sync phases</button><button id="sigSave">Save as power-on setup</button></div>
  </div>
  <div id="outs"></div>
  <details class="card" id="sigHelp"><summary>Wiring &amp; filtering</summary>
    <table class="pins"><tbody id="pinTable"></tbody></table>
    <p class="mute" style="margin:0 0 8px">The ESP32-S3 has no DAC. Sine, triangle, sawtooth and DC come out as a 312.5 kHz PWM signal whose average is the waveform, updated 31,250 times a second. One resistor and one capacitor per output turn it into a smooth voltage:</p>
    <p style="margin:0 0 8px;font-size:14px"><b>pin → 1 kΩ → signal out, with 33 nF from signal out to GND.</b> That passes 1 kHz with about 2% loss and leaves roughly 0.08 V of carrier ripple. Two of these in series (1 kΩ / 33 nF twice) bring the ripple down to a few millivolts.</p>
    <p class="mute" style="margin:0 0 8px">Square waves come from a hardware timer as a clean 0–3.3 V logic signal with exact timing (5 Hz to 20 kHz). Take them straight from the pin, because the filter would round off the edges. Below 5 Hz squares are generated in software, with edges within 32 µs.</p>
    <p class="mute" style="margin:0">Outputs swing 0–3.3 V only (no negative voltage) and can source a few mA; buffer with an op-amp to drive real loads. Saving the power-on setup pauses the outputs for a few milliseconds. IO43 flickers briefly with the boot message at power-up.</p>
  </details>
</section>
</main>
<script>
const $=id=>document.getElementById(id);
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
let tab='camera',cam=null,mot=null,retry=null,streamUrl='',motorTimer=null,camTimer=null;
const touched={};const touch=k=>touched[k]=Date.now();const busyEdit=k=>Date.now()-(touched[k]||0)<1500;
function toast(t){const d=document.createElement('div');d.className='toast';d.textContent=t;document.body.appendChild(d);setTimeout(()=>d.remove(),4000)}
async function post(url,o){const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(o)});let j={};try{j=await r.json()}catch(e){}if(!r.ok){toast(j.error||('Error '+r.status));return null}return j}
function store(k,v){try{localStorage.setItem(k,v)}catch(e){}}
function recall(k){try{return localStorage.getItem(k)}catch(e){return null}}

// ---------------------------------------------------------------- tabs
function show(t){tab=t;store('tab',t);
  for(const x of ['camera','motor','signals']){$('tab-'+x).hidden=x!==t;$('tb-'+x).classList.toggle('on',x===t);$('tb-'+x).setAttribute('aria-selected',x===t)}
  if(t!=='motor'){clearInterval(motorTimer);motorTimer=null}
  if(t==='camera')startStream();else stopStream();
  if(t==='motor'){if(!motorTimer){pollMotor();motorTimer=setInterval(pollMotor,250)}requestAnimationFrame(draw)}
  if(t==='signals')pollSig();
  history.replaceState(null,'','#'+t)}
document.querySelectorAll('.tabs button').forEach(b=>b.onclick=()=>show(b.dataset.tab));

// ---------------------------------------------------------------- camera
async function getCam(){return (await fetch('/api/cam',{cache:'no-store'})).json()}
async function setCam(o){const j=await post('/api/cam',o);if(j){cam=j;renderCam()}}
function renderCam(){if(!cam)return;
  $('sizes').innerHTML='';cam.sizes.forEach(s=>{const b=document.createElement('button');b.textContent=s.label;b.className=s.id===cam.size?'on':'';b.onclick=()=>setCam({size:s.id});$('sizes').appendChild(b)});
  $('vflip').checked=cam.vflip;$('mirror').checked=cam.mirror;$('bright').value=cam.brightness;$('quality').value=cam.quality;
  const lbl=(cam.sizes.find(s=>s.id===cam.size)||{}).label||'';
  $('badge').textContent=cam.sensor+' · '+lbl+(cam.fps>0?' · '+cam.fps.toFixed(1)+' fps':'');$('badge').hidden=!cam.ok;
  $('camControls').hidden=!cam.ok;
  if(!cam.enabled){$('ov').hidden=false;$('ov').innerHTML='<div><b>Camera off</b><br><br>The motor’s STEP signal is on IO3, which is also the camera’s clock pin. Move the STEP wire to IO17 (header pin 7) and select IO17 under Motor → Wiring to use both.<br><button id="toWiring">Open motor wiring</button></div>';$('toWiring').onclick=()=>{show('motor');$('wiring').open=true;$('wiring').scrollIntoView({behavior:'smooth'})}}
  else if(!cam.ok){$('ov').hidden=false;$('ov').innerHTML='<div><b class="err">'+esc(cam.error)+'</b><br><br>Check the camera ribbon cable is seated (contacts facing the right way), then reboot the board from the <a href="/update" style="color:#93c5fd">firmware page</a>.</div>'}}
function startStream(){clearTimeout(retry);if(!cam||!cam.ok||tab!=='camera')return;$('ov').hidden=false;$('ov').textContent='Connecting to the camera…';$('cam').src=streamUrl+'&n='+Date.now()}
function stopStream(){clearTimeout(retry);$('cam').src='data:,';$('live').hidden=true}
setInterval(()=>{if(tab!=='camera'||!cam||!cam.ok)return;const up=$('cam').naturalWidth>1;$('ov').hidden=up;$('live').hidden=!up},500);
$('cam').onerror=()=>{if(tab!=='camera'||!cam||!cam.ok)return;$('ov').hidden=false;$('ov').textContent='Stream interrupted. Reconnecting…';retry=setTimeout(startStream,2000)};
$('vflip').onchange=e=>setCam({vflip:e.target.checked?1:0});
$('mirror').onchange=e=>setCam({mirror:e.target.checked?1:0});
$('bright').onchange=e=>setCam({brightness:e.target.value});
$('quality').onchange=e=>setCam({quality:e.target.value});
$('snap').onclick=()=>{const a=document.createElement('a');a.href='/capture?n='+Date.now();a.download='snapshot-'+new Date().toISOString().slice(0,19).replace(/[:T]/g,'-')+'.jpg';document.body.appendChild(a);a.click();a.remove()};
$('full').onclick=()=>{const v=$('view');if(document.fullscreenElement)document.exitFullscreen();else (v.requestFullscreen||v.webkitRequestFullscreen||(()=>{})).call(v)};

// ---------------------------------------------------------------- motor
const hist=[];const HIST_S=30;
const accToPos=v=>Math.round(Math.log10(v/10)/3*1000), posToAcc=p=>{const v=10*Math.pow(1000,p/1000);const m=Math.pow(10,Math.floor(Math.log10(v))-1);return Math.round(v/m)*m};
const fmt=n=>Math.round(n).toLocaleString();
function modeName(m){return m==='quarter'?'¼ step':'full step'}
function renderMotor(){const m=mot;if(!m)return;
  $('rpm').textContent=fmt(m.running?m.measured:0);
  const p=$('modePill');p.className='pill '+(m.mode==='quarter'?'q':'f');p.textContent=modeName(m.mode)+(m.pending&&m.running?' → '+modeName(m.mode==='quarter'?'full':'quarter'):'');
  $('stateText').textContent=m.running?(m.run?(Math.abs(m.rpm-m.speed)<0.5?'holding '+fmt(m.speed)+' rpm':(m.rpm<m.speed?'speeding up':'slowing down')+' to '+fmt(m.speed)):'stopping'):'stopped';
  const g=$('go');g.className='go'+(m.run?' stop':m.running?' wait':'');g.textContent=m.run?'Stop':m.running?'Stopping…':'Start';
  $('pulse').textContent=m.running?fmt(m.pulseHz)+' steps/s':'';
  if(!busyEdit('speed')){$('speed').value=m.speed;if(document.activeElement!==$('speedIn'))$('speedIn').value=m.speed}
  if(!busyEdit('acc')){$('acc').value=accToPos(m.accel);setAccLabel(m.accel)}
  if(!busyEdit('auto')){$('autoRpm').value=m.autoRpm;$('autoVal').textContent=fmt(m.autoRpm)+' rpm'}
  document.querySelectorAll('#msSeg button').forEach(b=>b.classList.toggle('on',b.dataset.ms===m.ms));
  $('autoBox').hidden=m.ms!=='auto';
  document.querySelectorAll('#pinSeg button').forEach(b=>b.classList.toggle('on',+b.dataset.pin===m.stepPin));
  document.querySelectorAll('#sprSeg button').forEach(b=>{b.classList.toggle('on',+b.dataset.spr===m.spr);b.disabled=m.running||m.run});
  document.querySelectorAll('#pinSeg button').forEach(b=>b.disabled=m.running||m.run);
  let s='';if(m.last){const ago=m.last.ago<60000?Math.round(m.last.ago/1000)+' s ago':Math.round(m.last.ago/60000)+' min ago';
    s='Last switch: '+(m.last.to==='full'?'¼ → full':'full → ¼')+' at <b>'+fmt(m.last.rpm)+' rpm</b>, '+ago+'. '}
  s+=m.switches?m.switches+' switch'+(m.switches>1?'es':'')+' since boot.':'No switches yet.';$('swInfo').innerHTML=s;
  $('foot').textContent=m.fw}
function setAccLabel(a){$('accVal').textContent=fmt(a)+' rpm/s';const t=(1000-20)/a;$('accHint').textContent='20 → 1000 rpm takes '+(t<10?t.toFixed(t<1?2:1):Math.round(t))+' s at this limit.'}
async function pollMotor(){try{mot=await (await fetch('/api/motor',{cache:'no-store'})).json();
  hist.push({t:Date.now(),v:mot.running?mot.measured:0,c:mot.running?mot.rpm:0,m:mot.mode,on:mot.running});
  while(hist.length&&hist[0].t<Date.now()-HIST_S*1000)hist.shift();renderMotor();draw()}catch(e){}}
async function setMotor(o){const j=await post('/api/motor',o);if(j){mot=j;renderMotor()}}
$('go').onclick=()=>{if(!mot)return;setMotor({run:mot.run?0:1})};
function setSpeed(v){v=Math.max(20,Math.min(1000,Math.round(v)));$('speed').value=v;$('speedIn').value=v;touch('speed');setMotor({speed:v})}
$('speed').oninput=e=>{touch('speed');$('speedIn').value=e.target.value};
$('speed').onchange=e=>setSpeed(+e.target.value);
$('speedIn').onchange=e=>setSpeed(+e.target.value||20);
$('speedIn').onkeydown=e=>{if(e.key==='Enter')e.target.blur()};
$('minus').onclick=()=>setSpeed(+$('speedIn').value-10);
$('plus').onclick=()=>setSpeed(+$('speedIn').value+10);
[60,120,300,500,750,1000].forEach(v=>{const b=document.createElement('button');b.textContent=v;b.onclick=()=>setSpeed(v);$('speedChips').appendChild(b)});
$('acc').oninput=e=>{touch('acc');setAccLabel(posToAcc(+e.target.value))};
$('acc').onchange=e=>{touch('acc');setMotor({accel:posToAcc(+e.target.value)})};
$('autoRpm').oninput=e=>{touch('auto');$('autoVal').textContent=fmt(e.target.value)+' rpm'};
$('autoRpm').onchange=e=>{touch('auto');setMotor({autoRpm:e.target.value})};
document.querySelectorAll('#msSeg button').forEach(b=>b.onclick=()=>setMotor({ms:b.dataset.ms}));
document.querySelectorAll('#sprSeg button').forEach(b=>b.onclick=()=>setMotor({spr:b.dataset.spr}));
document.querySelectorAll('#pinSeg button').forEach(b=>b.onclick=async()=>{const pin=+b.dataset.pin;if(!mot||pin===mot.stepPin)return;
  if(!confirm('Use IO'+pin+' for STEP? The board reboots to apply it'+(pin===3?'; the camera turns off and signal output OUT3 becomes available.':'; the camera comes back on and signal output OUT3 is disabled (it uses IO17).')))return;
  const j=await post('/api/motor/pin',{pin});if(!j)return;
  const c=document.createElement('div');c.className='cover';c.textContent='Rebooting with STEP on IO'+pin+'…';document.body.appendChild(c);
  await new Promise(r=>setTimeout(r,4000));for(let i=0;i<40;i++){try{const r=await fetch('/api/motor',{cache:'no-store'});if(r.ok){location.reload();return}}catch(e){}await new Promise(r=>setTimeout(r,1500))}
  c.textContent='The board hasn’t come back yet. Check you’re still on its hotspot, then reload.'});

// ---------------------------------------------------------------- chart
function draw(){const cv=$('chart');if(tab!=='motor'||!cv.offsetWidth)return;
  const dpr=window.devicePixelRatio||1,W=cv.offsetWidth,H=cv.offsetHeight;
  if(cv.width!==Math.round(W*dpr)||cv.height!==Math.round(H*dpr)){cv.width=Math.round(W*dpr);cv.height=Math.round(H*dpr)}
  const x=cv.getContext('2d');x.setTransform(dpr,0,0,dpr,0,0);x.clearRect(0,0,W,H);
  const cs=getComputedStyle(document.documentElement),col=n=>cs.getPropertyValue(n).trim();
  const now=Date.now(),t0=now-HIST_S*1000,L=34,R=4,T=6,B=16,pw=W-L-R,ph=H-T-B;
  let ymax=Math.max(100,mot?mot.speed:0,...hist.map(h=>h.v),...hist.map(h=>h.c));ymax=Math.ceil(ymax/100)*100;
  const X=t=>L+(t-t0)/(HIST_S*1000)*pw,Y=v=>T+ph-(v/ymax)*ph;
  let segS=null,segM=null;const flush=e=>{if(segS!==null){x.fillStyle=col(segM==='quarter'?'--q':'--f');const a=Math.floor(Math.max(L,segS));x.fillRect(a,T,Math.max(1,Math.ceil(e)-a),ph)}segS=null};
  for(let i=0;i<hist.length;i++){const h=hist[i];const st=X(i?hist[i-1].t:h.t-250);if(!h.on||h.m!==segM){flush(st);segM=h.m}if(h.on&&segS===null)segS=st;if(!h.on)segM=null}flush(hist.length?X(hist[hist.length-1].t):0)
  x.strokeStyle=col('--line');x.lineWidth=1;x.fillStyle=col('--mute');x.font='11px system-ui,sans-serif';x.textAlign='right';x.textBaseline='middle';
  for(const v of [0,ymax/2,ymax]){x.beginPath();x.moveTo(L,Y(v)+.5);x.lineTo(W-R,Y(v)+.5);x.stroke();x.fillText(fmt(v),L-5,Y(v))}
  x.textAlign='left';x.textBaseline='alphabetic';x.fillText('−30 s',L,H-2);x.textAlign='right';x.fillText('now',W-R,H-2);
  if(mot&&mot.ms==='auto'){x.setLineDash([2,4]);x.strokeStyle=col('--fl');x.beginPath();x.moveTo(L,Y(mot.autoRpm)+.5);x.lineTo(W-R,Y(mot.autoRpm)+.5);x.stroke();x.setLineDash([])}
  const line=(k,c,w,dash)=>{x.strokeStyle=c;x.lineWidth=w;x.setLineDash(dash);x.lineJoin='round';x.beginPath();hist.forEach((h,i)=>{const px=X(h.t),py=Y(h[k]);i?x.lineTo(px,py):x.moveTo(px,py)});x.stroke();x.setLineDash([])};
  if(hist.length>1){line('c',col('--mute'),1.5,[5,4]);line('v',col('--acc'),2.5,[])}}
window.addEventListener('resize',draw);

// ---------------------------------------------------------------- signals
let sig=null;const WAVES=[['sine','Sine'],['triangle','Triangle'],['saw','Saw'],['square','Square'],['dc','DC']];
const fmtHz=h=>h>=1000?(h/1000).toFixed(h>=10000?1:3).replace(/\.?0+$/,'')+' kHz':h>=10?(+h.toFixed(2))+' Hz':(+h.toFixed(3))+' Hz';
const hzToPos=(h,max)=>Math.round(Math.log10(h/0.1)/Math.log10(max/0.1)*1000),posToHz=(p,max)=>{const v=0.1*Math.pow(max/0.1,p/1000);const m=Math.pow(10,Math.floor(Math.log10(v))-2);return Math.round(v/m)*m};
function wv(w,ph,duty){ph=ph-Math.floor(ph);switch(w){case'sine':return Math.sin(2*Math.PI*ph);case'triangle':return ph<.5?ph*4-1:3-ph*4;case'saw':return ph*2-1;case'square':return ph<duty/100?1:-1;default:return 0}}
function buildOuts(){const host=$('outs');host.innerHTML='';sig.ch.forEach((c,i)=>{const d=document.createElement('div');d.className='card out';d.id='out'+i;
  d.innerHTML='<div class="ohead"><div><b>OUT'+c.id+'</b><span class="tag">IO'+c.pin+' · header pin '+c.header+'</span></div><label class="sw"><input type="checkbox" class="onT" aria-label="OUT'+c.id+' on"><span></span></label></div>'+
  '<div class="blocked" hidden>IO17 is wired as the motor’s STEP pin right now. To use OUT3, switch STEP back to IO3 under <a href="#" class="toMotor" style="color:var(--acc)">Motor → Wiring</a> (the camera turns off).</div>'+
  '<div class="body"><canvas class="prev"></canvas><div class="seg five">'+WAVES.map(w=>'<button data-w="'+w[0]+'">'+w[1]+'</button>').join('')+'</div>'+
  '<div class="ctl fq"><span>Frequency</span><input type="range" class="hzR" min="0" max="1000"><label class="hz"><input class="hzN" inputmode="decimal" aria-label="Frequency in hertz"><span class="mute">Hz</span></label></div>'+
  '<div class="ctl amp"><span>Amplitude</span><input type="range" class="vpp" min="0" max="3.3" step="0.01"><span class="v vppV"></span></div>'+
  '<div class="ctl mid"><span class="midL">Centre</span><input type="range" class="vmid" min="0" max="3.3" step="0.01"><span class="v vmidV"></span></div>'+
  '<div class="ctl ph"><span>Phase</span><input type="range" class="phase" min="0" max="359" step="1"><span class="v phaseV"></span></div>'+
  '<div class="ctl dt"><span>Duty</span><input type="range" class="duty" min="1" max="99" step="1"><span class="v dutyV"></span></div>'+
  '<p class="onote"></p></div>';
  host.appendChild(d);
  const q=k=>d.querySelector(k),send=o=>setSig(Object.assign({ch:c.id},o)),ed=()=>touch('sig'+i);
  q('.onT').onchange=e=>send({on:e.target.checked?1:0});
  d.querySelectorAll('.seg button').forEach(b=>b.onclick=()=>send({wave:b.dataset.w,on:1}));
  q('.hzR').oninput=e=>{ed();const c2=sig.ch[i];const h=posToHz(+e.target.value,maxFor(c2.wave));q('.hzN').value=+h.toFixed(3);local(i,{hz:h})};
  q('.hzR').onchange=e=>{ed();send({hz:posToHz(+e.target.value,maxFor(sig.ch[i].wave))})};
  q('.hzN').onchange=e=>{ed();send({hz:+e.target.value||1})};q('.hzN').onkeydown=e=>{if(e.key==='Enter')e.target.blur()};
  for(const [k,key] of [['vpp','vpp'],['vmid','vmid'],['phase','phase'],['duty','duty']]){q('.'+k).oninput=e=>{ed();local(i,{[key]:+e.target.value})};q('.'+k).onchange=e=>{ed();send({[key]:+e.target.value})}}
  q('.toMotor').onclick=e=>{e.preventDefault();show('motor');$('wiring').open=true;$('wiring').scrollIntoView({behavior:'smooth'})};
 });
 $('pinTable').innerHTML=sig.ch.map(c=>'<tr><td>OUT'+c.id+'</td><td>IO'+c.pin+'</td><td>header pin '+c.header+'</td><td class="mute">'+(c.available?'':'in use as STEP')+'</td></tr>').join('')+'<tr><td>GND</td><td colspan="3" class="mute">any GND pin on the header</td></tr>'}
const maxFor=w=>w==='square'?sig.squareMax:sig.analogMax;
function local(i,o){Object.assign(sig.ch[i],o);renderOut(i)}
function renderOut(i){const c=sig.ch[i],d=$('out'+i);if(!d)return;const q=k=>d.querySelector(k);const ed=busyEdit('sig'+i);
  d.classList.toggle('off',!c.on||!c.available);q('.blocked').hidden=c.available;q('.body').hidden=!c.available;q('.onT').disabled=!c.available;
  if(!ed)q('.onT').checked=c.on;
  d.querySelectorAll('.seg button').forEach(b=>b.classList.toggle('on',b.dataset.w===c.wave));
  const sq=c.wave==='square',dc=c.wave==='dc';
  q('.fq').hidden=dc;q('.amp').hidden=sq||dc;q('.mid').hidden=sq;q('.ph').hidden=sq||dc;q('.dt').hidden=!sq;
  q('.midL').textContent=dc?'Level':'Centre';
  if(!ed){q('.hzR').value=hzToPos(c.hz,maxFor(c.wave));if(document.activeElement!==q('.hzN'))q('.hzN').value=+(+c.hz).toFixed(3);
    q('.vpp').value=c.vpp;q('.vmid').value=c.vmid;q('.phase').value=c.phase;q('.duty').value=c.duty}
  q('.vppV').textContent=(+c.vpp).toFixed(2)+' Vpp';q('.vmidV').textContent=(+c.vmid).toFixed(2)+' V';q('.phaseV').textContent=Math.round(c.phase)+'°';q('.dutyV').textContent=Math.round(c.duty)+' %';
  const lo=c.vmid-c.vpp/2,hi=c.vmid+c.vpp/2,clip=!sq&&!dc&&(lo<-0.005||hi>sig.vmax+0.005);
  let n='';if(!c.on)n='Off: the pin sits at 0 V.';
  else if(sq)n=(c.hw?'Hardware square, exact timing: ':'Software square (below 5 Hz): ')+fmtHz(c.actualHz||c.hz)+', 0–3.3 V logic. Use the pin directly, no filter.';
  else if(dc)n='Steady '+(+c.vmid).toFixed(2)+' V after the RC filter.';
  else n=fmtHz(c.actualHz||c.hz)+', '+lo.toFixed(2)+' to '+hi.toFixed(2)+' V after the RC filter.';
  if(clip)n='Clipped: the wave would go outside 0–3.3 V. Lower the amplitude or move the centre.';
  const no=q('.onote');no.textContent=n;no.className='onote'+(clip&&c.on?' warn':'');
  drawPrev(q('.prev'),c)}
function drawPrev(cv,c){if(!cv.offsetWidth)return;const dpr=window.devicePixelRatio||1,W=cv.offsetWidth,H=cv.offsetHeight;
  if(cv.width!==Math.round(W*dpr)){cv.width=Math.round(W*dpr);cv.height=Math.round(H*dpr)}
  const x=cv.getContext('2d');x.setTransform(dpr,0,0,dpr,0,0);x.clearRect(0,0,W,H);
  const cs=getComputedStyle(document.documentElement),col=n=>cs.getPropertyValue(n).trim(),T=8,B=8,L=46,R=8,ph=H-T-B,Y=v=>T+ph-v/sig.vmax*ph;
  x.font='10px system-ui,sans-serif';x.fillStyle=col('--mute');x.textAlign='right';x.textBaseline='middle';x.strokeStyle=col('--line');x.lineWidth=1;
  for(const [v,t] of [[0,'0 V'],[1.65,'1.65 V'],[3.3,'3.3 V']]){x.beginPath();x.moveTo(L,Y(v)+.5);x.lineTo(W-R,Y(v)+.5);x.stroke();x.fillText(t,L-5,Y(v))}
  if(!c.on)return;
  const sq=c.wave==='square',amp=sq?sig.vmax:c.vpp,mid=sq?sig.vmax/2:c.vmid,N=Math.max(200,W);
  x.lineWidth=2;x.lineJoin='round';
  let prevClip=null;x.beginPath();
  for(let k=0;k<=N;k++){const t=k/N*2,raw=mid+amp/2*wv(c.wave,t+(sq?0:c.phase/360),c.duty),v=Math.min(sig.vmax,Math.max(0,raw)),clip=raw!==v,px=L+k/N*(W-L-R),py=Y(v);
    if(clip!==prevClip){if(k){x.lineTo(px,py);x.stroke()}x.beginPath();x.strokeStyle=col(clip?'--bad':'--acc');x.moveTo(px,py);prevClip=clip}else x.lineTo(px,py)}
  x.stroke()}
async function pollSig(){try{const j=await (await fetch('/api/sig',{cache:'no-store'})).json();const first=!sig||sig.ch.length!==j.ch.length||sig.ch.some((c,i)=>c.available!==j.ch[i].available);sig=j;if(first)buildOuts();sig.ch.forEach((_,i)=>renderOut(i))}catch(e){}}
async function setSig(o){const j=await post('/api/sig',o);if(j){sig=j;sig.ch.forEach((_,i)=>renderOut(i))}}
document.querySelectorAll('#presets button').forEach(b=>b.onclick=()=>setSig({action:'preset',name:b.dataset.p}));
$('sigSync').onclick=()=>setSig({action:'sync'}).then(()=>toast('Outputs restarted together at their phase offsets.'));
$('sigSave').onclick=async()=>{const j=await post('/api/sig',{action:'save'});if(j)toast('Saved: the outputs start like this after a reboot.')};
setInterval(()=>{if(tab==='signals')pollSig()},3000);
window.addEventListener('resize',()=>{if(sig&&tab==='signals')sig.ch.forEach((_,i)=>renderOut(i))});

// ---------------------------------------------------------------- start
(async()=>{try{cam=await getCam()}catch(e){cam=null}
  if(cam){renderCam();streamUrl='http://'+location.hostname+':'+cam.port+'/stream?t='+cam.token}
  const want=(location.hash||'').slice(1)||recall('tab');
  show(['motor','camera','signals'].includes(want)?want:(cam&&!cam.enabled?'motor':'camera'));
  camTimer=setInterval(async()=>{if(tab!=='camera')return;try{cam=Object.assign(cam||{},await getCam());renderCam()}catch(e){}},4000)})();
</script></body></html>)HTML";
