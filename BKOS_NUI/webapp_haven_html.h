// webapp_haven_html.h — HAVEN-fotobeheerpagina (/haven). Puur HTML/CSS/JS,
// geen externe dependencies (moet werken zonder internet). Verkleint een
// gekozen foto in de browser naar exact de doelresolutie (canvas "cover"-crop)
// en encodeert 'm als JPEG op een kwaliteit die binnen de servergrens past
// (maxUploadBytes, opgehaald via /haven/info) vóórdat 'm geüpload wordt —
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
.filerow .naam{flex:1;font-size:.85rem;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}
.filerow .grootte{font-size:.72rem;color:var(--text-dim);flex:none;}
.filerow button.del{padding:6px 12px;font-size:.78rem;color:var(--red);border-color:var(--red);}
#leeg{color:var(--text-dim);font-size:.85rem;padding:8px 2px;}
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

<div class="wrap">
  <section>
    <h2>Status</h2>
    <div id="status">—</div>
  </section>

  <section>
    <h2>Nieuwe foto toevoegen</h2>
    <div id="pinRow">
      <input id="pinInput" inputmode="numeric" pattern="[0-9]*" maxlength="4" placeholder="pincode" autocomplete="off">
    </div>
    <input type="file" id="bestandInput" accept="image/*" style="display:none">
    <button id="uploadBtn" onclick="kiesBestand()">FOTO KIEZEN &amp; VERKLEINEN</button>
    <div id="voortgang"><b></b></div>
    <div id="melding"></div>
  </section>

  <section>
    <h2>Opgeslagen foto's</h2>
    <div id="lijst"><div id="leeg">Laden…</div></div>
  </section>
</div>

<script>
'use strict';
var doelW = 800, doelH = 480, maxUploadBytes = 300 * 1024;

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
  fetch('/haven/info').then(function(r){ return r.json(); }).then(function(d){
    doelW = d.w; doelH = d.h; maxUploadBytes = d.maxBytes || maxUploadBytes;
    document.getElementById('hdrSub').textContent = doelW + '×' + doelH + ' · ' + d.aantal + ' foto\'s';
    document.getElementById('status').innerHTML =
      'Doelresolutie: <b>' + doelW + '×' + doelH + '</b><br>' +
      'Vrije opslag: <b>' + fmtBytes(d.vrij) + '</b><br>' +
      'Eigen foto\'s: <b>' + d.aantal + '</b> (vervangen de voorbeeldfoto\'s zodra er minstens één is)';
  }).catch(function(){ document.getElementById('status').textContent = 'Kon status niet ophalen.'; });
}

function lijst(){
  fetch('/haven/lijst').then(function(r){ return r.json(); }).then(function(d){
    var fotos = d.fotos || [];
    var box = document.getElementById('lijst');
    if (!fotos.length){ box.innerHTML = '<div id="leeg">Nog geen eigen foto\'s — de ingebakken voorbeeldfoto\'s worden getoond.</div>'; return; }
    box.innerHTML = fotos.map(function(f){
      return '<div class="filerow"><div class="naam">' + esc(f.naam) + '</div>' +
             '<div class="grootte">' + fmtBytes(f.bytes) + '</div>' +
             '<button class="del" onclick="verwijder(\'' + esc(f.naam) + '\')">VERWIJDER</button></div>';
    }).join('');
  }).catch(function(){});
}

function pin(){ return document.getElementById('pinInput').value; }

function verwijder(naam){
  if (pin().length !== 4){ melding('Voer eerst de 4-cijferige pincode in.', 'fout'); return; }
  if (!confirm('Foto "' + naam + '" verwijderen?')) return;
  var fd = new URLSearchParams(); fd.set('pin', pin()); fd.set('naam', naam);
  fetch('/haven/verwijder', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:fd.toString()})
    .then(function(r){ return r.json(); })
    .then(function(d){
      if (d.ok){ melding('Verwijderd.', 'ok'); info(); lijst(); }
      else melding('Verwijderen mislukt (onjuiste pincode?).', 'fout');
    }).catch(function(){ melding('Verwijderen mislukt.', 'fout'); });
}

function kiesBestand(){
  if (pin().length !== 4){ melding('Voer eerst de 4-cijferige pincode in.', 'fout'); return; }
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
var KWALITEIT_STAPPEN = [0.75, 0.6, 0.45, 0.32, 0.22];

// Probeert canvas.toBlob() op steeds lagere kwaliteit tot de blob binnen
// maxUploadBytes past (of de laagste stap bereikt is — dan die maar, beter
// een zichtbaar iets grovere foto dan een upload die blijft mislukken).
function encodeerBinnenBudget(canvas, stapIdx, callback){
  var kwaliteit = KWALITEIT_STAPPEN[stapIdx];
  canvas.toBlob(function(blob){
    var laatsteStap = stapIdx >= KWALITEIT_STAPPEN.length - 1;
    if (blob && (blob.size <= maxUploadBytes || laatsteStap)){
      callback(blob);
    } else {
      encodeerBinnenBudget(canvas, stapIdx + 1, callback);
    }
  }, 'image/jpeg', kwaliteit);
}

function verwerkEnUpload(file){
  melding('Foto wordt verkleind…', '');
  document.getElementById('uploadBtn').disabled = true;
  var img = new Image();
  img.onload = function(){
    var canvas = document.createElement('canvas');
    canvas.width = doelW; canvas.height = doelH;
    var ctx = canvas.getContext('2d');
    // "Cover"-crop: uitvullen zonder vervorming, overtollige randen afsnijden.
    var schaal = Math.max(doelW / img.width, doelH / img.height);
    var sw = doelW / schaal, sh = doelH / schaal;
    var sx = (img.width - sw) / 2, sy = (img.height - sh) / 2;
    ctx.drawImage(img, sx, sy, sw, sh, 0, 0, doelW, doelH);

    encodeerBinnenBudget(canvas, 0, function(blob){ uploadBlob(blob); });
  };
  img.onerror = function(){
    melding('Kon de foto niet lezen.', 'fout');
    document.getElementById('uploadBtn').disabled = false;
  };
  img.src = URL.createObjectURL(file);
}

function uploadBlob(blob){
  var fd = new FormData();
  fd.append('foto', blob, 'foto.jpg');
  var xhr = new XMLHttpRequest();
  xhr.open('POST', '/haven/upload?pin=' + encodeURIComponent(pin()));
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

info(); lijst();
</script>
</body>
</html>
)HTMLPAGE";
