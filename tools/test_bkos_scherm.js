// Headless test: rendert de BKOS-weergave met een nagebootste toestand en schrijft een PNG-bestand (raw RGBA).
// Gebruik: node tools/test_bkos_scherm.js uit.rgba [scherm]
const fs = require('fs'), vm = require('vm'), path = require('path');
const dir = path.join(__dirname, '..', 'BKOS_NUI');
let code = fs.readFileSync(path.join(dir, 'webapp_scherm_data.js'), 'utf8') + '\n' + fs.readFileSync(path.join(dir, 'webapp_scherm.js'), 'utf8');
const ctxObj = { console, Math, Uint32Array, Uint8ClampedArray, ArrayBuffer };
vm.createContext(ctxObj); vm.runInContext(code + '\nthis.BKS = BKS;', ctxObj);
const BKS = ctxObj.BKS;
const fake = { width: 0, height: 0, addEventListener() {}, getContext() { return { createImageData: (w, h) => ({ data: new Uint8ClampedArray(w * h * 4) }), putImageData(img) { fake.img = img; } }; } };
BKS.init(fake, {});
const pal = [[4,8,22],[10,20,44],[18,34,68],[28,50,96],[3,6,16],[3,6,16],[230,195,118],[155,128,72],[6,12,28],[36,52,82],[255,192,72]]
  .map(([r,g,b]) => ((r&0xF8)<<8)|((g&0xFC)<<3)|((b&0xF8)>>3));
BKS.zet({ t:'ui', pal, tijd:'14:07', wifi:2, hot:1, bel:1, upd:1, cat:0, mod:0, auto:1, im:3, ik:0, lt:[3,0,0,3,3], iw:1, ir:0,
  nav:[{l:'BERICHT',s:16}], mg:1, mc:2, md:1,
  w:{o:'Halfbewolkt',t:'14.3',tm:'17',wr:'ZW B4',ws:'stoot B6',wm:'7.2m/s',nu:'1.20m',ri:1,st:'Vlieland',mn:'EK +3',mf:0.22,gt:['HW 16:20 1.85m','LW 22:31 -0.40m']},
  vic:{v:1,a:['12.8V','45W','312Wh']} });
BKS.zet({ t:'state', m:0, l:2, h:0 });
BKS.zet({ t:'info', boot:'NYMBUS', ver:'0.2.261108.5' });
BKS.zet({ t:'paneel', items:[{naam:'USB',lb:'USB',ic:7,staat:2},{naam:'230V',lb:'230V',ic:8,staat:0},{naam:'TV',lb:'TV',ic:9,staat:1},{naam:'WATER',lb:'WATER',ic:10,staat:0},{naam:'DEK',lb:'DEK',ic:11,staat:2}] });
const sc = parseInt(process.argv[3] || '0');
if (sc === 20) {
  const raw = fs.readFileSync(process.argv[4]); BKS._zetFoto(new Uint16Array(raw.buffer, raw.byteOffset, raw.length / 2));
  BKS.zet({ t:'lampen', hoofdAanwezig:true, hoofdAan:true, kleur:0, overrule:-1, items:[{nr:1,naam:'SALON',aan:true},{nr:2,naam:'KEUKEN',aan:false},{nr:3,naam:'SLAAP',aan:false}] });
  BKS.zet({ t:'huispaneel', items:[{naam:'USB',lb:'USB',ic:7,staat:2,licht:false},{naam:'230V',lb:'230V',ic:8,staat:0,licht:false},{naam:'TV',lb:'TV',ic:9,staat:0,licht:false},{naam:'WATER',lb:'WATER',ic:10,staat:2,licht:false},{naam:'E_DEK',lb:'E_DEK',ic:11,staat:0,licht:true}] });
}
BKS.teken(sc, 'TEST');
fs.writeFileSync(process.argv[2] || 'uit.rgba', Buffer.from(fake.img.data.buffer));
console.log('ok');
