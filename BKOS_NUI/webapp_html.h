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
button.pbtn.locked, button.sw:disabled{opacity:.5;}

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

.tabbar{
  display:flex;gap:6px;margin-top:16px;border-bottom:1px solid var(--border);
  padding-bottom:0;overflow-x:auto;
}
.tabbtn{
  flex:1;min-width:0;background:transparent;color:var(--text-dim);border:none;
  border-bottom:2px solid transparent;padding:10px 4px;font-size:.78rem;
  font-weight:700;letter-spacing:.5px;white-space:nowrap;
}
.tabbtn.active{color:var(--cyan);border-bottom-color:var(--cyan);}
.tabpane section:first-child{margin-top:14px;}

#bfInfo{font-size:.78rem;color:var(--text-dim);line-height:1.6;}
.filerow{
  display:flex;align-items:center;gap:10px;
  background:var(--surface);border:1px solid var(--border);border-radius:8px;
  padding:9px 12px;margin-bottom:6px;font-size:.85rem;
}
.filerow .naam{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}
.filerow .naam.map{color:var(--cyan);font-weight:700;}
.filerow .grootte{font-size:.72rem;color:var(--text-dim);flex:none;}
.filerow button.del{padding:6px 12px;font-size:.78rem;color:var(--red);border-color:var(--red);flex:none;border-radius:8px;background:var(--surface2);border-width:1px;border-style:solid;}
.pbtn.hoofd{grid-column:1/-1;}
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

#cropModal{
  position:fixed;inset:0;background:rgba(4,10,16,.94);z-index:60;
  display:flex;flex-direction:column;align-items:center;justify-content:center;
  padding:20px;
}
#cropModal.hidden{display:none;}
#cropViewport{
  width:min(88vw,420px);position:relative;overflow:hidden;
  border-radius:8px;border:2px solid var(--cyan);background:#000;touch-action:none;
}
#cropImg{position:absolute;left:0;top:0;transform-origin:0 0;user-select:none;-webkit-user-drag:none;max-width:none;}
#cropZoom{width:min(88vw,420px);margin-top:16px;accent-color:var(--cyan);}
#cropModal .row{display:flex;gap:8px;width:min(88vw,420px);}
#cropModal .row button{
  flex:1;border-radius:10px;padding:12px;font-size:.9rem;font-weight:600;border:1px solid var(--border);
}
#cropModal .row button.ok{background:var(--cyan);color:#04121c;border-color:var(--cyan);}
#cropModal .row button.cancel{background:var(--surface2);color:var(--text-dim);}
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
    <h2 id="berichtKop" onclick="berichtToggle()" style="cursor:pointer;display:flex;align-items:center;justify-content:space-between;">
      <span>Iets aan de hand?</span>
      <span id="berichtChevron">&#9660;</span>
    </h2>
    <div id="berichtBody">
      <p style="font-size:.78rem;color:var(--text-dim);margin-bottom:8px;">Stuur direct een berichtje naar de eigenaar — geen pincode nodig.</p>
      <div class="grid2" id="berichtGrid"></div>
      <div id="berichtOk" style="font-size:.78rem;color:var(--green);min-height:1.1em;margin-top:8px;"></div>
    </div>
  </section>

  <div id="gated" style="display:none">
    <div class="tabbar">
      <button class="tabbtn active" data-tab="huis" onclick="setTab('huis')">HUIS</button>
      <button class="tabbtn" data-tab="boot" onclick="setTab('boot')">BOOT</button>
      <button class="tabbtn" id="tabBtnIo" data-tab="io" onclick="setTab('io')">IO</button>
      <button class="tabbtn" id="tabBtnFotos" data-tab="fotos" onclick="setTab('fotos')">FOTOS</button>
      <button class="tabbtn" id="tabBtnInstellingen" data-tab="instellingen" onclick="setTab('instellingen')">INSTELL.</button>
    </div>

    <div id="tabHuis" class="tabpane">
      <section>
        <h2>Interieurverlichting</h2>
        <div class="grid3" id="huisAlgemeen"></div>
      </section>
      <section>
        <h2>Lampen</h2>
        <div id="huisLampen"></div>
      </section>
    </div>

    <div id="tabBoot" class="tabpane" style="display:none">
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
    </div>

    <div id="tabIo" class="tabpane" style="display:none">
      <section>
        <h2>IO kanalen</h2>
        <div id="ioList"></div>
      </section>

      <section>
        <h2>Verbonden modules</h2>
        <div id="netInfo">—</div>
      </section>
    </div>

    <div id="tabFotos" class="tabpane" style="display:none">
      <section>
        <h2>Foto HAVEN-dashboard</h2>
        <p style="font-size:.78rem;color:var(--text-dim);margin-bottom:8px;">Achtergrondfoto van het HAVEN-scherm op de boordcomputer zelf. Elke upload voegt een nieuwe foto toe aan de diashow.</p>
        <input type="file" id="fotoInputHaven" accept="image/*" style="display:none">
        <button class="mbtn" id="fotoBtnHaven" onclick="document.getElementById('fotoInputHaven').click()">FOTO KIEZEN &amp; BIJSNIJDEN</button>
        <div id="fotosMelding" style="font-size:.78rem;min-height:1.1em;margin-top:8px;"></div>
        <div id="fotosLijst" style="margin-top:8px;"></div>
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
        <h2>Opslag &amp; bestanden</h2>
        <div id="bfInfo">—</div>
        <div class="grid2" id="bfFsKeuze" style="margin-top:8px;display:none;"></div>
        <div id="bfPad" style="font-size:.75rem;color:var(--text-dim);margin:8px 0 6px;">/</div>
        <div id="bfLijst"><div style="color:var(--text-dim);font-size:.85rem;">Laden…</div></div>
      </section>
    </div>

    <div id="tabInstellingen" class="tabpane" style="display:none">
      <section>
        <h2>Boot</h2>
        <div id="instBoot"></div>
      </section>
      <section>
        <h2>Eigenaar</h2>
        <div id="instEig"></div>
        <button class="mbtn" style="margin-top:8px;" onclick="instellingenOpslaan()">OPSLAAN</button>
        <div id="instMelding" style="font-size:.78rem;min-height:1.1em;margin-top:8px;"></div>
      </section>

      <section>
        <h2>Pincode wijzigen</h2>
        <input id="pinOud" type="password" inputmode="numeric" pattern="[0-9]*" maxlength="4" placeholder="huidige pincode" style="width:100%;background:var(--surface2);border:1px solid var(--border);border-radius:8px;color:var(--text);font-size:1rem;padding:10px;margin-bottom:8px;">
        <input id="pinNieuw" type="password" inputmode="numeric" pattern="[0-9]*" maxlength="4" placeholder="nieuwe pincode" style="width:100%;background:var(--surface2);border:1px solid var(--border);border-radius:8px;color:var(--text);font-size:1rem;padding:10px;margin-bottom:8px;">
        <button class="mbtn" onclick="pinWijzigen()">PINCODE WIJZIGEN</button>
        <div id="pinWijzMelding" style="font-size:.78rem;min-height:1.1em;margin-top:8px;"></div>
      </section>

      <section>
        <h2>Gasten pincodes</h2>
        <p style="font-size:.78rem;color:var(--text-dim);margin-bottom:8px;">GAST = HUIS+BOOT. LOGE = ook kanalen die minimaal LOGE vereisen (bv. een slot). DELER = ook kanalen die minimaal DELER vereisen. Nooit IO/FOTOS/INSTELLINGEN — dat blijft de eigenaar.</p>
        <input id="gastNaam" type="text" placeholder="naam (optioneel)" style="width:100%;background:var(--surface2);border:1px solid var(--border);border-radius:8px;color:var(--text);font-size:.9rem;padding:10px;margin-bottom:8px;">
        <label style="font-size:.72rem;color:var(--text-dim);display:block;margin-bottom:4px;">Niveau</label>
        <div class="grid3" id="gastNiveauKeuze" style="margin-bottom:8px;"></div>
        <label style="font-size:.72rem;color:var(--text-dim);display:block;margin-bottom:4px;">Geldigheid</label>
        <div class="grid3" id="gastDuurKeuze" style="margin-bottom:8px;"></div>
        <div class="grid2">
          <button class="mbtn" id="gastActieBtn" onclick="gastActie()">CODE AANMAKEN</button>
          <button class="mbtn" id="gastAnnuleerBtn" onclick="gastFormReset()" style="display:none;">ANNULEER</button>
        </div>
        <div id="gastMelding" style="font-size:.78rem;min-height:1.1em;margin-top:8px;"></div>
        <div id="gastLijst" style="margin-top:8px;"></div>
      </section>
    </div>
  </div>

  <section id="lockedHint">
    <p style="font-size:.78rem;color:var(--text-dim);text-align:center;padding:10px 0;">Bediening vereist de eigenaars- of een gastpincode. <a href="#" onclick="openPin();return false;">Ontgrendelen &#8594;</a></p>
  </section>
</div>

<div id="overlay" class="hidden">
  <div id="pinCard">
    <h3>Pincode vereist</h3>
    <p>De eigenaars-pincode (zelfde als op het scherm van de boordcomputer) geeft volledige toegang. Een tijdelijke gastcode geeft alleen HUIS+BOOT.</p>
    <input id="pinInput" type="password" inputmode="numeric" pattern="[0-9]*" maxlength="4" autocomplete="off">
    <div id="pinErr"></div>
    <div class="row">
      <button class="cancel" onclick="closePin(false)">ANNULEER</button>
      <button class="ok" onclick="submitPin()">ONTGRENDEL</button>
    </div>
  </div>
</div>

<div id="cropModal" class="hidden">
  <p style="font-size:.78rem;color:var(--text-dim);text-align:center;margin-bottom:10px;">Sleep om te schuiven, gebruik de schuif om in/uit te zoomen. Alleen het deel in het kader wordt opgeslagen.</p>
  <div id="cropViewport"><img id="cropImg" draggable="false" alt=""></div>
  <input type="range" id="cropZoom" min="100" max="400" value="100">
  <div class="row" style="margin-top:14px;">
    <button class="cancel" onclick="cropAnnuleer()">ANNULEER</button>
    <button class="ok" onclick="cropBevestig()">GEBRUIK DIT DEEL</button>
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
var lampData = {hoofdAanwezig:false,hoofdAan:false,kleur:0,overrule:-1,items:[]};
// 0 = uitgelogd, 1 = gastcode (alleen HUIS+BOOT), 2 = eigenaar (alles) — de
// server bepaalt dit (pin_niveau() in gast.h), de client verbergt alleen de
// tabbladen die toch niets zouden mogen doen; échte afdwinging gebeurt altijd
// serverkant (WebSocket-commando's en HTTP-routes checken zelf opnieuw).
// Zelfde 4 rechtenniveaus als gast.h (server bepaalt/handhaaft dit altijd
// opnieuw — deze constanten zijn puur voor leesbare UI-vergelijkingen).
var NIVEAU_GEEN = 0, NIVEAU_GAST = 1, NIVEAU_LOGE = 2, NIVEAU_DELER = 3, NIVEAU_EIGENAAR = 4;
var NIVEAU_NAMEN = ['GEEN', 'GAST', 'LOGE', 'DELER', 'EIGENAAR'];
var niveau = 0;
var actieveTab = 'huis';
var berichtOpen = true;
var instellingenData = {zeilnr:'',naam:'',boot:[],eig:[]};
var gastData = [];

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
  ws.onclose = function(){ setConn(false); unlocked=false; setLock(false, 0); setTimeout(connect, 2000); };
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

function berichtSetOpen(open){
  berichtOpen = open;
  document.getElementById('berichtBody').style.display = open ? '' : 'none';
  document.getElementById('berichtChevron').innerHTML = open ? '&#9660;' : '&#9654;';
}
function berichtToggle(){ berichtSetOpen(!berichtOpen); }

function setLock(on, niv){
  unlocked = on;
  niveau = on ? (niv || 0) : 0;
  var b = document.getElementById('lockBtn');
  b.className = 'lock' + (on ? ' open' : '');
  b.innerHTML = on ? '&#128275;' : '&#128274;';
  document.getElementById('gated').style.display = on ? '' : 'none';
  document.getElementById('lockedHint').style.display = on ? 'none' : '';
  // Ingelogd: het berichtje-blok is dan niet meer de enige manier om iets
  // te melden, dus mag ingeklapt — uitgelogd blijft het vanzelf open (de
  // enige publieke functie op deze pagina).
  berichtSetOpen(!on);
  // Een gastcode (niveau 1) mag alleen HUIS+BOOT — IO/FOTOS/INSTELLINGEN
  // blijven voor de eigenaar (niveau 2). Server dwingt dit sowieso zelf af;
  // dit is puur zodat een gast geen tabblad ziet dat toch niets zou doen.
  var eigenaar = (niveau >= NIVEAU_EIGENAAR);
  document.getElementById('tabBtnIo').style.display = eigenaar ? '' : 'none';
  document.getElementById('tabBtnFotos').style.display = eigenaar ? '' : 'none';
  document.getElementById('tabBtnInstellingen').style.display = eigenaar ? '' : 'none';
  var eigenaarTabs = ['io','fotos','instellingen'];
  if (on && !eigenaar && eigenaarTabs.indexOf(actieveTab) >= 0) setTab('huis');
  else if (on) setTab(actieveTab);
}

function setTab(naam){
  actieveTab = naam;
  var tabs = ['huis','boot','io','fotos','instellingen'];
  tabs.forEach(function(t){
    var pane = document.getElementById('tab' + t.charAt(0).toUpperCase() + t.slice(1));
    if (pane) pane.style.display = (t === naam) ? '' : 'none';
  });
  if (naam === 'fotos') { fotosInfo(); fotosLijst(); bfInfo(); bfLijst(); }
  if (naam === 'instellingen') { instellingenLaden(); gastLaden(); }
  document.querySelectorAll('.tabbtn').forEach(function(btn){
    btn.classList.toggle('active', btn.getAttribute('data-tab') === naam);
  });
}

// Klik op het hangslot: ontgrendeld → uitloggen (opgeslagen pin vergeten),
// vergrendeld → pincode vragen.
function lockClick(){
  if (unlocked) { localStorage.removeItem(PIN_KEY); setLock(false, 0); }
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
    case 'lampen':
      lampData = msg; renderHuis(); break;
    case 'instellingen':
      instellingenData = msg; renderInstellingen(); break;
    case 'gastlijst':
      gastData = msg.items || []; renderGast(); break;
    case 'gast_nieuw':
      renderGastNieuw(msg); break;
    case 'gast_bewerkt':
      renderGastBewerkt(msg); break;
    case 'pin_wijzig_res':
      renderPinRes(msg.ok); break;
    case 'auth_ok':
      if (pendingPin) localStorage.setItem(PIN_KEY, pendingPin);
      setLock(true, msg.niveau || 0); closePin(true); break;
    case 'auth_fout':
      localStorage.removeItem(PIN_KEY);
      if (!autoPinSilent) document.getElementById('pinErr').textContent = 'Onjuiste pincode';
      autoPinSilent = false; break;
    case 'auth_vereist':
      setLock(false, 0); break;
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
function hoofdToggle(){ if (needAuth()) return; send({t:'interieur_toggle'}); }
function kleurKiezen(rood){ if (needAuth()) return; send({t:'interieur_kleur', rood:(rood?1:0)}); }
function lampToggle(nr){ if (needAuth()) return; send({t:'lamp_toggle', nr:nr}); }
function lampAlles(aan){ if (needAuth()) return; send({t:'lamp_alles', aan:(aan?1:0)}); }

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

function renderHuis(){
  var alg = [];
  if (lampData.hoofdAanwezig) {
    alg.push('<button class="pbtn hoofd ' + (lampData.hoofdAan?'aan':'') + '" onclick="hoofdToggle()">' +
              (lampData.hoofdAan ? 'VERLICHTING: AAN' : 'VERLICHTING: UIT') + '</button>');
  }
  alg.push('<button class="pbtn ' + (lampData.overrule===0?'aan':'') + '" onclick="kleurKiezen(false)">WIT</button>');
  alg.push('<button class="pbtn ' + (lampData.overrule===1?'aan':'') + '" onclick="kleurKiezen(true)">ROOD</button>');
  if (lampData.items.length){
    alg.push('<button class="pbtn" onclick="lampAlles(true)">ALLES AAN</button>');
    alg.push('<button class="pbtn" onclick="lampAlles(false)">ALLES UIT</button>');
  }
  document.getElementById('huisAlgemeen').innerHTML = alg.join('');

  var box = document.getElementById('huisLampen');
  if (!lampData.items.length){
    box.innerHTML = '<div style="color:var(--text-dim);font-size:.85rem;padding:8px 2px;">Geen genummerde lampgroepen gevonden.</div>';
    return;
  }
  box.innerHTML = lampData.items.map(function(l){
    var minNiveau = l.minNiveau || NIVEAU_GAST;
    if (niveau < minNiveau) {
      return '<div class="iorow"><div class="naam">&#128274; ' + esc(l.naam) + '</div>' +
             '<button class="sw" disabled title="Vereist niveau ' + esc(NIVEAU_NAMEN[minNiveau]) + '">' + esc(NIVEAU_NAMEN[minNiveau]) + '</button></div>';
    }
    return '<div class="iorow"><div class="naam">' + esc(l.naam) + '</div>' +
           '<button class="sw' + (l.aan?' aan':'') + '" onclick="lampToggle(' + l.nr + ')">' + (l.aan?'AAN':'UIT') + '</button></div>';
  }).join('');
}

function renderPaneel(){
  var sec = document.getElementById('paneelSection');
  if (!paneelData.length){ sec.style.display = 'none'; return; }
  sec.style.display = '';
  document.getElementById('paneelGrid').innerHTML = paneelData.map(function(p, i){
    var minNiveau = p.minNiveau || NIVEAU_GAST;
    if (niveau < minNiveau) {
      return '<button class="pbtn locked" disabled title="Vereist niveau ' + esc(NIVEAU_NAMEN[minNiveau]) + '">&#128274; ' + esc(p.naam) + '</button>';
    }
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

// ─── Bestanden-tab: simpele SPIFFS/SD-lijst+verwijderen, mirror van CONFIG →
// BESTANDEN op het scherm zelf. Eigenaar-only (server checkt dit zelf ook bij
// verwijderen) — vandaar dat dit tabblad al bij het inloggen verborgen wordt
// voor een gastcode (zie setLock()).
var bfFs = 'spiffs';
var bfPad = '/';

function fmtBytes(n){
  if (n < 1024) return n + ' B';
  if (n < 1024*1024) return (n/1024).toFixed(1) + ' KB';
  return (n/1024/1024).toFixed(1) + ' MB';
}

function bfInfo(){
  fetch('/bestanden/info').then(function(r){ return r.json(); }).then(function(d){
    var s = 'SPIFFS: ' + fmtBytes(d.spiffsTotaal - d.spiffsVrij) + ' / ' + fmtBytes(d.spiffsTotaal) + ' gebruikt';
    if (d.sdBeschikbaar) s += '<br>SD: ' + fmtBytes(d.sdTotaal - d.sdVrij) + ' / ' + fmtBytes(d.sdTotaal) + ' gebruikt';
    document.getElementById('bfInfo').innerHTML = s;
    var keuze = document.getElementById('bfFsKeuze');
    if (d.sdBeschikbaar) {
      keuze.style.display = '';
      keuze.innerHTML =
        '<button class="mbtn' + (bfFs==='spiffs'?' active':'') + '" onclick="bfWisselFs(\'spiffs\')">SPIFFS</button>' +
        '<button class="mbtn' + (bfFs==='sd'?' active':'') + '" onclick="bfWisselFs(\'sd\')">SD</button>';
    } else {
      keuze.style.display = 'none';
    }
  }).catch(function(){});
}

function bfWisselFs(fs){ bfFs = fs; bfPad = '/'; bfLijst(); }

function bfLijst(){
  document.getElementById('bfPad').textContent = bfPad;
  fetch('/bestanden/lijst?fs=' + bfFs + '&pad=' + encodeURIComponent(bfPad))
    .then(function(r){ return r.json(); })
    .then(function(d){
      var items = d.items || [];
      var html = '';
      if (bfPad !== '/') {
        html += '<div class="filerow" onclick="bfOmhoog()" style="cursor:pointer;"><div class="naam map">.. (omhoog)</div></div>';
      }
      if (!items.length && bfPad === '/') {
        html += '<div style="color:var(--text-dim);font-size:.85rem;padding:8px 2px;">Geen bestanden gevonden.</div>';
      }
      items.forEach(function(it){
        if (it.map) {
          html += '<div class="filerow" onclick="bfNaarMap(\'' + esc(it.naam) + '\')" style="cursor:pointer;">' +
                  '<div class="naam map">' + esc(it.naam) + '/</div></div>';
        } else {
          html += '<div class="filerow"><div class="naam">' + esc(it.naam) + '</div>' +
                  '<div class="grootte">' + fmtBytes(it.bytes) + '</div>' +
                  '<button class="del" onclick="bfVerwijder(\'' + esc(it.naam) + '\')">WISSEN</button></div>';
        }
      });
      document.getElementById('bfLijst').innerHTML = html;
    }).catch(function(){});
}

function bfNaarMap(naam){
  bfPad = (bfPad === '/' ? '/' : bfPad + '/') + naam;
  bfLijst();
}
function bfOmhoog(){
  var i = bfPad.lastIndexOf('/');
  bfPad = (i <= 0) ? '/' : bfPad.substring(0, i);
  bfLijst();
}
function bfVerwijder(naam){
  if (needAuth()) return;
  var pad = (bfPad === '/' ? '/' : bfPad + '/') + naam;
  var fd = new URLSearchParams();
  fd.set('fs', bfFs); fd.set('pad', pad); fd.set('pin', localStorage.getItem(PIN_KEY) || '');
  fetch('/bestanden/verwijder', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:fd.toString()})
    .then(function(r){ return r.json(); })
    .then(function(d){ bfLijst(); })
    .catch(function(){});
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

// ─── Eén gedeelde interactieve pan/zoom/crop-modal voor alle 3 foto-uploads
// (HAVEN-dashboardfoto, achtergrond liggend, achtergrond staand) — elk met
// zijn eigen doelresolutie/kwaliteitsladder/servergrens, cropTarget bepaalt
// welke bij bevestigen wordt gebruikt en waarheen geüpload wordt.
var CROP_TARGETS = {
  liggend: { doelW: 1280, doelH: 720,  maxBytes: 1536 * 1024, stappen: [0.8, 0.65, 0.5, 0.35, 0.22, 0.12] },
  staand:  { doelW: 720,  doelH: 1280, maxBytes: 1536 * 1024, stappen: [0.8, 0.65, 0.5, 0.35, 0.22, 0.12] },
  haven:   { doelW: 800,  doelH: 480,  maxBytes: 300 * 1024,  stappen: [0.75, 0.6, 0.45, 0.32, 0.22, 0.14, 0.08, 0.04] }
};

var cropTarget = null;
var cropImgEl, cropVp;
var cropNatW = 0, cropNatH = 0;
var cropBaseScale = 1, cropScale = 1;
var cropPanX = 0, cropPanY = 0;
var cropSlepen = false, cropStartX = 0, cropStartY = 0, cropStartPanX = 0, cropStartPanY = 0;

function cropMelding(target, tekst, isFout){
  var el = document.getElementById(target === 'haven' ? 'fotosMelding' : 'agMelding');
  if (!el) return;
  el.style.color = isFout ? 'var(--red)' : 'var(--green)';
  el.textContent = tekst;
}

function openCrop(file, target){
  cropTarget = target;
  cropImgEl = document.getElementById('cropImg');
  cropVp = document.getElementById('cropViewport');
  var cfg = CROP_TARGETS[target];
  cropVp.style.aspectRatio = cfg.doelW + '/' + cfg.doelH;
  var img = new Image();
  img.onload = function(){
    cropNatW = img.naturalWidth; cropNatH = img.naturalHeight;
    cropImgEl.src = img.src;
    document.getElementById('cropZoom').value = 100;
    document.getElementById('cropModal').classList.remove('hidden');
    requestAnimationFrame(cropHerbereken);
  };
  img.onerror = function(){ cropMelding(target, 'Kon de foto niet lezen.', true); };
  img.src = URL.createObjectURL(file);
}

function cropHerbereken(){
  var vpW = cropVp.clientWidth, vpH = cropVp.clientHeight;
  cropBaseScale = Math.max(vpW / cropNatW, vpH / cropNatH);
  var zoom = document.getElementById('cropZoom').value / 100;
  cropScale = cropBaseScale * zoom;
  var dispW = cropNatW * cropScale, dispH = cropNatH * cropScale;
  cropPanX = Math.min(0, Math.max(vpW - dispW, (vpW - dispW) / 2));
  cropPanY = Math.min(0, Math.max(vpH - dispH, (vpH - dispH) / 2));
  cropToon();
}
function cropToon(){
  cropImgEl.style.width  = (cropNatW * cropScale) + 'px';
  cropImgEl.style.height = (cropNatH * cropScale) + 'px';
  cropImgEl.style.transform = 'translate(' + cropPanX + 'px,' + cropPanY + 'px)';
}
function cropKlem(){
  var vpW = cropVp.clientWidth, vpH = cropVp.clientHeight;
  var dispW = cropNatW * cropScale, dispH = cropNatH * cropScale;
  cropPanX = Math.min(0, Math.max(vpW - dispW, cropPanX));
  cropPanY = Math.min(0, Math.max(vpH - dispH, cropPanY));
}
document.getElementById('cropZoom').addEventListener('input', function(e){
  if (!cropVp) return;
  var vpW = cropVp.clientWidth, vpH = cropVp.clientHeight;
  var midXvoor = (vpW / 2 - cropPanX) / cropScale;
  var midYvoor = (vpH / 2 - cropPanY) / cropScale;
  cropScale = cropBaseScale * (e.target.value / 100);
  cropPanX = vpW / 2 - midXvoor * cropScale;
  cropPanY = vpH / 2 - midYvoor * cropScale;
  cropKlem();
  cropToon();
});
document.getElementById('cropViewport').addEventListener('pointerdown', function(e){
  cropSlepen = true;
  cropStartX = e.clientX; cropStartY = e.clientY;
  cropStartPanX = cropPanX; cropStartPanY = cropPanY;
  cropVp.setPointerCapture(e.pointerId);
});
document.getElementById('cropViewport').addEventListener('pointermove', function(e){
  if (!cropSlepen) return;
  cropPanX = cropStartPanX + (e.clientX - cropStartX);
  cropPanY = cropStartPanY + (e.clientY - cropStartY);
  cropKlem();
  cropToon();
});
document.getElementById('cropViewport').addEventListener('pointerup', function(){ cropSlepen = false; });
document.getElementById('cropViewport').addEventListener('pointercancel', function(){ cropSlepen = false; });

function cropAnnuleer(){
  document.getElementById('cropModal').classList.add('hidden');
  if (cropImgEl) cropImgEl.src = '';
  cropTarget = null;
}

function cropEncodeerBinnenBudget(canvas, stapIdx, cfg, callback){
  canvas.toBlob(function(blob){
    var laatsteStap = stapIdx >= cfg.stappen.length - 1;
    if (blob && blob.size <= cfg.maxBytes) callback(blob, true);
    else if (laatsteStap) callback(blob, false);
    else cropEncodeerBinnenBudget(canvas, stapIdx + 1, cfg, callback);
  }, 'image/jpeg', cfg.stappen[stapIdx]);
}

function cropBevestig(){
  var target = cropTarget;
  if (!target) return;
  var cfg = CROP_TARGETS[target];
  // Meten VÓÓR verbergen — een verborgen element geeft clientWidth/Height 0
  // terug, waardoor drawImage() niets tekent en canvas.toBlob() dat exporteert
  // als een volledig ZWARTE JPEG (geen alphakanaal in dat formaat). Dit was de
  // daadwerkelijke oorzaak van eerder gemelde zwarte-foto's/mislukte uploads.
  // De try/catch eromheen zorgt dat een onverwachte fout hier voortaan altijd
  // een zichtbare melding geeft i.p.v. een stille, oneindige "wordt verwerkt…".
  var vpW = cropVp.clientWidth, vpH = cropVp.clientHeight;
  var sx = -cropPanX / cropScale, sy = -cropPanY / cropScale;
  var sw = vpW / cropScale, sh = vpH / cropScale;
  document.getElementById('cropModal').classList.add('hidden');
  cropMelding(target, 'Foto wordt verwerkt…', false);
  try {
    if (!(sw > 0) || !(sh > 0)) throw new Error('leeg kader');
    var canvas = document.createElement('canvas');
    canvas.width = cfg.doelW; canvas.height = cfg.doelH;
    var ctx = canvas.getContext('2d');
    ctx.drawImage(cropImgEl, sx, sy, sw, sh, 0, 0, cfg.doelW, cfg.doelH);
    cropEncodeerBinnenBudget(canvas, 0, cfg, function(blob, gelukt){
      if (!gelukt) { cropMelding(target, 'Foto blijft te groot, ook na maximale compressie.', true); return; }
      if (target === 'haven') uploadHavenFoto(blob);
      else uploadAchtergrondFoto(blob, target === 'liggend');
    });
  } catch (e) {
    cropMelding(target, 'Bijsnijden mislukt (' + e.message + '). Probeer het opnieuw.', true);
  }
  cropTarget = null;
}

function uploadAchtergrondFoto(blob, liggend){
  var fd = new FormData(); fd.append('foto', blob, 'bg.jpg');
  var xhr = new XMLHttpRequest();
  xhr.open('POST', '/achtergrond/upload?slot=' + (liggend?'liggend':'staand') + '&pin=' + encodeURIComponent(localStorage.getItem(PIN_KEY) || ''));
  xhr.onload = function(){
    if (xhr.status === 200){ cropMelding('liggend', 'Opgeslagen.', false); achtergrondToepassen(); }
    else cropMelding('liggend', 'Upload mislukt (' + (xhr.status===403?'geen toegang':'opslag') + ').', true);
  };
  xhr.onerror = function(){ cropMelding('liggend', 'Upload mislukt (verbinding).', true); };
  xhr.send(fd);
}

function uploadHavenFoto(blob){
  var fd = new FormData(); fd.append('foto', blob, 'foto.jpg');
  var xhr = new XMLHttpRequest();
  xhr.open('POST', '/fotos/upload?pin=' + encodeURIComponent(localStorage.getItem(PIN_KEY) || ''));
  xhr.onload = function(){
    var d = {}; try { d = JSON.parse(xhr.responseText); } catch(e){}
    if (xhr.status === 200 && d.ok) { cropMelding('haven', 'Foto opgeslagen als ' + d.naam + '.', false); fotosLijst(); }
    else {
      var reden = xhr.status === 403 ? 'geen toegang' : xhr.status === 413 ? 'te groot' : 'opslag vol of ongeldig bestand';
      cropMelding('haven', 'Upload mislukt (' + reden + ').', true);
    }
  };
  xhr.onerror = function(){ cropMelding('haven', 'Upload mislukt (verbinding).', true); };
  xhr.send(fd);
}

document.getElementById('agInputLiggend').addEventListener('change', function(e){
  var f = e.target.files[0]; e.target.value = ''; if (f) openCrop(f, 'liggend');
});
document.getElementById('agInputStaand').addEventListener('change', function(e){
  var f = e.target.files[0]; e.target.value = ''; if (f) openCrop(f, 'staand');
});
document.getElementById('fotoInputHaven').addEventListener('change', function(e){
  var f = e.target.files[0]; e.target.value = ''; if (f) openCrop(f, 'haven');
});

// ─── FOTOS-tab: HAVEN-dashboardfoto's uploaden/lijst/verwijderen ───────────
function fotosInfo(){
  fetch('/fotos/info').then(function(r){ return r.json(); }).then(function(d){
    if (d.w && d.h) { CROP_TARGETS.haven.doelW = d.w; CROP_TARGETS.haven.doelH = d.h; }
    if (d.maxBytes) CROP_TARGETS.haven.maxBytes = d.maxBytes;
  }).catch(function(){});
}
function fotosLijst(){
  fetch('/fotos/lijst').then(function(r){ return r.json(); }).then(function(d){
    var fotos = d.fotos || [];
    var box = document.getElementById('fotosLijst');
    if (!fotos.length){ box.innerHTML = '<div style="color:var(--text-dim);font-size:.85rem;">Nog geen eigen foto\'s.</div>'; return; }
    box.innerHTML = fotos.map(function(f){
      return '<div class="filerow"><img src="/fotos/foto?naam=' + encodeURIComponent(f.naam) + '" style="width:44px;height:28px;object-fit:cover;border-radius:4px;flex:none;">' +
             '<div class="naam">' + esc(f.naam) + '</div>' +
             '<div class="grootte">' + fmtBytes(f.bytes) + '</div>' +
             '<button class="del" onclick="fotoVerwijderen(\'' + esc(f.naam) + '\')">WISSEN</button></div>';
    }).join('');
  }).catch(function(){});
}
function fotoVerwijderen(naam){
  if (needAuth()) return;
  var fd = new URLSearchParams(); fd.set('pin', localStorage.getItem(PIN_KEY) || ''); fd.set('naam', naam);
  fetch('/fotos/verwijder', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:fd.toString()})
    .then(function(r){ return r.json(); })
    .then(function(){ fotosLijst(); })
    .catch(function(){});
}

// ─── INSTELLINGEN-tab: boot/eigenaar-info, pincode wijzigen, gastcodes ─────
function escAttr(s){
  return String(s == null ? '' : s).replace(/&/g,'&amp;').replace(/"/g,'&quot;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}
function veldRij(label, attr, idx, waarde, numeriek){
  return '<label style="font-size:.72rem;color:var(--text-dim);display:block;margin-bottom:4px;">' + esc(label) + '</label>' +
         '<input data-' + attr + '="' + idx + '" class="inst' + attr + 'Veld" type="' + (numeriek?'number':'text') +
         '" value="' + escAttr(waarde) + '" style="width:100%;background:var(--surface2);border:1px solid var(--border);border-radius:8px;color:var(--text);font-size:.9rem;padding:10px;margin-bottom:10px;">';
}
function instellingenLaden(){ send({t:'instellingen_get'}); }
function renderInstellingen(){
  var d = instellingenData;
  document.getElementById('instBoot').innerHTML =
    veldRij('Zeilnummer', 'zn', 0, d.zeilnr, false) +
    veldRij('Apparaatnaam', 'nm', 0, d.naam, false) +
    (d.boot || []).map(function(v, i){ return veldRij(v.label, 'bi', i, v.waarde, v.num); }).join('');
  document.getElementById('instEig').innerHTML =
    (d.eig || []).map(function(v, i){ return veldRij(v.label, 'ei', i, v.waarde, false); }).join('');
}
function instellingenOpslaan(){
  if (needAuth()) return;
  var obj = {t:'instellingen_set'};
  var zn = document.querySelector('[data-zn]'); if (zn) obj.zeilnr = zn.value;
  var nm = document.querySelector('[data-nm]'); if (nm) obj.naam = nm.value;
  document.querySelectorAll('.instbiVeld').forEach(function(el){ obj['b' + el.getAttribute('data-bi')] = el.value; });
  document.querySelectorAll('.insteiVeld').forEach(function(el){ obj['e' + el.getAttribute('data-ei')] = el.value; });
  send(obj);
  var el = document.getElementById('instMelding');
  el.style.color = 'var(--green)'; el.textContent = 'Opgeslagen.';
  setTimeout(function(){ el.textContent = ''; }, 3000);
}

function pinWijzigen(){
  if (needAuth()) return;
  var oud = document.getElementById('pinOud').value;
  var nieuw = document.getElementById('pinNieuw').value;
  var el = document.getElementById('pinWijzMelding');
  if (oud.length !== 4 || nieuw.length !== 4){
    el.style.color = 'var(--red)'; el.textContent = 'Beide velden vereisen 4 cijfers.';
    return;
  }
  send({t:'pin_wijzig', oud:oud, nieuw:nieuw});
}
function renderPinRes(ok){
  var el = document.getElementById('pinWijzMelding');
  if (ok) {
    el.style.color = 'var(--green)'; el.textContent = 'Pincode gewijzigd.';
    document.getElementById('pinOud').value = ''; document.getElementById('pinNieuw').value = '';
  } else {
    el.style.color = 'var(--red)'; el.textContent = 'Wijzigen mislukt (huidige pincode onjuist?).';
  }
}

var GAST_DUUR_OPTIES = [
  {lbl:'ONBEPERKT', dagen:0}, {lbl:'1 DAG', dagen:1}, {lbl:'3 DAGEN', dagen:3},
  {lbl:'7 DAGEN', dagen:7}, {lbl:'30 DAGEN', dagen:30}
];
var GAST_NIVEAU_OPTIES = [NIVEAU_GAST, NIVEAU_LOGE, NIVEAU_DELER];
var gastDuurIdx = 0;
var gastNiveau = NIVEAU_GAST;
var gastBewerkIdx = -1;  // -1 = nieuwe code aanmaken, anders index in gastData die bewerkt wordt

function renderGastDuur(){
  document.getElementById('gastDuurKeuze').innerHTML = GAST_DUUR_OPTIES.map(function(o, i){
    return '<button class="mbtn' + (gastDuurIdx===i?' active':'') + '" onclick="gastDuurKiezen(' + i + ')">' + o.lbl + '</button>';
  }).join('');
}
function gastDuurKiezen(i){ gastDuurIdx = i; renderGastDuur(); }
function renderGastNiveau(){
  document.getElementById('gastNiveauKeuze').innerHTML = GAST_NIVEAU_OPTIES.map(function(n){
    return '<button class="mbtn' + (gastNiveau===n?' active':'') + '" onclick="gastNiveauKiezen(' + n + ')">' + NIVEAU_NAMEN[n] + '</button>';
  }).join('');
}
function gastNiveauKiezen(n){ gastNiveau = n; renderGastNiveau(); }
renderGastDuur();
renderGastNiveau();

function gastLaden(){ send({t:'gast_get'}); }
function renderGast(){
  var box = document.getElementById('gastLijst');
  if (!gastData.length){ box.innerHTML = '<div style="color:var(--text-dim);font-size:.85rem;">Nog geen gastcodes.</div>'; return; }
  box.innerHTML = gastData.map(function(g, i){
    return '<div class="filerow" onclick="gastBewerken(' + i + ')" style="cursor:pointer;">' +
           '<div class="naam">' + esc(g.code) + (g.naam ? (' — ' + esc(g.naam)) : '') + ' &middot; ' + esc(g.niveauNaam) + '</div>' +
           '<div class="grootte">' + esc(g.resterend) + '</div>' +
           '<button class="del" onclick="event.stopPropagation();gastVerwijderen(' + i + ')">WISSEN</button></div>';
  }).join('');
}
function gastFormReset(){
  gastBewerkIdx = -1;
  document.getElementById('gastNaam').value = '';
  gastDuurIdx = 0; gastNiveau = NIVEAU_GAST;
  renderGastDuur(); renderGastNiveau();
  document.getElementById('gastActieBtn').textContent = 'CODE AANMAKEN';
  document.getElementById('gastAnnuleerBtn').style.display = 'none';
  document.getElementById('gastMelding').textContent = '';
}
function gastBewerken(i){
  var g = gastData[i];
  if (!g) return;
  gastBewerkIdx = i;
  document.getElementById('gastNaam').value = g.naam || '';
  gastNiveau = g.niveau || NIVEAU_GAST;
  // De precieze resterende duur laat zich niet 1-op-1 terugvertalen naar een
  // van de vaste duur-knoppen — standaard op ONBEPERKT laten staan, tenzij de
  // code al verlopen/tijdelijk is; de gebruiker kiest bij bewerken gewoon
  // opnieuw een duur (telt vanaf nu, niet vanaf de oorspronkelijke aanmaak).
  gastDuurIdx = 0;
  renderGastDuur(); renderGastNiveau();
  document.getElementById('gastActieBtn').textContent = 'WIJZIGEN OPSLAAN';
  document.getElementById('gastAnnuleerBtn').style.display = '';
  var el = document.getElementById('gastMelding');
  el.style.color = ''; el.textContent = 'Code ' + g.code + ' bewerken — kies evt. een nieuwe geldigheidsduur (telt vanaf nu).';
}
function gastActie(){
  if (needAuth()) return;
  var naam = document.getElementById('gastNaam').value;
  var dagen = GAST_DUUR_OPTIES[gastDuurIdx].dagen;
  if (gastBewerkIdx < 0) send({t:'gast_toevoegen', naam:naam, dagen:dagen, niveau:gastNiveau});
  else send({t:'gast_bewerken', idx:gastBewerkIdx, naam:naam, dagen:dagen, niveau:gastNiveau});
}
function renderGastNieuw(msg){
  var el = document.getElementById('gastMelding');
  if (msg.ok) {
    el.style.color = 'var(--green)'; el.textContent = 'Nieuwe code: ' + msg.code + ' — geef door aan de gast.';
    gastFormReset();
    gastLaden();
  } else {
    var reden = msg.reden === 'tijd' ? 'tijd nog onbekend, kies ONBEPERKT'
              : msg.reden === 'vol'  ? 'maximum (20) bereikt' : 'onbekende fout';
    el.style.color = 'var(--red)'; el.textContent = 'Aanmaken mislukt (' + reden + ').';
  }
}
function renderGastBewerkt(msg){
  var el = document.getElementById('gastMelding');
  if (msg.ok) {
    el.style.color = 'var(--green)'; el.textContent = 'Gewijzigd.';
    gastFormReset();
    gastLaden();
  } else {
    var reden = msg.reden === 'tijd' ? 'tijd nog onbekend, kies ONBEPERKT' : 'onbekende fout';
    el.style.color = 'var(--red)'; el.textContent = 'Wijzigen mislukt (' + reden + ').';
  }
}
function gastVerwijderen(i){
  if (needAuth()) return;
  if (gastBewerkIdx === i) gastFormReset();
  send({t:'gast_verwijderen', idx:i});
}

achtergrondToepassen();
ladenPubliek();
ladenBericht();
connect();
</script>
</body>
</html>
)HTMLPAGE";
