// webapp_haven_html.h — fotobeheerpagina (/fotos). Puur HTML/CSS/JS,
// geen externe dependencies (moet werken zonder internet). Verkleint een
// gekozen foto in de browser naar exact de doelresolutie (canvas "cover"-crop)
// en encodeert 'm als JPEG op een kwaliteit die binnen de servergrens past
// (maxUploadBytes, opgehaald via /fotos/info) vóórdat 'm geüpload wordt —
// dus vóór het versturen al zo klein als nodig, i.p.v. eerst het hele
// bestand versturen en pas daarna te ontdekken dat het te groot is. GEEN
// dithering meer (zie encodeerBinnenBudget hieronder): dat bleek de JPEG-
// compressie juist tegen te werken en foto's onnodig groot te maken.
#pragma once

const char WEBAPP_HAVEN_HTML[] PROGMEM = R"HTMLPAGE(<!DOCTYPE html>
<html lang="nl">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, viewport-fit=cover">
<title>BKOS HAVEN-foto's</title>
<style>
:root{
  --bg:#0c1a26; --surface:#152535; --surface2:#1e3347; --surface3:#243d55;
  --cyan:#00d4ff; --text:#cce4f0; --text-dim:#7a99b0;
  --green:#00dc64; --amber:#ffb400; --red:#ff3246; --border:#2a4560;
}
*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent;}
html,body{height:100%;}
body{
  background:var(--bg);color:var(--text);
  font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',system-ui,sans-serif;
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
header .titel{flex:1;min-width:0;}
header .titel b{display:block;font-size:1.02rem;color:var(--cyan);letter-spacing:.5px;}
header .titel span{display:block;font-size:.72rem;color:var(--text-dim);}
header a.terug{
  background:var(--surface2);border:1px solid var(--border);color:var(--text);
  border-radius:8px;padding:8px 12px;font-size:.8rem;text-decoration:none;flex:none;
}
section{margin-top:18px;}
section h2{
  font-size:.68rem;letter-spacing:2px;text-transform:uppercase;color:var(--text-dim);
  margin-bottom:8px;padding-left:2px;
}
#status{font-size:.8rem;color:var(--text-dim);line-height:1.6;padding:4px 2px;}
#status b{color:var(--text);}
#pinRow{display:flex;gap:8px;margin-bottom:14px;}
#pinInput{
  flex:1;background:var(--surface2);border:1px solid var(--border);border-radius:10px;
  color:var(--text);font-size:1.1rem;letter-spacing:.4em;text-align:center;padding:10px 0 10px 0.4em;
}
#ontgrendelBtn{
  flex:none;background:var(--cyan);color:#04121c;border:1px solid var(--cyan);
  border-radius:10px;padding:0 18px;font-size:.85rem;font-weight:600;
}
#uploadBtn, .filerow button.del{
  background:var(--surface2);border:1px solid var(--border);color:var(--text);
  border-radius:10px;padding:12px 16px;font-size:.88rem;font-weight:600;
}
#uploadBtn{width:100%;background:var(--cyan);color:#04121c;border-color:var(--cyan);margin-top:4px;}
#uploadBtn:disabled{background:var(--surface2);color:var(--text-dim);border-color:var(--border);}
#voortgang{
  height:8px;border-radius:4px;background:var(--surface2);overflow:hidden;margin-top:10px;display:none;
}
#voortgang b{display:block;height:100%;width:0;background:var(--cyan);transition:width .15s;}
#melding{font-size:.8rem;min-height:1.2em;margin-top:8px;}
#melding.fout{color:var(--red);}
#melding.ok{color:var(--green);}
.filerow{
  display:flex;align-items:center;gap:10px;
  background:var(--surface);border:1px solid var(--border);border-radius:8px;
  padding:10px 12px;margin-bottom:6px;
}
.filerow img.thumb{
  width:48px;height:29px;object-fit:cover;border-radius:5px;flex:none;
  background:var(--surface2);border:1px solid var(--border);
}
.filerow .naam{flex:1;font-size:.85rem;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}
.filerow .grootte{font-size:.72rem;color:var(--text-dim);flex:none;}
.filerow button.del{padding:6px 12px;font-size:.78rem;color:var(--red);border-color:var(--red);}
#leeg{color:var(--text-dim);font-size:.85rem;padding:8px 2px;}

#cropModal{
  position:fixed;inset:0;background:rgba(4,10,16,.94);z-index:60;
  display:flex;flex-direction:column;align-items:center;justify-content:center;
  padding:20px;
}
#cropModal.hidden{display:none;}
#cropViewport{
  width:min(88vw,420px);aspect-ratio:800/480;position:relative;overflow:hidden;
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
  <div class="titel">
    <b>HAVEN-foto's</b>
    <span id="hdrSub">laden…</span>
  </div>
  <a class="terug" href="/">&#8592; besturing</a>
</header>

<div class="wrap" id="gate">
  <section>
    <h2>Pincode vereist</h2>
    <p style="font-size:.8rem;color:var(--text-dim);margin-bottom:12px;">Dezelfde pincode als op het scherm van de boordcomputer en de afstandsbediening — eenmaal invoeren, ook geldig voor uploaden en verwijderen.</p>
    <div id="pinRow">
      <input id="pinInput" type="password" inputmode="numeric" pattern="[0-9]*" maxlength="4" placeholder="pincode" autocomplete="off">
      <button id="ontgrendelBtn" onclick="ontgrendel()">ONTGRENDEL</button>
    </div>
    <div id="pinErr" style="color:var(--red);font-size:.8rem;margin-top:8px;min-height:1.1em;"></div>
  </section>
</div>

<div class="wrap" id="inhoud" style="display:none">
  <section>
    <h2>Status</h2>
    <div id="status">—</div>
  </section>

  <section>
    <h2>Nieuwe foto toevoegen</h2>
    <input type="file" id="bestandInput" accept="image/*" style="display:none">
    <button id="uploadBtn" onclick="kiesBestand()">FOTO KIEZEN &amp; VERKLEINEN</button>
    <div id="voortgang"><b></b></div>
    <div id="melding"></div>
  </section>

  <section>
    <h2>Opgeslagen foto's</h2>
    <div id="lijst"><div id="leeg">Laden…</div></div>
  </section>

  <section>
    <a href="#" onclick="uitloggen();return false;" style="display:block;text-align:center;font-size:.8rem;color:var(--text-dim);padding:6px;">Uitloggen</a>
  </section>
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
var doelW = 800, doelH = 480, maxUploadBytes = 300 * 1024;

// Zelfde pincode/opslagsleutel als de afstandsbedieningspagina ("/") — eenmaal
// daar (of hier) ontgrendeld werkt overal, zonder opnieuw in te loggen.
var PIN_KEY = 'bkos_pin';

function esc(s){ var d=document.createElement('div'); d.textContent=String(s); return d.innerHTML; }
function fmtBytes(n){
  if (n < 1024) return n + ' B';
  return (n/1024).toFixed(1) + ' KB';
}
function melding(tekst, klasse){
  var el = document.getElementById('melding');
  el.textContent = tekst; el.className = klasse || '';
}

function info(){
  fetch('/fotos/info').then(function(r){ return r.json(); }).then(function(d){
    doelW = d.w; doelH = d.h; maxUploadBytes = d.maxBytes || maxUploadBytes;
    document.getElementById('cropViewport').style.aspectRatio = doelW + '/' + doelH;
    document.getElementById('hdrSub').textContent = doelW + '×' + doelH + ' · ' + d.aantal + ' foto\'s';
    document.getElementById('status').innerHTML =
      'Doelresolutie: <b>' + doelW + '×' + doelH + '</b><br>' +
      'Vrije opslag: <b>' + fmtBytes(d.vrij) + '</b><br>' +
      'Eigen foto\'s: <b>' + d.aantal + '</b> (vervangen de voorbeeldfoto\'s zodra er minstens één is)';
  }).catch(function(){ document.getElementById('status').textContent = 'Kon status niet ophalen.'; });
}

function lijst(){
  fetch('/fotos/lijst').then(function(r){ return r.json(); }).then(function(d){
    var fotos = d.fotos || [];
    var box = document.getElementById('lijst');
    if (!fotos.length){ box.innerHTML = '<div id="leeg">Nog geen eigen foto\'s — de ingebakken voorbeeldfoto\'s worden getoond.</div>'; return; }
    box.innerHTML = fotos.map(function(f){
      var src = '/fotos/foto?naam=' + encodeURIComponent(f.naam);
      return '<div class="filerow"><img class="thumb" src="' + src + '" loading="lazy" alt="">' +
             '<div class="naam">' + esc(f.naam) + '</div>' +
             '<div class="grootte">' + fmtBytes(f.bytes) + '</div>' +
             '<button class="del" onclick="verwijder(\'' + esc(f.naam) + '\')">VERWIJDER</button></div>';
    }).join('');
  }).catch(function(){});
}

function pin(){ return localStorage.getItem(PIN_KEY) || ''; }

// ─── Toegang: één pincode, gedeeld met "/" via localStorage ────────────────
function toonInhoud(){
  document.getElementById('gate').style.display = 'none';
  document.getElementById('inhoud').style.display = '';
  info(); lijst();
}
function toonGate(){
  document.getElementById('inhoud').style.display = 'none';
  document.getElementById('gate').style.display = '';
  document.getElementById('pinInput').value = '';
}
// Foto's (achtergrond én uploaden/verwijderen) zijn bewust eigenaar-only —
// een gastcode (niveau 1) is hier geldig genoeg om NIET als "onjuiste
// pincode" te worden afgewezen, maar krijgt toch geen toegang: de server
// staat upload/verwijder sowieso alleen aan de eigenaars-pincode toe (zie
// _pin_eigenaar() in webapp.ino), dus deze check voorkomt vooral een
// verwarrende "toegang gelukt, actie mislukt"-ervaring voor een gast.
function ontgrendel(){
  var v = document.getElementById('pinInput').value;
  if (v.length !== 4){ document.getElementById('pinErr').textContent = '4 cijfers invoeren'; return; }
  var fd = new URLSearchParams(); fd.set('pin', v);
  fetch('/verify', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:fd.toString()})
    .then(function(r){ return r.json(); })
    .then(function(d){
      if (d.ok && d.niveau >= 2){ localStorage.setItem(PIN_KEY, v); toonInhoud(); }
      else if (d.ok) document.getElementById('pinErr').textContent = 'Deze code geeft geen toegang tot foto\'s';
      else document.getElementById('pinErr').textContent = 'Onjuiste pincode';
    }).catch(function(){ document.getElementById('pinErr').textContent = 'Verbindingsfout'; });
}
document.getElementById('pinInput').addEventListener('keydown', function(e){
  if (e.key === 'Enter') ontgrendel();
});
function uitloggen(){ localStorage.removeItem(PIN_KEY); toonGate(); }

// Bij het laden: een eerder opgeslagen pincode (bv. via "/") in stilte
// bevestigen — geen nieuwe promptvraag als hij nog klopt.
(function(){
  var saved = pin();
  if (!saved){ toonGate(); return; }
  var fd = new URLSearchParams(); fd.set('pin', saved);
  fetch('/verify', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:fd.toString()})
    .then(function(r){ return r.json(); })
    .then(function(d){
      if (d.ok && d.niveau >= 2) { toonInhoud(); return; }
      // Een geldige maar niet-eigenaar (gast)code NIET uit localStorage
      // verwijderen — die code is nog steeds geldig voor de bedieningspagina
      // ("/"), enkel niet voor foto's. Alleen een echt ongeldige/verlopen
      // code wissen.
      if (!d.ok) localStorage.removeItem(PIN_KEY);
      toonGate();
      if (d.ok) document.getElementById('pinErr').textContent = 'Deze code geeft geen toegang tot foto\'s';
    })
    .catch(function(){ toonGate(); });
})();

function verwijder(naam){
  if (!confirm('Foto "' + naam + '" verwijderen?')) return;
  var fd = new URLSearchParams(); fd.set('pin', pin()); fd.set('naam', naam);
  fetch('/fotos/verwijder', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:fd.toString()})
    .then(function(r){ return r.json(); })
    .then(function(d){
      if (d.ok){ melding('Verwijderd.', 'ok'); info(); lijst(); }
      else melding('Verwijderen mislukt (onjuiste pincode?).', 'fout');
    }).catch(function(){ melding('Verwijderen mislukt.', 'fout'); });
}

function kiesBestand(){
  document.getElementById('bestandInput').click();
}

document.getElementById('bestandInput').addEventListener('change', function(e){
  var file = e.target.files[0];
  e.target.value = '';
  if (!file) return;
  verwerkEnUpload(file);
});

// Geen dithering meer vóór het JPEG-encoderen: Floyd-Steinberg voegt fijne
// pseudo-willekeurige ruis toe over de hele foto, en dat is precies wat een
// DCT-gebaseerde codec (JPEG) het slechtst kan comprimeren — een gedithered
// 800x480-foto werd daardoor makkelijk 3-10x groter dan dezelfde foto zonder
// dithering, en liep zo alsnog tegen de servergrens aan. JPEG's eigen
// kwantisatie doet al genoeg aan gladde verlopen; het risico op lichte
// bandvorming weegt niet op tegen een upload die gewoon niet lukt.
// Ruime, tot laag doorlopende ladder — verschillende browsers/besturings-
// systemen gebruiken elk hun eigen JPEG-encoder met een eigen kwaliteit-naar-
// bestandsgrootte-curve, dus dezelfde foto kan op de ene computer bij 0.32 al
// ruim onder de grens zitten en op een andere pas bij 0.15. Vandaar tot ver
// onder wat op één machine getest ooit nodig leek doorlopen, i.p.v. te vroeg
// opgeven en de foto toch (te groot) te versturen.
var KWALITEIT_STAPPEN = [0.75, 0.6, 0.45, 0.32, 0.22, 0.14, 0.08, 0.04];

// Probeert canvas.toBlob() op steeds lagere kwaliteit tot de blob binnen
// maxUploadBytes past. callback(blob, gelukt) — gelukt=false als zelfs de
// laagste stap nog te groot is (op een 800x480-foto in de praktijk zo goed
// als nooit); dan NIET alsnog uploaden (dat weet de server toch af te wijzen),
// gewoon meteen een duidelijke melding tonen.
function encodeerBinnenBudget(canvas, stapIdx, callback){
  var kwaliteit = KWALITEIT_STAPPEN[stapIdx];
  canvas.toBlob(function(blob){
    var laatsteStap = stapIdx >= KWALITEIT_STAPPEN.length - 1;
    if (blob && blob.size <= maxUploadBytes){
      callback(blob, true);
    } else if (laatsteStap){
      callback(blob, false);
    } else {
      encodeerBinnenBudget(canvas, stapIdx + 1, callback);
    }
  }, 'image/jpeg', kwaliteit);
}

// ─── Zelf bijsnijden: kader op vaste positie/verhouding (het kijkvenster
// #cropViewport, verhouding = doelW:doelH), de foto zelf schuift/zoomt
// eronder — precies zoals een profielfoto-crop op de meeste apps werkt. Bij
// zoom 100% (schuifminimum) dekt de foto het kader net volledig (de oude
// automatische "cover"-crop), verder inzoomen geeft een striktere keuze.
var cropImgEl, cropVp;
var cropNatW = 0, cropNatH = 0;   // echte fotoafmetingen
var cropBaseScale = 1;             // schaal bij zoom=100% (dekt het kader net)
var cropScale = 1;                 // daadwerkelijke schaal (baseScale × zoom%)
var cropPanX = 0, cropPanY = 0;    // positie linkerbovenhoek foto t.o.v. kader, in CSS-pixels
var cropSlepen = false, cropStartX = 0, cropStartY = 0, cropStartPanX = 0, cropStartPanY = 0;

function verwerkEnUpload(file){
  cropImgEl = document.getElementById('cropImg');
  cropVp = document.getElementById('cropViewport');
  var img = new Image();
  img.onload = function(){
    cropNatW = img.naturalWidth; cropNatH = img.naturalHeight;
    cropImgEl.src = img.src;
    document.getElementById('cropZoom').value = 100;
    document.getElementById('cropModal').classList.remove('hidden');
    // Wacht tot het kader zijn echte (verhouding-bepaalde) afmetingen heeft
    // vóór de basisschaal te berekenen — vlak na classList.remove al klaar.
    requestAnimationFrame(cropHerbereken);
  };
  img.onerror = function(){ melding('Kon de foto niet lezen.', 'fout'); };
  img.src = URL.createObjectURL(file);
}

function cropHerbereken(){
  var vpW = cropVp.clientWidth, vpH = cropVp.clientHeight;
  cropBaseScale = Math.max(vpW / cropNatW, vpH / cropNatH);
  var zoom = document.getElementById('cropZoom').value / 100;
  cropScale = cropBaseScale * zoom;
  var dispW = cropNatW * cropScale, dispH = cropNatH * cropScale;
  // Centreren + klemmen zodat de foto het kader altijd blijft dekken (geen
  // lege randen zichtbaar).
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
  var vpW = cropVp.clientWidth, vpH = cropVp.clientHeight;
  var midXvoor = (vpW / 2 - cropPanX) / cropScale;   // vasthouden welk fotopunt in het midden blijft
  var midYvoor = (vpH / 2 - cropPanY) / cropScale;
  cropScale = cropBaseScale * (e.target.value / 100);
  cropPanX = vpW / 2 - midXvoor * cropScale;
  cropPanY = vpH / 2 - midYvoor * cropScale;
  cropKlem();
  cropToon();
});

function cropPointerDown(e){
  cropSlepen = true;
  cropStartX = e.clientX; cropStartY = e.clientY;
  cropStartPanX = cropPanX; cropStartPanY = cropPanY;
  cropVp.setPointerCapture(e.pointerId);
}
function cropPointerMove(e){
  if (!cropSlepen) return;
  cropPanX = cropStartPanX + (e.clientX - cropStartX);
  cropPanY = cropStartPanY + (e.clientY - cropStartY);
  cropKlem();
  cropToon();
}
function cropPointerUp(){ cropSlepen = false; }
document.getElementById('cropViewport').addEventListener('pointerdown', cropPointerDown);
document.getElementById('cropViewport').addEventListener('pointermove', cropPointerMove);
document.getElementById('cropViewport').addEventListener('pointerup', cropPointerUp);
document.getElementById('cropViewport').addEventListener('pointercancel', cropPointerUp);

function cropAnnuleer(){
  document.getElementById('cropModal').classList.add('hidden');
  cropImgEl.src = '';
}

function cropBevestig(){
  // BELANGRIJK: de kader-afmetingen MOETEN gemeten worden vóórdat de modal
  // verborgen wordt — zodra #cropModal (of #cropViewport zelf) display:none
  // krijgt, geven clientWidth/clientHeight van elk kind-element 0 terug (niet
  // meer gelayout). Met vpW/vpH op 0 werd sw/sh ook 0, waardoor drawImage()
  // een leeg (transparant) canvas achterliet — en dat exporteert als een
  // volledig ZWARTE JPEG (geen alpha-ondersteuning in dat formaat). Dit was
  // de daadwerkelijke oorzaak van "de foto's die ik upload zijn zwart".
  var vpW = cropVp.clientWidth, vpH = cropVp.clientHeight;
  // Het kader ís het gekozen deel: linkerbovenhoek van het kader (0,0 in
  // kader-ruimte) komt overeen met fotopixel (-panX/scale, -panY/scale);
  // de kaderafmetingen in fotopixels zijn (vpW/scale, vpH/scale).
  var sx = -cropPanX / cropScale, sy = -cropPanY / cropScale;
  var sw = vpW / cropScale, sh = vpH / cropScale;

  document.getElementById('cropModal').classList.add('hidden');
  melding('Foto wordt verkleind…', '');
  document.getElementById('uploadBtn').disabled = true;

  var canvas = document.createElement('canvas');
  canvas.width = doelW; canvas.height = doelH;
  var ctx = canvas.getContext('2d');
  // cropImgEl staat al geladen in de modal (dat is precies wat de gebruiker
  // net zag) — drawImage gebruikt sowieso altijd de volle fotoresolutie,
  // ongeacht de CSS-weergavegrootte, dus geen nieuwe Image() nodig.
  ctx.drawImage(cropImgEl, sx, sy, sw, sh, 0, 0, doelW, doelH);
  encodeerBinnenBudget(canvas, 0, function(blob, gelukt){
    if (gelukt) uploadBlob(blob);
    else {
      melding('Deze foto blijft te groot, ook na maximale compressie. Probeer een andere foto.', 'fout');
      document.getElementById('uploadBtn').disabled = false;
    }
  });
}

function uploadBlob(blob){
  var fd = new FormData();
  fd.append('foto', blob, 'foto.jpg');
  var xhr = new XMLHttpRequest();
  xhr.open('POST', '/fotos/upload?pin=' + encodeURIComponent(pin()));
  var voortgang = document.getElementById('voortgang');
  voortgang.style.display = 'block';
  xhr.upload.onprogress = function(e){
    if (e.lengthComputable) voortgang.querySelector('b').style.width = (e.loaded / e.total * 100) + '%';
  };
  xhr.onload = function(){
    voortgang.style.display = 'none';
    document.getElementById('uploadBtn').disabled = false;
    var d = {};
    try { d = JSON.parse(xhr.responseText); } catch(e){}
    if (xhr.status === 200 && d.ok){
      melding('Foto opgeslagen als ' + d.naam + '.', 'ok');
      info(); lijst();
    } else {
      var reden = xhr.status === 403 ? 'onjuiste pincode'
                : xhr.status === 413 ? 'bestand nog te groot, ook na verkleinen'
                : 'opslag vol of bestand ongeldig';
      melding('Upload mislukt (' + reden + ').', 'fout');
    }
  };
  xhr.onerror = function(){
    voortgang.style.display = 'none';
    document.getElementById('uploadBtn').disabled = false;
    melding('Upload mislukt (verbinding).', 'fout');
  };
  xhr.send(fd);
}

// info()/lijst() draaien pas ná ontgrendelen (zie toonInhoud() hierboven) —
// vóór dat moment is er niets te laden, de gate staat nog in de weg.
</script>
</body>
</html>
)HTMLPAGE";
