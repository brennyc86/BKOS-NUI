#!/usr/bin/env python3
"""Genereert app/index.html (GitHub Pages) uit BKOS_NUI/webapp_html.h.

De pagina van de boordcomputer blijft de enige bron van de UI; dit script hangt er een
transportlaag onder (Web Bluetooth) en een startscherm voor. Opnieuw draaien na elke wijziging
in webapp_html.h:   python3 tools/maak_app.py
Ontwerp: docs/BKOS_APP_ONTWERP.md
"""
import re, sys, pathlib

root = pathlib.Path(__file__).resolve().parent.parent
bron = (root / "BKOS_NUI" / "webapp_html.h").read_text(encoding="utf-8")
m = re.search(r'R"HTMLPAGE\((.*)\)HTMLPAGE"', bron, re.S)
if not m:
    sys.exit("webapp_html.h: HTMLPAGE-blok niet gevonden")
html = m.group(1)

# 1. connect(): transport kiesbaar, en niets doen zolang er (nog) geen is gekozen
oud = "  try{ ws = new WebSocket(wsUrl()); }catch(e){ setTimeout(connect, 2000); return; }\n"
nieuw = ("  try{\n"
         "    if (window.BKOS_TRANSPORT) { ws = window.BKOS_TRANSPORT.maak(); if (!ws) return; }\n"
         "    else ws = new WebSocket(wsUrl());\n"
         "  }catch(e){ setTimeout(connect, 2000); return; }\n")
if oud not in html:
    sys.exit("connect() in webapp_html.h is gewijzigd: pas tools/maak_app.py aan")
html = html.replace(oud, nieuw, 1)

html = html.replace("<title>BKOS Afstandsbediening</title>", "<title>BKOS</title>", 1)

css = r"""
<style>
#bkosStart{position:fixed;inset:0;z-index:9999;background:#06131f;color:#e6f1f7;display:flex;align-items:center;justify-content:center;padding:16px;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
#bkosStart .kaart{width:100%;max-width:420px}
#bkosStart h1{margin:0 0 4px;font-size:28px;letter-spacing:.5px}
#bkosStart p{margin:0 0 18px;color:#8fb3c7;font-size:14px;line-height:1.4}
#bkosStart button,#bkosStart a.knop{display:block;width:100%;box-sizing:border-box;margin:0 0 10px;padding:16px;border:0;border-radius:12px;background:#0e3a55;color:#7fe3ff;font-size:17px;font-weight:600;text-align:center;text-decoration:none;cursor:pointer}
#bkosStart button.sec,#bkosStart a.sec{background:#0b2233;color:#9ec3d6}
#bkosStart .melding{margin:10px 0 0;padding:10px 12px;border-radius:10px;background:#2a1a12;color:#ffb27a;font-size:13px;display:none}
#bkosStart .hint{margin-top:16px;font-size:12px;color:#5f8399}
#bkosTegels{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px;margin:0 0 12px}
#bkosTegels .tegel{background:#0e3a55;color:#7fe3ff;border-radius:12px;padding:14px;text-align:left;min-height:110px;display:flex;flex-direction:column;justify-content:space-between;cursor:pointer;border:0;font:inherit;margin:0;width:auto}
#bkosTegels .tegel.uit{background:#0b2233;color:#6f93a8}
#bkosTegels .tegel .ico{font-size:26px}
#bkosTegels .tegel b{display:block;font-size:16px}
#bkosTegels .tegel small{display:block;font-size:11px;color:#8fb3c7;margin-top:2px}
#bkosTegels .tegel .dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:#555;margin-right:5px}
#bkosTegels .tegel.in .dot{background:#00dc64}
#bkosTegels a.wifi{font-size:11px;color:#7fe3ff;margin-top:6px;display:inline-block}
#bkosBanner{display:none;position:sticky;top:0;z-index:50;background:#1b2a35;color:#ffd58a;font-size:12px;padding:6px 10px;text-align:center}
#bkosBanner a{color:#7fe3ff}
</style>
"""

overlay = r"""
<div id="bkosBanner"></div>
<div id="bkosStart"><div class="kaart">
  <h1>BKOS</h1>
  <p id="bkosSub">Je boordcomputers. Tik op een tegel om te verbinden.</p>
  <div id="bkosTegels"></div>
  <button class="sec" id="bkosBtnZoek" onclick="BKOS_UI.zoek()">+ Boordcomputer zoeken</button>
  <div class="melding" id="bkosMelding"></div>
  <div class="hint" id="bkosHint"></div>
</div></div>
"""

js = r"""
<script>
(function(){
  var SVC='b4b05a01-5d3e-4c6b-9a77-0c4b05000001',
      RX ='b4b05a01-5d3e-4c6b-9a77-0c4b05000002',
      TX ='b4b05a01-5d3e-4c6b-9a77-0c4b05000003',
      INF='b4b05a01-5d3e-4c6b-9a77-0c4b05000004';
  var LS='bkos_app_boten';
  var device=null, info=null, huidigId=null, huidigSocket=null;
  var bekend={};   // id -> {id,naam,boot,ip,ap,dev,gezien}
  function $(i){return document.getElementById(i);}
  function esc(t){return String(t==null?'':t).replace(/[&<>"]/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c];});}
  function melding(t){var e=$('bkosMelding'); e.textContent=t; e.style.display=t?'block':'none';}
  function lsLees(){try{return JSON.parse(localStorage.getItem(LS)||'{}')||{};}catch(e){return {};}}
  function lsBewaar(){
    var o={}; Object.keys(bekend).forEach(function(k){ var b=bekend[k]; o[k]={id:b.id,naam:b.naam,boot:b.boot,ip:b.ip,ap:b.ap}; });
    try{localStorage.setItem(LS,JSON.stringify(o));}catch(e){}
  }

  // WebSocket-achtig object zodat de bestaande pagina ongewijzigd werkt.
  function BleSocket(dev){
    var s=this; s.readyState=0; s.onopen=s.onclose=s.onmessage=s.onerror=null;
    var rx=null, txc=null, buf=[], schrijfKetting=Promise.resolve(), dec=new TextDecoder('utf-8');
    function sluit(){ if(s.readyState===3) return; s.readyState=3; if(s.onclose) s.onclose({}); }
    s.close=function(){ try{ if(dev.gatt.connected) dev.gatt.disconnect(); }catch(e){} sluit(); };
    s.send=function(tekst){
      var b=new TextEncoder().encode(tekst), delen=[], N=19;       // 19+1 byte past in elke MTU
      for(var i=0;i<b.length;i+=N){ var d=new Uint8Array(Math.min(N,b.length-i)+1); d[0]=(i+N<b.length)?1:0; d.set(b.subarray(i,i+N),1); delen.push(d); }
      delen.forEach(function(d){ schrijfKetting=schrijfKetting.then(function(){ return rx.writeValueWithResponse(d); }).catch(function(){}); });
    };
    function ontvang(ev){
      var v=new Uint8Array(ev.target.value.buffer); if(!v.length) return;
      for(var i=1;i<v.length;i++) buf.push(v[i]);
      if(v[0]===0){ var tekst=dec.decode(new Uint8Array(buf)); buf=[]; if(s.onmessage) s.onmessage({data:tekst}); }
    }
    dev.addEventListener('gattserverdisconnected', sluit);
    dev.gatt.connect().then(function(srv){ return srv.getPrimaryService(SVC); }).then(function(svc){
      return Promise.all([svc.getCharacteristic(RX), svc.getCharacteristic(TX), svc.getCharacteristic(INF)]);
    }).then(function(c){
      rx=c[0]; txc=c[1];
      c[2].readValue().then(function(v){ try{
        info=JSON.parse(new TextDecoder().decode(v));
        var b=bekend[huidigId]; if(b){ b.naam=info.naam||b.naam; b.boot=info.boot; b.ip=info.ip; b.ap=info.ap; lsBewaar(); }
        banner();
      }catch(e){} }).catch(function(){});
      txc.addEventListener('characteristicvaluechanged', ontvang);
      return txc.startNotifications();
    }).then(function(){
      s.readyState=1; $('bkosStart').style.display='none'; if(s.onopen) s.onopen({});
    }).catch(function(e){ if(s.onerror) s.onerror(e); sluit(); });
  }

  function banner(){
    var b=$('botenBalk'); if(!b) return;
    $('botenNaam').textContent=(info&&(info.boot||info.naam))||'BKOS';
    $('botenBtn').style.display=''; $('botenBtn').onclick=function(){ BKOS_UI.terug(); };
    b.style.display='flex';
    var t='Bluetooth: berichten, bestanden en foto’s werken alleen via WiFi.', adr=info&&(info.ip||info.ap), x=$('bkosBanner');
    if(adr) t+=' <a href="http://'+adr+'/">Open via WiFi ('+adr+')</a>';
    x.innerHTML=t; x.style.display='block';
  }

  // Zonder netwerk-endpoints (alleen WiFi): netjes falen i.p.v. 404's van github.io
  function fetchUit(){ window.fetch=function(){ return Promise.reject(new Error('Alleen via WiFi')); }; }

  window.BKOS_TRANSPORT={ maak:function(){ return device? (huidigSocket=new BleSocket(device)) : null; } };

  // ── Tegels ──
  function registreer(dev){
    var b=bekend[dev.id]; if(!b) b=bekend[dev.id]={id:dev.id,naam:dev.name||'Boordcomputer'};
    if(dev.name) b.naam=dev.name;
    if(b.dev===dev) return b;
    b.dev=dev;
    if(dev.watchAdvertisements){
      dev.addEventListener('advertisementreceived',function(){ b.gezien=Date.now(); teken(); });
      try{ dev.watchAdvertisements().catch(function(){}); }catch(e){}
    }
    return b;
  }
  function teken(){
    var ids=Object.keys(bekend), h='', nu=Date.now();
    ids.forEach(function(id){
      var b=bekend[id], in_=b.gezien&&nu-b.gezien<20000, kan=!!b.dev;
      h+='<div><button class="tegel '+(in_?'in ':'')+(kan?'':'uit')+'" onclick="BKOS_UI.open(\''+esc(id)+'\')">'
        +'<span class="ico">&#9875;</span><span><b>'+esc(b.boot||b.naam)+'</b>'
        +'<small><span class="dot"></span>'+(in_?'in bereik':kan?'Bluetooth':'opnieuw koppelen')+'</small></span></button>'
        +((b.ip||b.ap)?'<a class="wifi" href="http://'+esc(b.ip||b.ap)+'/">via WiFi</a>':'')+'</div>';
    });
    $('bkosTegels').innerHTML=h;
    $('bkosSub').textContent=ids.length?'Je boordcomputers. Tik op een tegel om te verbinden.':'Nog geen boordcomputer gekoppeld. Tik hieronder op zoeken; dat hoeft per boordcomputer maar één keer.';
  }
  function scanStart(){
    if(!navigator.bluetooth) return;
    if(navigator.bluetooth.getDevices){
      navigator.bluetooth.getDevices().then(function(devs){ devs.forEach(registreer); teken(); }).catch(function(){});
    }
  }
  function verbind(b){
    if(!b||!b.dev){ melding('Deze boordcomputer moet opnieuw gekoppeld worden: tik op "Boordcomputer zoeken".'); return; }
    melding(''); device=b.dev; huidigId=b.id; fetchUit(); connect();
  }

  window.BKOS_UI={
    zoek:function(){
      melding('');
      if(!navigator.bluetooth){ melding(uitleg()); return; }
      navigator.bluetooth.requestDevice({filters:[{services:[SVC]}]}).then(function(d){
        var b=registreer(d); b.gezien=Date.now(); lsBewaar(); teken(); verbind(b);
      }).catch(function(e){ if(e&&e.name!=='NotFoundError') melding('Bluetooth: '+(e.message||e)); else melding('Geen boordcomputer (master) gevonden. Staat Bluetooth-app aan in CONFIG → VERBINDINGEN?'); });
    },
    open:function(id){ verbind(bekend[id]); },
    terug:function(){
      var s=huidigSocket; device=null; huidigSocket=null;
      if(s) s.close();
      $('bkosStart').style.display='flex'; $('bkosBanner').style.display='none'; $('botenBalk').style.display='none';
      teken();
    }
  };

  function uitleg(){
    var ua=navigator.userAgent;
    if(/iPhone|iPad|iPod/.test(ua)) return 'Safari ondersteunt geen Bluetooth. Open deze pagina in de browser Bluefy.';
    if(/Firefox/.test(ua)) return 'Firefox ondersteunt geen Bluetooth. Gebruik Chrome of Edge.';
    if(/Safari/.test(ua)&&!/Chrome|Chromium|Edg/.test(ua)) return 'Safari ondersteunt geen Bluetooth. Gebruik Chrome op je Mac.';
    return 'Deze browser ondersteunt geen Web Bluetooth. Gebruik Chrome of Edge (of Bluefy op iPhone).';
  }

  document.addEventListener('DOMContentLoaded',function(){
    var o=lsLees(); Object.keys(o).forEach(function(k){ bekend[k]=o[k]; });
    teken(); scanStart();
    setInterval(teken,5000);
    $('bkosHint').textContent = navigator.bluetooth ? 'Bluetooth vindt alleen boordcomputers in master-modus.' : uitleg();
  });
})();
</script>
"""

html = html.replace("</head>", css + js + "</head>", 1)
html = re.sub(r"(<body[^>]*>)", lambda mo: mo.group(1) + overlay, html, count=1)

(root / "app").mkdir(exist_ok=True)
(root / "app" / "index.html").write_text(html, encoding="utf-8")
print("app/index.html geschreven,", len(html), "bytes")
