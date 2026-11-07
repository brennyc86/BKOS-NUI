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
#bkosBanner{display:none;position:sticky;top:0;z-index:50;background:#1b2a35;color:#ffd58a;font-size:12px;padding:6px 10px;text-align:center}
#bkosBanner a{color:#7fe3ff}
</style>
"""

overlay = r"""
<div id="bkosBanner"></div>
<div id="bkosStart"><div class="kaart">
  <h1>BKOS</h1>
  <p>Bedien je boordcomputer. Kies hoe je verbindt.</p>
  <button id="bkosBtnBle" onclick="BKOS_UI.bluetooth()">Verbind via Bluetooth</button>
  <button class="sec" id="bkosBtnLaatste" style="display:none" onclick="BKOS_UI.laatste()"></button>
  <a class="knop sec" id="bkosBtnWifi" href="#" onclick="return BKOS_UI.wifi()">Verbind via WiFi</a>
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
  var LS='bkos_app_laatste';
  var device=null, info=null, geannuleerd=false;
  function $(i){return document.getElementById(i);}
  function melding(t){var e=$('bkosMelding'); e.textContent=t; e.style.display=t?'block':'none';}
  function lsGet(){try{return JSON.parse(localStorage.getItem(LS)||'null');}catch(e){return null;}}
  function lsSet(o){try{localStorage.setItem(LS,JSON.stringify(o));}catch(e){}}

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
      c[2].readValue().then(function(v){ try{ info=JSON.parse(new TextDecoder().decode(v)); lsSet({naam:info.naam,boot:info.boot,ip:info.ip,ap:info.ap}); banner(); }catch(e){} }).catch(function(){});
      txc.addEventListener('characteristicvaluechanged', ontvang);
      return txc.startNotifications();
    }).then(function(){
      s.readyState=1; $('bkosStart').style.display='none'; if(s.onopen) s.onopen({});
    }).catch(function(e){ if(s.onerror) s.onerror(e); sluit(); });
  }

  function banner(){
    var b=$('bkosBanner'), t='Bluetooth-modus: berichten, bestanden en foto’s werken alleen via WiFi.';
    var adr=info&&(info.ip||info.ap);
    if(adr) t+=' <a href="http://'+adr+'/">Open via WiFi ('+adr+')</a>';
    b.innerHTML=t; b.style.display='block';
  }

  // Zonder netwerk-endpoints (alleen WiFi): netjes falen i.p.v. 404's van github.io
  function fetchUit(){ window.fetch=function(){ return Promise.reject(new Error('Alleen via WiFi')); }; }

  window.BKOS_TRANSPORT={ maak:function(){ return device? new BleSocket(device) : null; } };

  window.BKOS_UI={
    bluetooth:function(){
      melding('');
      if(!navigator.bluetooth){ melding(uitleg()); return; }
      navigator.bluetooth.requestDevice({filters:[{services:[SVC]}]}).then(function(d){
        device=d; geannuleerd=false; fetchUit(); connect();
      }).catch(function(e){ if(e&&e.name!=='NotFoundError') melding('Bluetooth: '+(e.message||e)); else melding('Geen boordcomputer (master) gevonden. Staat Bluetooth-app aan in CONFIG → VERBINDINGEN?'); });
    },
    laatste:function(){
      var l=lsGet(); if(!l) return;
      var adr=l.ip||l.ap; if(adr) location.href='http://'+adr+'/';
    },
    wifi:function(){
      var l=lsGet(), adr=prompt('Adres van de boordcomputer:\n(hotspot van de boordcomputer: 192.168.4.1)', (l&&(l.ip||l.ap))||'192.168.4.1');
      if(adr){ location.href='http://'+adr.replace(/^https?:\/\//,'').replace(/\/.*$/,'')+'/'; }
      return false;
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
    var l=lsGet();
    if(l&&(l.ip||l.ap)){ var b=$('bkosBtnLaatste'); b.style.display='block'; b.textContent='Open via WiFi: '+(l.boot||l.naam||'laatste')+' ('+(l.ip||l.ap)+')'; }
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
