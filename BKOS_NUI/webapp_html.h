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
  <button class="lock" id="lockBtn" onclick="lockClick()">&#128274;</button>
</header>

<div class="wrap">
  <section>
    <h2>Boot &amp; eigenaar</h2>
    <div id="pubInfo">—</div>
  </section>

  <section>
    <h2>Iets aan de hand?</h2>
    <p style="font-size:.78rem;color:var(--text-dim);margin-bottom:8px;">Stuur direct een berichtje naar de eigenaar — geen pincode nodig.</p>
    <div class="grid2" id="berichtGrid"></div>
    <div id="berichtOk" style="font-size:.78rem;color:var(--green);min-height:1.1em;margin-top:8px;"></div>
  </section>

  <div id="gated" style="display:none">
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

    <section>
      <h2>Achtergrond webapp</h2>
      <p style="font-size:.78rem;color:var(--text-dim);margin-bottom:8px;">Eigen foto op de achtergrond van deze pagina — apart voor staand en liggend gebruik. Een nieuwe upload vervangt de oude in datzelfde slot.</p>
      <input type="file" id="agInputLiggend" accept="image/*" style="display:none">
      <input type="file" id="agInputStaand"  accept="image/*" style="display:none">
      <div class="grid2">
        <button class="mbtn" id="agBtnLiggend" onclick="document.getElementById('agInputLiggend').click()">LIGGEND</button>
        <button class="mbtn" id="agBtnStaand"  onclick="document.getElementById('agInputStaand').click()">STAAND</button>
      </div>
      <div id="agMelding" style="font-size:.78rem;min-height:1.1em;margin-top:8px;"></div>
    </section>

    <section>
      <a href="/haven" style="display:block;text-align:center;font-size:.82rem;color:var(--text-dim);padding:6px;">HAVEN-foto's beheren &#8594;</a>
    </section>
  </div>

  <section id="lockedHint">
    <p style="font-size:.78rem;color:var(--text-dim);text-align:center;padding:10px 0;">Vaarmodus, verlichting, paneel, IO en HAVEN-foto's vereisen de pincode. <a href="#" onclick="openPin();return false;">Ontgrendelen &#8594;</a></p>
  </section>
</div>

<div id="overlay" class="hidden">
  <div id="pinCard">
    <h3>Pincode vereist</h3>
    <p>Eén keer invoeren geeft toegang tot bediening én HAVEN-foto's — dezelfde pincode als op het scherm van de boordcomputer.</p>
    <input id="pinInput" type="password" inputmode="numeric" pattern="[0-9]*" maxlength="4" autocomplete="off">
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

// Eén PIN, gedeeld met /haven (localStorage is per host, dus zelfde apparaat) —
// eenmaal invoeren ontgrendelt zowel bediening hier als uploaden/verwijderen
// op de HAVEN-pagina, zonder daar opnieuw te hoeven inloggen.
var PIN_KEY = 'bkos_pin';
var pendingPin = '';       // welke pin het laatste auth-verzoek gebruikte
var autoPinSilent = false; // true = automatische poging met opgeslagen pin (geen foutmelding tonen als hij niet meer klopt)

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
  ws.onopen = function(){
    setConn(true);
    var saved = localStorage.getItem(PIN_KEY);
    if (saved && !unlocked) { pendingPin = saved; autoPinSilent = true; send({t:'auth', pin:saved}); }
  };
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
  document.getElementById('gated').style.display = on ? '' : 'none';
  document.getElementById('lockedHint').style.display = on ? 'none' : '';
}

// Klik op het hangslot: ontgrendeld → uitloggen (opgeslagen pin vergeten),
// vergrendeld → pincode vragen.
function lockClick(){
  if (unlocked) { localStorage.removeItem(PIN_KEY); setLock(false); }
  else openPin();
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
      if (pendingPin) localStorage.setItem(PIN_KEY, pendingPin);
      setLock(true); closePin(true); break;
    case 'auth_fout':
      localStorage.removeItem(PIN_KEY);
      if (!autoPinSilent) document.getElementById('pinErr').textContent = 'Onjuiste pincode';
      autoPinSilent = false; break;
    case 'auth_vereist':
      setLock(false); break;
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
  pendingPin = v; autoPinSilent = false;
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

// ─── Openbaar (geen PIN): boot/eigenaar-info + "iets is los"-berichtje ─────
function ladenPubliek(){
  fetch('/info/publiek').then(function(r){ return r.json(); }).then(function(d){
    document.getElementById('pubInfo').innerHTML =
      '<div style="font-size:.9rem;"><b>' + esc(d.boot || '?') + '</b>' +
      (d.type ? ' — ' + esc(d.type) : '') + '</div>' +
      (d.eigenaar ? '<div style="color:var(--text-dim);font-size:.8rem;margin-top:2px;">Eigenaar: ' + esc(d.eigenaar) + '</div>' : '');
  }).catch(function(){});
}

function ladenBericht(){
  fetch('/bericht/lijst').then(function(r){ return r.json(); }).then(function(d){
    var presets = d.presets || [];
    document.getElementById('berichtGrid').innerHTML = presets.map(function(t, i){
      return '<button class="mbtn" onclick="stuurBericht(' + i + ')">' + esc(t) + '</button>';
    }).join('');
  }).catch(function(){});
}

function stuurBericht(i){
  var fd = new URLSearchParams(); fd.set('idx', i);
  var el = document.getElementById('berichtOk');
  fetch('/bericht/verzend', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:fd.toString()})
    .then(function(r){ return r.json(); })
    .then(function(d){ el.textContent = d.ok ? 'Bericht verzonden.' : 'Versturen mislukt.'; })
    .catch(function(){ el.textContent = 'Versturen mislukt.'; })
    .finally(function(){ setTimeout(function(){ el.textContent = ''; }, 4000); });
}

// ─── Eigen achtergrondfoto (staand/liggend, apart slot per oriëntatie) ─────
// Puur cosmetisch, dus altijd toegepast (ook zonder pincode) — het UPLOADEN
// van een nieuwe foto blijft wel achter de pincode (zie de #gated-sectie).
function achtergrondToepassen(){
  var liggend = window.matchMedia('(orientation: landscape)').matches;
  var slot = liggend ? 'liggend' : 'staand';
  fetch('/achtergrond/info').then(function(r){ return r.json(); }).then(function(d){
    var aanwezig = liggend ? d.liggend : d.staand;
    if (!aanwezig) { document.body.style.backgroundImage = ''; return; }
    // Donkere overlay overheen zodat de bestaande (effen) kaartjes/secties
    // leesbaar blijven — zelfde idee als de fototint op het HAVEN-scherm van
    // de boordcomputer zelf.
    document.body.style.backgroundImage =
      'linear-gradient(rgba(12,26,38,.82),rgba(12,26,38,.82)), url(/achtergrond/foto?slot=' + slot + '&t=' + Date.now() + ')';
    document.body.style.backgroundSize = 'cover';
    document.body.style.backgroundPosition = 'center';
    document.body.style.backgroundAttachment = 'fixed';
  }).catch(function(){});
}
window.addEventListener('resize', achtergrondToepassen);

// ─── Uploaden van een achtergrondfoto: eenvoudige "cover"-crop (geen los
// zoom/pan-kader zoals bij de HAVEN-foto's — hier niet expliciet gevraagd),
// vaste HD-doelresolutie per oriëntatie, zelfde kwaliteitsladder-aanpak als
// de HAVEN-upload zodat het bestand altijd binnen de servergrens past.
var AG_KWALITEIT_STAPPEN = [0.8, 0.65, 0.5, 0.35, 0.22, 0.12];
var agMaxBytes = 1536 * 1024;
fetch('/achtergrond/info').then(function(r){ return r.json(); }).then(function(d){
  agMaxBytes = d.maxBytes || agMaxBytes;
}).catch(function(){});

function agEncodeerBinnenBudget(canvas, stapIdx, callback){
  canvas.toBlob(function(blob){
    var laatsteStap = stapIdx >= AG_KWALITEIT_STAPPEN.length - 1;
    if (blob && blob.size <= agMaxBytes) callback(blob, true);
    else if (laatsteStap) callback(blob, false);
    else agEncodeerBinnenBudget(canvas, stapIdx + 1, callback);
  }, 'image/jpeg', AG_KWALITEIT_STAPPEN[stapIdx]);
}

function agUpload(file, liggend){
  var doelW = liggend ? 1280 : 720, doelH = liggend ? 720 : 1280;
  var melding = document.getElementById('agMelding');
  melding.style.color = ''; melding.textContent = 'Foto wordt verwerkt…';
  var img = new Image();
  img.onload = function(){
    var canvas = document.createElement('canvas');
    canvas.width = doelW; canvas.height = doelH;
    var ctx = canvas.getContext('2d');
    var schaal = Math.max(doelW / img.width, doelH / img.height);
    var sw = doelW / schaal, sh = doelH / schaal;
    var sx = (img.width - sw) / 2, sy = (img.height - sh) / 2;
    ctx.drawImage(img, sx, sy, sw, sh, 0, 0, doelW, doelH);
    agEncodeerBinnenBudget(canvas, 0, function(blob, gelukt){
      if (!gelukt){ melding.style.color = 'var(--red)'; melding.textContent = 'Foto blijft te groot.'; return; }
      var fd = new FormData(); fd.append('foto', blob, 'bg.jpg');
      var xhr = new XMLHttpRequest();
      xhr.open('POST', '/achtergrond/upload?slot=' + (liggend?'liggend':'staand') + '&pin=' + encodeURIComponent(localStorage.getItem(PIN_KEY) || ''));
      xhr.onload = function(){
        if (xhr.status === 200){ melding.style.color = 'var(--green)'; melding.textContent = 'Opgeslagen.'; achtergrondToepassen(); }
        else { melding.style.color = 'var(--red)'; melding.textContent = 'Upload mislukt (' + (xhr.status===403?'onjuiste pincode':'opslag') + ').'; }
      };
      xhr.onerror = function(){ melding.style.color = 'var(--red)'; melding.textContent = 'Upload mislukt (verbinding).'; };
      xhr.send(fd);
    });
  };
  img.onerror = function(){ melding.style.color = 'var(--red)'; melding.textContent = 'Kon de foto niet lezen.'; };
  img.src = URL.createObjectURL(file);
}
document.getElementById('agInputLiggend').addEventListener('change', function(e){
  var f = e.target.files[0]; e.target.value = ''; if (f) agUpload(f, true);
});
document.getElementById('agInputStaand').addEventListener('change', function(e){
  var f = e.target.files[0]; e.target.value = ''; if (f) agUpload(f, false);
});

achtergrondToepassen();
ladenPubliek();
ladenBericht();
connect();
</script>
</body>
</html>
)HTMLPAGE";
