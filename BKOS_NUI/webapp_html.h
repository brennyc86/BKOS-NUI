// webapp_html.h — statische pagina voor de lokale afstandsbediening.
// Puur HTML/CSS/JS, geen externe dependencies (moet werken zonder internet).
// Praat met de firmware uitsluitend via de WebSocket op poort 8080
// (protocol: bkos_client.ino). PROGMEM: leeft in flash, niet in RAM.
#pragma once

const char WEBAPP_HTML[] PROGMEM = R"HTMLPAGE(<!DOCTYPE html>
<html lang="nl">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, viewport-fit=cover">
<title>BKOS Afstandsbediening</title>
<style>
:root{
  --bg:#0c1a26; --surface:#152535; --surface2:#1e3347; --surface3:#243d55;
  --cyan:#00d4ff; --text:#cce4f0; --text-dim:#7a99b0;
  --green:#00dc64; --amber:#ffb400; --red:#ff3246; --border:#2a4560;
  --haven:#3c64ff; --zeilen:#00c8aa; --motor:#ff7800; --anker:#8c6428;
}
*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent;}
html,body{height:100%;}
body{
  background:var(--bg);color:var(--text);
  font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',system-ui,sans-serif;
  -webkit-user-select:none;user-select:none;
  padding-bottom:env(safe-area-inset-bottom);
}
.wrap{max-width:640px;margin:0 auto;padding:0 14px 40px;}
a{color:var(--cyan);}

header{
  position:sticky;top:0;z-index:20;background:var(--bg);
  padding:calc(env(safe-area-inset-top) + 12px) 14px 10px;
  border-bottom:1px solid var(--border);
  display:flex;align-items:center;gap:10px;
}
header .dot{width:11px;height:11px;border-radius:50%;background:var(--red);flex:none;transition:background .2s;}
header .dot.on{background:var(--green);}
header .titel{flex:1;min-width:0;}
header .titel b{display:block;font-size:1.02rem;color:var(--cyan);letter-spacing:.5px;}
header .titel span{display:block;font-size:.72rem;color:var(--text-dim);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;}
header button.lock{
  background:var(--surface2);border:1px solid var(--border);color:var(--text);
  border-radius:8px;padding:8px 12px;font-size:1.1rem;line-height:1;flex:none;
}
header button.lock.open{color:var(--green);border-color:var(--green);}

section{margin-top:18px;}
section h2{
  font-size:.68rem;letter-spacing:2px;text-transform:uppercase;color:var(--text-dim);
  margin-bottom:8px;padding-left:2px;
}
.grid2{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;}
.grid3{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;}
.grid4{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;}
@media (max-width:400px){ .grid4{grid-template-columns:repeat(2,1fr);} }

button.mbtn{
  background:var(--surface);border:1px solid var(--border);color:var(--text-dim);
  border-radius:10px;padding:14px 6px;font-size:.82rem;font-weight:600;letter-spacing:.5px;
  transition:background .15s,color .15s,border-color .15s;
}
button.mbtn.active{background:var(--acc,var(--cyan));color:#04121c;border-color:var(--acc,var(--cyan));}

button.pbtn{
  background:var(--surface);border:1px solid var(--border);color:var(--text-dim);
  border-radius:10px;padding:14px 8px;font-size:.85rem;font-weight:600;
}
button.pbtn.mix{background:#3a2a06;color:var(--amber);border-color:var(--amber);}
button.pbtn.aan{background:#063a1c;color:var(--green);border-color:var(--green);}

.iorow{
  display:flex;align-items:center;gap:10px;
  background:var(--surface);border:1px solid var(--border);border-radius:8px;
  padding:10px 12px;margin-bottom:6px;
}
.iorow .lbl{font-size:.68rem;color:var(--text-dim);width:34px;flex:none;font-weight:700;}
.iorow .naam{flex:1;font-size:.92rem;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}
.iorow .badge{
  flex:none;width:12px;height:12px;border-radius:50%;background:#324a63;
}
.iorow .badge.aan{background:var(--green);}
.iorow button.sw{
  flex:none;border-radius:20px;padding:7px 16px;font-size:.78rem;font-weight:700;
  background:var(--surface2);color:var(--text-dim);border:1px solid var(--border);
}
.iorow button.sw.aan{background:var(--green);color:#04120a;border-color:var(--green);}
.iorow.ingang .naam{color:var(--text-dim);}

#netInfo{font-size:.78rem;color:var(--text-dim);line-height:1.6;padding:4px 2px;}
#netInfo b{color:var(--text);}

#overlay{
  position:fixed;inset:0;background:rgba(4,10,16,.82);z-index:50;
  display:flex;align-items:center;justify-content:center;padding:20px;
}
#overlay.hidden{display:none;}
#pinCard{
  background:var(--surface);border:1px solid var(--border);border-radius:14px;
  padding:26px 22px;width:100%;max-width:320px;text-align:center;
}
#pinCard h3{color:var(--cyan);font-size:1rem;margin-bottom:6px;}
#pinCard p{color:var(--text-dim);font-size:.8rem;margin-bottom:18px;}
#pinInput{
  width:100%;background:var(--surface2);border:1px solid var(--border);border-radius:10px;
  color:var(--text);font-size:1.6rem;letter-spacing:.6em;text-align:center;padding:12px 0 12px 0.6em;
  margin-bottom:14px;
}
#pinErr{color:var(--red);font-size:.78rem;min-height:1.1em;margin-bottom:10px;}
#pinCard .row{display:flex;gap:8px;}
#pinCard button{
  flex:1;border-radius:10px;padding:12px;font-size:.9rem;font-weight:600;border:1px solid var(--border);
}
#pinCard button.ok{background:var(--cyan);color:#04121c;border-color:var(--cyan);}
#pinCard button.cancel{background:var(--surface2);color:var(--text-dim);}
</style>
</head>
<body>

<header>
  <div class="dot" id="connDot"></div>
  <div class="titel">
    <b>BKOS</b>
    <span id="hdrSub">verbinden…</span>
  </div>
  <button class="lock" id="lockBtn" onclick="openPin()">&#128274;</button>
</header>

<div class="wrap">
  <section>
    <h2>Vaarmodus</h2>
    <div class="grid4" id="modusGrid"></div>
  </section>

  <section>
    <h2>Verlichting</h2>
    <div class="grid3" id="lichtGrid"></div>
  </section>

  <section id="paneelSection" style="display:none">
    <h2>Paneel</h2>
    <div class="grid3" id="paneelGrid"></div>
  </section>

  <section>
    <h2>IO kanalen</h2>
    <div id="ioList"></div>
  </section>

  <section>
    <h2>Verbonden modules</h2>
    <div id="netInfo">—</div>
  </section>
</div>

<div id="overlay" class="hidden">
  <div id="pinCard">
    <h3>Pincode vereist</h3>
    <p>Voor bediening is dezelfde pincode nodig als op het scherm van de boordcomputer.</p>
    <input id="pinInput" inputmode="numeric" pattern="[0-9]*" maxlength="4" autocomplete="off">
    <div id="pinErr"></div>
    <div class="row">
      <button class="cancel" onclick="closePin(false)">ANNULEER</button>
      <button class="ok" onclick="submitPin()">ONTGRENDEL</button>
    </div>
  </div>
</div>

<script>
'use strict';
var ws = null;
var unlocked = false;
var ioData = {cnt:0,o:[],i:[],r:[],n:[],lbl:[]};
var paneelData = [];
var stateData = {m:0,l:0};
var infoData = {};
var netData = {peers:[]};

var MODI = [
  {id:0,naam:'HAVEN',kleur:'var(--haven)'},
  {id:1,naam:'ZEILEN',kleur:'var(--zeilen)'},
  {id:2,naam:'MOTOR',kleur:'var(--motor)'},
  {id:3,naam:'ANKER',kleur:'var(--anker)'}
];
var LICHT = ['UIT','AAN','AUTO'];

function esc(s){
  var d=document.createElement('div'); d.textContent=String(s); return d.innerHTML;
}

function wsUrl(){
  return 'ws://' + location.hostname + ':8080/';
}

function connect(){
  try{ ws = new WebSocket(wsUrl()); }catch(e){ setTimeout(connect, 2000); return; }
  ws.onopen = function(){ setConn(true); };
  ws.onclose = function(){ setConn(false); unlocked=false; setLock(false); setTimeout(connect, 2000); };
  ws.onerror = function(){ try{ ws.close(); }catch(e){} };
  ws.onmessage = function(ev){
    var msg;
    try{ msg = JSON.parse(ev.data); }catch(e){ return; }
    handleMsg(msg);
  };
}

function setConn(on){
  document.getElementById('connDot').className = 'dot' + (on ? ' on' : '');
  document.getElementById('hdrSub').textContent = on ? 'verbonden' : 'verbinden…';
}

function setLock(on){
  unlocked = on;
  var b = document.getElementById('lockBtn');
  b.className = 'lock' + (on ? ' open' : '');
  b.innerHTML = on ? '&#128275;' : '&#128274;';
}

function handleMsg(msg){
  switch(msg.t){
    case 'io_full':
      ioData = msg; renderIO(); break;
    case 'io_delta':
      if (msg.ch < ioData.o.length) { ioData.o[msg.ch]=msg.o; ioData.i[msg.ch]=msg.i; renderIO(); }
      break;
    case 'state':
      stateData = msg; renderState(); break;
    case 'net':
      netData = msg; renderNet(); break;
    case 'info':
      infoData = msg; renderInfo(); break;
    case 'paneel':
      paneelData = msg.items || []; renderPaneel(); break;
    case 'auth_ok':
      setLock(true); closePin(true); break;
    case 'auth_fout':
      document.getElementById('pinErr').textContent = 'Onjuiste pincode'; break;
    case 'auth_vereist':
      setLock(false); openPin(); break;
    default: break;
  }
}

function send(obj){
  if (ws && ws.readyState === 1) ws.send(JSON.stringify(obj));
}

function needAuth(){
  if (!unlocked){ openPin(); return true; }
  return false;
}

function setModus(m){ if (needAuth()) return; send({t:'set_modus', m:m}); }
function setLicht(l){ if (needAuth()) return; send({t:'set_licht', l:l}); }
function toggleIO(i){ if (needAuth()) return; send({t:'io_toggle', i:i}); }
function togglePaneel(i){ if (needAuth()) return; send({t:'paneel_toggle', i:i}); }

function openPin(){
  document.getElementById('pinErr').textContent = '';
  document.getElementById('pinInput').value = '';
  document.getElementById('overlay').classList.remove('hidden');
  setTimeout(function(){ document.getElementById('pinInput').focus(); }, 50);
}
function closePin(){
  document.getElementById('overlay').classList.add('hidden');
}
function submitPin(){
  var v = document.getElementById('pinInput').value;
  if (v.length !== 4){ document.getElementById('pinErr').textContent = '4 cijfers invoeren'; return; }
  send({t:'auth', pin:v});
}
document.getElementById('pinInput').addEventListener('keydown', function(e){
  if (e.key === 'Enter') submitPin();
});

function renderState(){
  document.getElementById('modusGrid').innerHTML = MODI.map(function(m){
    return '<button class="mbtn' + (stateData.m===m.id?' active':'') + '" style="--acc:' + m.kleur + '" onclick="setModus(' + m.id + ')">' + m.naam + '</button>';
  }).join('');
  document.getElementById('lichtGrid').innerHTML = LICHT.map(function(l,i){
    return '<button class="mbtn' + (stateData.l===i?' active':'') + '" onclick="setLicht(' + i + ')">' + l + '</button>';
  }).join('');
}

function renderPaneel(){
  var sec = document.getElementById('paneelSection');
  if (!paneelData.length){ sec.style.display = 'none'; return; }
  sec.style.display = '';
  document.getElementById('paneelGrid').innerHTML = paneelData.map(function(p, i){
    var cls = p.staat === 2 ? 'aan' : (p.staat === 1 ? 'mix' : '');
    return '<button class="pbtn ' + cls + '" onclick="togglePaneel(' + i + ')">' + esc(p.naam) + '</button>';
  }).join('');
}

function renderIO(){
  var box = document.getElementById('ioList');
  var html = '';
  for (var i = 0; i < ioData.cnt; i++){
    var isIn = ioData.r[i] === 1;
    var aan  = ioData.o[i] === 1 || ioData.o[i] === 3;
    var lbl  = (ioData.lbl && ioData.lbl[i]) ? ioData.lbl[i] : i;
    var naam = (ioData.n && ioData.n[i]) ? ioData.n[i] : ('kanaal ' + i);
    if (isIn){
      var actief = !!ioData.i[i];
      html += '<div class="iorow ingang"><div class="lbl">' + esc(lbl) + '</div>' +
              '<div class="naam">' + esc(naam) + '</div>' +
              '<div class="badge' + (actief?' aan':'') + '"></div></div>';
    } else {
      html += '<div class="iorow"><div class="lbl">' + esc(lbl) + '</div>' +
              '<div class="naam">' + esc(naam) + '</div>' +
              '<button class="sw' + (aan?' aan':'') + '" onclick="toggleIO(' + i + ')">' + (aan?'AAN':'UIT') + '</button></div>';
    }
  }
  box.innerHTML = html || '<div style="color:var(--text-dim);font-size:.85rem;padding:8px 2px;">Geen IO-kanalen gevonden</div>';
}

function renderInfo(){
  document.getElementById('hdrSub').textContent =
    (infoData.boot || infoData.naam || 'BKOS') + ' · v' + (infoData.ver || '?');
}

function renderNet(){
  var el = document.getElementById('netInfo');
  var peers = (netData.peers || []);
  if (!peers.length){ el.innerHTML = 'Geen extra modules gekoppeld (standalone of master zonder slaves).'; return; }
  el.innerHTML = peers.map(function(p){
    return '<div><b>' + esc(p.naam) + '</b> — ' + (p.online ? 'online' : 'offline') + ', ' + p.io + ' IO</div>';
  }).join('');
}

connect();
</script>
</body>
</html>
)HTMLPAGE";
