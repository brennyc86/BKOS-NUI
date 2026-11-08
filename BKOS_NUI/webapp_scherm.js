// webapp_scherm.js — BKOS-weergave: het scherm van de boordcomputer pixel-voor-pixel in de browser.
//
// Dit is een 1-op-1 port van de teken-code in de firmware (Arduino_GFX-primitieven, het 5x7-font,
// nav_bar.ino, screen_main.ino). Dezelfde 800x480 coordinaten, dezelfde RGB565-kleuren (het palet
// komt live van het apparaat), dezelfde iconen. Wijzig je het echte scherm, pas dan dit bestand
// mee aan. Data (bootmodellen, font, paletten) wordt gegenereerd door
// tools/genereer_bkos_scherm_data.py — niet met de hand wijzigen.
//
// Gebruik: BKS.init(canvas, opties); BKS.zet(bericht) bij elk WebSocket-bericht; BKS.teken().
var BKS = (function () {
  'use strict';
  var W = 800, H = 480, SB_H = 42, NAV_H = 42, NAV_Y = H - NAV_H, CONTENT_Y = SB_H, CONTENT_H = H - SB_H - NAV_H;
  var KNOP_R = 8;

  // ── Schermnummers (app_state.h) ────────────────────────────────────────────
  var SC = { MAIN: 0, IO: 1, METEO: 2, CONFIG: 3, WIFI: 4, INFO: 5, APPS: 8, LUA: 9, VICTRON: 11, NETWERK: 12, BRUG: 13,
             MELDING: 14, PANEEL: 15, BERICHT: 16, HAVEN: 20 };

  // ── RGB565 ─────────────────────────────────────────────────────────────────
  function RGB(r, g, b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3); }
  var LUT = new Uint32Array(65536);
  (function () {
    for (var c = 0; c < 65536; c++) {
      var r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
      LUT[c] = (0xFF000000 | (((b << 3) | (b >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((r << 3) | (r >> 2))) >>> 0;
    }
  })();

  // Vaste kleuren (ui_colors.h)
  var C_BLUE = RGB(80, 140, 255), C_GREEN = RGB(0, 220, 100), C_RED_BRIGHT = RGB(255, 50, 70), C_ORANGE = RGB(255, 150, 0),
      C_AMBER = RGB(255, 180, 0), C_WHITE = 0xFFFF, C_GRAY = RGB(80, 100, 130), C_BLACK = 0,
      C_HAVEN = RGB(60, 100, 255), C_ZEILEN = RGB(0, 200, 170), C_MOTOR = RGB(255, 120, 0), C_ANKER = RGB(140, 100, 40),
      C_LIGHT_COOLING = RGB(160, 80, 0), C_LIGHT_PENDING = RGB(180, 160, 50), C_LIGHT_ON = RGB(255, 250, 180),
      C_LIGHT_ON_RED = RGB(255, 30, 50), C_LIGHT_ON_GRN = RGB(20, 255, 80);
  // Thema-kleuren (komen van het apparaat)
  var C_BG, C_SURFACE, C_SURFACE2, C_SURFACE3, C_STATUSBAR, C_NAVBAR, C_TEXT, C_TEXT_DIM, C_TEXT_DARK, C_DARK_GRAY, C_CYAN;
  function zetPalet(p) {
    C_BG = p[0]; C_SURFACE = p[1]; C_SURFACE2 = p[2]; C_SURFACE3 = p[3]; C_STATUSBAR = p[4]; C_NAVBAR = p[5];
    C_TEXT = p[6]; C_TEXT_DIM = p[7]; C_TEXT_DARK = p[8]; C_DARK_GRAY = p[9]; C_CYAN = p[10];
  }
  // Standaardpalet tot het apparaat het zijne stuurt: RAN
  zetPalet([RGB(4, 8, 22), RGB(10, 20, 44), RGB(18, 34, 68), RGB(28, 50, 96), RGB(3, 6, 16), RGB(3, 6, 16),
            RGB(230, 195, 118), RGB(155, 128, 72), RGB(6, 12, 28), RGB(36, 52, 82), RGB(255, 192, 72)]);

  // ── Software-framebuffer + Arduino_GFX-primitieven ─────────────────────────
  var fb = new Uint32Array(W * H);
  function px(x, y, c) { if (x >= 0 && y >= 0 && x < W && y < H) fb[y * W + x] = LUT[c & 0xFFFF]; }
  function hl(x, y, w, c) {
    if (w < 0) { x += w + 1; w = -w; }
    if (y < 0 || y >= H || w <= 0) return;
    var x1 = Math.min(W, x + w); x = Math.max(0, x); var v = LUT[c & 0xFFFF], o = y * W;
    for (var i = x; i < x1; i++) fb[o + i] = v;
  }
  function vl(x, y, h, c) {
    if (h < 0) { y += h + 1; h = -h; }
    if (x < 0 || x >= W || h <= 0) return;
    var y1 = Math.min(H, y + h); y = Math.max(0, y); var v = LUT[c & 0xFFFF];
    for (var j = y; j < y1; j++) fb[j * W + x] = v;
  }
  function fillRect(x, y, w, h, c) {
    if (w < 0) { x += w + 1; w = -w; }
    if (h < 0) { y += h + 1; h = -h; }
    var x1 = Math.min(W, x + w), y1 = Math.min(H, y + h), v = LUT[c & 0xFFFF];
    x = Math.max(0, x); y = Math.max(0, y);
    for (var j = y; j < y1; j++) { var o = j * W; for (var i = x; i < x1; i++) fb[o + i] = v; }
  }
  function drawRect(x, y, w, h, c) { hl(x, y, w, c); hl(x, y + h - 1, w, c); vl(x, y, h, c); vl(x + w - 1, y, h, c); }
  function drawLine(x0, y0, x1, y1, c) {
    if (x0 === x1) { if (y0 > y1) { var t = y0; y0 = y1; y1 = t; } vl(x0, y0, y1 - y0 + 1, c); return; }
    if (y0 === y1) { if (x0 > x1) { var t2 = x0; x0 = x1; x1 = t2; } hl(x0, y0, x1 - x0 + 1, c); return; }
    var steep = Math.abs(y1 - y0) > Math.abs(x1 - x0), t3;
    if (steep) { t3 = x0; x0 = y0; y0 = t3; t3 = x1; x1 = y1; y1 = t3; }
    if (x0 > x1) { t3 = x0; x0 = x1; x1 = t3; t3 = y0; y0 = y1; y1 = t3; }
    var dx = x1 - x0, dy = Math.abs(y1 - y0), err = dx >> 1, step = y0 < y1 ? 1 : -1;
    for (; x0 <= x1; x0++) {
      if (steep) px(y0, x0, c); else px(x0, y0, c);
      err -= dy; if (err < 0) { err += dx; y0 += step; }
    }
  }
  function ellipseDraw(x, y, rx, ry, corner, c) {
    if (rx < 0 || ry < 0 || (rx === 0 && ry === 0)) return;
    if (ry === 0) { hl(x - rx, y, (ry << 2) + 1, c); return; }
    if (rx === 0) { vl(x, y - ry, (rx << 2) + 1, c); return; }
    var xt, yt, s, i, rx2 = rx * rx, ry2 = ry * ry;
    i = -1; xt = 0; yt = ry; s = (ry2 << 1) + rx2 * (1 - (ry << 1));
    do {
      while (s < 0) s += ry2 * ((++xt << 2) + 2);
      if (corner & 1) hl(x - xt, y - yt, xt - i, c);
      if (corner & 2) hl(x + i + 1, y - yt, xt - i, c);
      if (corner & 4) hl(x + i + 1, y + yt, xt - i, c);
      if (corner & 8) hl(x - xt, y + yt, xt - i, c);
      i = xt; s -= (--yt) * rx2 << 2;
    } while (ry2 * xt <= rx2 * yt);
    i = -1; yt = 0; xt = rx; s = (rx2 << 1) + ry2 * (1 - (rx << 1));
    do {
      while (s < 0) s += rx2 * ((++yt << 2) + 2);
      if (corner & 1) vl(x - xt, y - yt, yt - i, c);
      if (corner & 2) vl(x + xt, y - yt, yt - i, c);
      if (corner & 4) vl(x + xt, y + i + 1, yt - i, c);
      if (corner & 8) vl(x - xt, y + i + 1, yt - i, c);
      i = yt; s -= (--xt) * ry2 << 2;
    } while (rx2 * yt <= ry2 * xt);
  }
  function ellipseFill(x, y, rx, ry, corners, delta, c) {
    if (rx < 0 || ry < 0 || (rx === 0 && ry === 0)) return;
    if (ry === 0) { hl(x - rx, y, (ry << 2) + 1, c); return; }
    if (rx === 0) { vl(x, y - ry, (rx << 2) + 1, c); return; }
    var xt, yt, i, rx2 = rx * rx, ry2 = ry * ry, s;
    hl(x - rx, y, (rx << 1) + 1, c);
    i = 0; yt = 0; xt = rx; s = (rx2 << 1) + ry2 * (1 - (rx << 1));
    do {
      while (s < 0) s += rx2 * ((++yt << 2) + 2);
      if (corners & 1) fillRect(x - xt, y - yt, (xt << 1) + 1 + delta, yt - i, c);
      if (corners & 2) fillRect(x - xt, y + i + 1, (xt << 1) + 1 + delta, yt - i, c);
      i = yt; s -= (--xt) * ry2 << 2;
    } while (rx2 * yt <= ry2 * xt);
    xt = 0; yt = ry; s = (ry2 << 1) + rx2 * (1 - (ry << 1));
    do {
      while (s < 0) s += ry2 * ((++xt << 2) + 2);
      if (corners & 1) hl(x - xt, y - yt, (xt << 1) + 1 + delta, c);
      if (corners & 2) hl(x - xt, y + yt, (xt << 1) + 1 + delta, c);
      s -= (--yt) * rx2 << 2;
    } while (ry2 * xt <= rx2 * yt);
  }
  function drawCircle(x, y, r, c) { ellipseDraw(x, y, r, r, 0xF, c); }
  function fillCircle(x, y, r, c) { ellipseFill(x, y, r, r, 3, 0, c); }
  function drawRoundRect(x, y, w, h, r, c) {
    var mr = (Math.min(w, h) / 2) | 0; if (r > mr) r = mr;
    hl(x + r, y, w - 2 * r, c); hl(x + r, y + h - 1, w - 2 * r, c); vl(x, y + r, h - 2 * r, c); vl(x + w - 1, y + r, h - 2 * r, c);
    ellipseDraw(x + r, y + r, r, r, 1, c); ellipseDraw(x + w - r - 1, y + r, r, r, 2, c);
    ellipseDraw(x + w - r - 1, y + h - r - 1, r, r, 4, c); ellipseDraw(x + r, y + h - r - 1, r, r, 8, c);
  }
  function fillRoundRect(x, y, w, h, r, c) {
    var mr = (Math.min(w, h) / 2) | 0; if (r > mr) r = mr;
    fillRect(x, y + r, w, h - (r << 1), c);
    ellipseFill(x + r, y + r, r, r, 1, w - 2 * r - 1, c);
    ellipseFill(x + r, y + h - r - 1, r, r, 2, w - 2 * r - 1, c);
  }
  function fillTriangle(x0, y0, x1, y1, x2, y2, c) {
    var a, b, y, last, t;
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y1 > y2) { t = y2; y2 = y1; y1 = t; t = x2; x2 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y0 === y2) {
      a = b = x0;
      if (x1 < a) a = x1; else if (x1 > b) b = x1;
      if (x2 < a) a = x2; else if (x2 > b) b = x2;
      hl(a, y0, b - a + 1, c); return;
    }
    last = (y1 === y2) ? y1 : y1 - 1;
    for (y = y0; y <= last; y++) {
      a = x0 + Math.trunc((x1 - x0) * (y - y0) / (y1 - y0));
      b = x0 + Math.trunc((x2 - x0) * (y - y0) / (y2 - y0));
      if (a > b) { t = a; a = b; b = t; }
      hl(a, y, b - a + 1, c);
    }
    for (; y <= y2; y++) {
      a = x1 + Math.trunc((x2 - x1) * (y - y1) / (y2 - y1));
      b = x0 + Math.trunc((x2 - x0) * (y - y0) / (y2 - y0));
      if (a > b) { t = a; a = b; b = t; }
      hl(a, y, b - a + 1, c);
    }
  }
  // fillArc (Arduino_GFX::fillArcHelper) — alleen gebruikt voor het AUTO-icoon
  function fillArcHelper(cx, cy, oradius, iradius, start, end, color) {
    var DEG = Math.PI / 180;
    if (start === 90 || start === 180 || start === 270 || start === 360) start -= 0.1;
    if (end === 90 || end === 180 || end === 270 || end === 360) end -= 0.1;
    var s_cos = Math.cos(start * DEG), e_cos = Math.cos(end * DEG);
    var sslope = s_cos / Math.sin(start * DEG), eslope = e_cos / Math.sin(end * DEG);
    var swidth = 0.5 / s_cos, ewidth = -0.5 / e_cos;
    --iradius;
    var ir2 = iradius * iradius + iradius, or2 = oradius * oradius + oradius;
    var start180 = !(start < 180), end180 = end < 180;
    var reversed = start + 180 < end || (end < start && start < end + 180);
    var xs = -oradius, y = -oradius, ye = oradius, xe = oradius + 1;
    if (!reversed) {
      if ((end >= 270 || end < 90) && (start >= 270 || start < 90)) xs = 0;
      else if (end < 270 && end >= 90 && start < 270 && start >= 90) xe = 1;
      if (end >= 180 && start >= 180) ye = 0; else if (end < 180 && start < 180) y = 0;
    }
    do {
      var y2 = y * y, x = xs;
      if (x < 0) { while (x * x + y2 >= or2) ++x; if (xe !== 1) xe = 1 - x; }
      var ysslope = (y + swidth) * sslope, yeslope = (y + ewidth) * eslope, len = 0;
      do {
        var flg1 = start180 !== (x <= ysslope), flg2 = end180 !== (x <= yeslope), distance = x * x + y2;
        if (distance >= ir2 && ((flg1 && flg2) || (reversed && (flg1 || flg2))) && x !== xe && distance < or2) ++len;
        else {
          if (len) { hl(cx + x - len, cy + y, len, color); len = 0; }
          if (distance >= or2) break;
          if (x < 0 && distance < ir2) x = -x;
        }
      } while (++x <= xe);
    } while (++y <= ye);
  }
  function fillArc(x, y, r1, r2, start, end, color) {
    // Arduino_GFX::fillArc — zie Arduino_GFX.cpp
    var t;
    if (r1 < r2) { t = r1; r1 = r2; r2 = t; }
    if (r1 < 1) r1 = 1; if (r2 < 1) r2 = 1;
    var equal = Math.abs(start - end) < 1.19e-7;
    start = start % 360; end = end % 360; if (start < 0) start += 360; if (end < 0) end += 360;
    fillArcHelper(x, y, r1, r2, start, end, color);
    if (!equal && Math.abs(start - end) <= 0.0001) { fillArcHelper(x, y, r1, r2, 0, 360, color); }
  }

  // ── Tekst (Adafruit-5x7-font, zoals Arduino_GFX::drawChar) ─────────────────
  var T = { size: 1, color: 0xFFFF, cx: 0, cy: 0 };
  function setTextSize(s) { T.size = s; }
  function setTextColor(c) { T.color = c; }
  function setCursor(x, y) { T.cx = x; T.cy = y; }
  function drawChar(x, y, ch, color, size) {
    var f = BK_FONT, base = ch * 5;
    for (var i = 0; i < 5; i++) {
      var line = f[base + i];
      for (var j = 0; j < 8; j++, line >>= 1) {
        if (line & 1) { if (size === 1) px(x + i, y + j, color); else fillRect(x + i * size, y + j * size, size, size, color); }
      }
    }
  }
  function print(s) {
    for (var i = 0; i < s.length; i++) {
      var ch = s.charCodeAt(i) & 0xFF;
      if (ch === 10) { T.cx = 0; T.cy += 8 * T.size; continue; }
      if (ch === 13) continue;
      drawChar(T.cx, T.cy, ch, T.color, T.size);
      T.cx += 6 * T.size;
    }
  }
  function tekst(s, x, y, size, color) { setTextSize(size); setTextColor(color); setCursor(x, y); print(s); }
  var DEG = '\xF7';

  // ── Toestand (komt van het apparaat) ───────────────────────────────────────
  var S = {
    ui: null, modus: 0, licht: 0, h: 0, scherm: 0, paneel: [], bootnaam: '', versie: '', klok: '--:--',
    toegang: 0,
    web: { ing: false, verb: false, sync: true },   // alleen webapp: ingelogd / verbonden / synchroon (gezet door de pagina)
    info: null,                                     // INFO-scherm (bericht 'info_scherm' van de boordcomputer)
    ber: null, berFlits: null                       // BERICHT-scherm: {presets, aan, nood, anker} + bevestigingsbalk
  };

  // ── Statusbalk (nav_bar.ino) ───────────────────────────────────────────────
  function ico(v, s) { return Math.trunc(v * s + (v >= 0 ? 0.5 : -0.5)); }
  function wifiIcon(x, cy, s) {
    var ui = S.ui || {}, y = cy + ico(9, s), staat = ui.wifi || 0;
    var bw = Math.max(1, ico(4, s)), bg = Math.max(1, ico(2, s)), bh0 = [6, 10, 14, 18];
    var kleur = staat === 2 ? C_GREEN : staat === 1 ? C_AMBER : RGB(80, 90, 100), dimkl = RGB(38, 48, 60);
    for (var b = 0; b < 4; b++) {
      var lit = staat === 2 || (staat === 1 && b < 2), bh = ico(bh0[b], s);
      fillRect(x + b * (bw + bg), y - bh, bw, bh, lit ? kleur : dimkl);
    }
    if (staat === 0) {
      var ccx = x + ico(11, s), ccy = y - ico(10, s), o3 = ico(3, s), o4 = ico(4, s), o5 = ico(5, s);
      drawLine(ccx - o4, ccy - o4, ccx + o4, ccy + o4, C_RED_BRIGHT); drawLine(ccx + o4, ccy - o4, ccx - o4, ccy + o4, C_RED_BRIGHT);
      drawLine(ccx - o3, ccy - o4, ccx + o5, ccy + o4, C_RED_BRIGHT); drawLine(ccx + o5, ccy - o4, ccx - o3, ccy + o4, C_RED_BRIGHT);
    }
  }
  function hotspotIcon(x, cy, s) {
    var c = (S.ui && S.ui.hot) ? C_GREEN : RGB(55, 70, 90), cx = x + ico(7, s);
    fillCircle(cx, cy, ico(2, s), c); drawCircle(cx, cy, ico(5, s), c); drawCircle(cx, cy, ico(8, s), c);
  }
  function belIcon(x, cy, s, c) {
    var cx = x + ico(7, s), o2 = Math.max(1, ico(2, s)), o4 = ico(4, s), o5 = ico(5, s), o6 = ico(6, s), o7 = ico(7, s);
    fillCircle(cx, cy - o2, o5, c); fillRect(cx - o5, cy - o2, 2 * o5 + 1, o5 + o2, c);
    fillRect(cx - o7, cy + o4 - (o2 >> 1), 2 * o7 + 1, o2, c); fillCircle(cx, cy + o6 + (o2 >> 1), o2, c);
    fillRect(cx, cy - o2 - o5 - o2, 1, o2, c);
  }
  function updateIcon(x, cy, s, c) {
    var cx = x + ico(7, s), o2 = Math.max(1, ico(2, s)), o5 = ico(5, s), o7 = ico(7, s), o8 = ico(8, s);
    fillTriangle(cx - o5, cy - o2, cx + o5, cy - o2, cx, cy - o8, c);
    fillRect(cx - Math.max(1, o2 >> 1), cy - o2, Math.max(1, o2), o7 + o2, c);
  }
  var SB_ICON_X0 = 10, SB_ICON_CY = SB_H / 2, SB_ICON_GAP = 12, SB_WIFI_W = 22, SB_HOTSPOT_W = 16,
      SB_HOTSPOT_X = SB_ICON_X0 + SB_WIFI_W + SB_ICON_GAP + 1, SB_UPDATE_W = 18, SB_BEL_W = 16, SB_TITEL_GAP = 18,
      SB_KLOK_X = W - 68, SB_KLOK_ZWART_X = SB_KLOK_X - 14;
  // Webapp: het zwarte klokvenster loopt door over twee iconen (synchroon/asynchroon + slotje)
  var WEB_ICON_W = 30, WEB_ZWART_X = SB_KLOK_ZWART_X - 3 * WEB_ICON_W;
  // Schermvullend: vier hoekjes naar buiten (aanzetten) of naar binnen (staat aan → verlaten)
  function volschermIcoon(cx, cy) {
    var w = S.web || {}, k = w.vol ? C_CYAN : C_TEXT_DIM, A = 8, L = 5;
    [[-1, -1], [1, -1], [-1, 1], [1, 1]].forEach(function (h) {
      var sx = h[0], sy = h[1], px0, py0, dx, dy;
      if (!w.vol) { px0 = cx + sx * A; py0 = cy + sy * A; dx = -sx; dy = -sy; }            // hoekjes naar buiten
      else { px0 = cx + sx * (A - L); py0 = cy + sy * (A - L); dx = sx; dy = sy; }        // hoekjes naar binnen
      for (var t = 0; t < 2; t++) {
        drawLine(px0, py0 - sy * t, px0 + dx * L, py0 - sy * t, k);   // horizontaal pootje
        drawLine(px0 - sx * t, py0, px0 - sx * t, py0 + dy * L, k);   // verticaal pootje
      }
    });
  }

  function miniPijl(cx, cy, r, graden, k) {
    var rad = graden * Math.PI / 180, trad = rad + Math.PI / 2, pX = cx + Math.trunc(r * Math.cos(rad)), pY = cy + Math.trunc(r * Math.sin(rad));
    var tx = Math.round(Math.cos(trad) * 4), ty = Math.round(Math.sin(trad) * 4), rx = Math.round(Math.cos(rad) * 3), ry = Math.round(Math.sin(rad) * 3);
    fillTriangle(pX + tx, pY + ty, pX - tx + rx, pY - ty + ry, pX - tx - rx, pY - ty - ry, k);
  }
  // Synchroon = twee ronddraaiende pijlen (cyaan; grijs zolang je niet ingelogd bent, dan staat sync stil).
  // Asynchroon = twee losse pijlen die elk hun eigen kant op gaan.
  function syncIcoon(cx, cy) {
    var w = S.web || {}, k;
    if (!w.sync) {
      k = C_TEXT_DIM;
      hl(cx - 8, cy - 4, 12, k); hl(cx - 8, cy - 3, 12, k); fillTriangle(cx + 4, cy - 8, cx + 4, cy + 1, cx + 9, cy - 4, k);
      hl(cx - 3, cy + 4, 12, k); hl(cx - 3, cy + 5, 12, k); fillTriangle(cx - 4, cy, cx - 4, cy + 9, cx - 9, cy + 4, k);
      return;
    }
    k = w.ing ? C_CYAN : C_DARK_GRAY;
    fillArc(cx, cy, 9, 7, 25, 155, k); fillArc(cx, cy, 9, 7, 205, 335, k);
    miniPijl(cx, cy, 8, 155, k); miniPijl(cx, cy, 8, 335, k);
  }
  // Slotje: groen en open = ingelogd, grijs en dicht = niet ingelogd, rood = geen verbinding met de boordcomputer
  // Geel = ingelogd, maar met een pincode die geen toegang geeft tot het scherm dat nu getoond wordt
  function slotIcoon(cx, cy) {
    var w = S.web || {}, k = !w.verb ? C_RED_BRIGHT : (w.ing && w.geel) ? C_AMBER : w.ing ? C_GREEN : C_TEXT_DIM, o = w.ing ? 4 : 0;
    var by = cy - 1;
    vl(cx - 5, by - 6 - o, 6 + o, k); vl(cx - 4, by - 6 - o, 6 + o, k);
    if (w.ing) { vl(cx + 4, by - 6 - o, 3, k); vl(cx + 5, by - 6 - o, 3, k); }
    else { vl(cx + 4, by - 6, 6, k); vl(cx + 5, by - 6, 6, k); }
    hl(cx - 3, by - 9 - o, 7, k); hl(cx - 3, by - 8 - o, 7, k); px(cx - 4, by - 7 - o, k); px(cx + 4, by - 7 - o, k);
    fillRoundRect(cx - 7, by, 15, 11, 2, k);
    fillRect(cx, by + 3, 1, 5, C_BLACK); fillRect(cx - 1, by + 3, 3, 2, C_BLACK);
  }
  var sbIconenEindX = SB_HOTSPOT_X + SB_HOTSPOT_W;
  var tintFn = null;   // optioneel: HAVEN tint achtergrond (foto) — zie nbFill
  function nbFill(x, y, w, h, kleur, sterkte) { if (tintFn) tintFn(x, y, w, h, kleur, sterkte == null ? 165 : sterkte); else fillRect(x, y, w, h, kleur); }
  function nbFillActief(x, y, w, h, kleurNormaal) { if (tintFn) tintFn(x, y, w, h, C_CYAN, 215); else fillRect(x, y, w, h, kleurNormaal); }
  function sbBasis() {
    var ui = S.ui || {};
    nbFill(0, 0, W, SB_H, C_STATUSBAR, 165);
    hl(0, SB_H - 1, W, C_SURFACE2);
    fillRect(WEB_ZWART_X, 0, W - WEB_ZWART_X, SB_H, C_BLACK);
    volschermIcoon(WEB_ZWART_X + (WEB_ICON_W >> 1), SB_ICON_CY);
    syncIcoon(WEB_ZWART_X + WEB_ICON_W + (WEB_ICON_W >> 1), SB_ICON_CY);
    slotIcoon(WEB_ZWART_X + 2 * WEB_ICON_W + (WEB_ICON_W >> 1), SB_ICON_CY);
    zone(WEB_ZWART_X, 0, WEB_ICON_W, SB_H, function () { return { volscherm: true }; });
    zone(WEB_ZWART_X + WEB_ICON_W, 0, WEB_ICON_W, SB_H, function () { return { sync: true }; });
    zone(WEB_ZWART_X + 2 * WEB_ICON_W, 0, WEB_ICON_W, SB_H, function () { return { slot: true }; });
    wifiIcon(SB_ICON_X0, SB_ICON_CY, 1);
    hotspotIcon(SB_HOTSPOT_X, SB_ICON_CY, 1);
    var x = SB_HOTSPOT_X + SB_HOTSPOT_W;
    if (ui.bel) { x += SB_ICON_GAP; belIcon(x, SB_ICON_CY, 1, C_AMBER); x += SB_BEL_W; }
    if (ui.upd) { x += SB_ICON_GAP; updateIcon(x, SB_ICON_CY, 1, C_AMBER); x += SB_UPDATE_W; }
    sbIconenEindX = x;
    tekst(ui.tijd || S.klok, SB_KLOK_X, (SB_H - 16) / 2, 2, C_TEXT);
  }
  function sbSchermTeken(titel, kleur) { sbBasis(); tekst(titel, sbIconenEindX + SB_TITEL_GAP, (SB_H - 16) / 2, 2, kleur); }
  function statusBarMain() {
    sbBasis();
    var naam = S.bootnaam || '';
    if (naam.length > 0) {
      naam = naam.substr(0, 14); tekst(naam, (W / 2 - naam.length * 6) | 0, (SB_H - 16) / 2, 2, C_TEXT);
    } else {
      var v = S.versie || ''; tekst(v, (W / 2 - v.length * 3) | 0, (SB_H - 8) / 2, 1, C_TEXT_DIM);
    }
  }

  // ── Navigatiebalk-iconen ───────────────────────────────────────────────────
  function icWolk(cx, cy, c) {
    fillCircle(cx - 7, cy + 2, 6, c); fillCircle(cx + 5, cy + 2, 5, c); fillCircle(cx - 1, cy - 3, 8, c); fillRect(cx - 13, cy + 2, 23, 7, c);
  }
  function icZon(cx, cy, c, r) {
    if (r == null) r = 6;
    fillCircle(cx, cy, r, c);
    for (var i = 0; i < 8; i++) {
      var a = i * Math.PI / 4;
      drawLine(cx + Math.trunc((r + 2) * Math.cos(a)), cy + Math.trunc((r + 2) * Math.sin(a)),
               cx + Math.trunc((r + 6) * Math.cos(a)), cy + Math.trunc((r + 6) * Math.sin(a)), c);
    }
  }
  function navIO(cx, cy, c) {
    var bw = 12, bh = 14, bx = cx - 6, by = cy - 7; drawRect(bx, by, bw, bh, c);
    for (var i = 0; i < 3; i++) { var py = by + 2 + i * 4; hl(bx - 4, py, 4, c); hl(bx + bw, py, 4, c); }
  }
  function navPaneel(cx, cy, c) {
    var sz = 7, gap = 3, ox = cx - sz - (gap >> 1), oy = cy - sz - (gap >> 1);
    fillRoundRect(ox, oy, sz, sz, 2, c); fillRoundRect(ox + sz + gap, oy, sz, sz, 2, c);
    fillRoundRect(ox, oy + sz + gap, sz, sz, 2, c); fillRoundRect(ox + sz + gap, oy + sz + gap, sz, sz, 2, c);
  }
  function navStore(cx, cy, c) { for (var r = 0; r < 2; r++) for (var k = 0; k < 3; k++) fillCircle(cx - 9 + k * 9, cy - 4 + r * 9, 3, c); }
  function navConfig(cx, cy, c, bg) {
    fillCircle(cx, cy, 6, c); fillCircle(cx, cy, 3, bg);
    for (var i = 0; i < 6; i++) { var a = i * Math.PI / 3; fillCircle(cx + Math.trunc(9.5 * Math.cos(a)), cy + Math.trunc(9.5 * Math.sin(a)), 3, c); }
  }
  function navInfo(cx, cy, c) { drawCircle(cx, cy, 12, c); drawCircle(cx, cy, 11, c); fillRect(cx - 1, cy - 6, 3, 3, c); fillRect(cx - 1, cy - 1, 3, 8, c); }
  function navSolar(cx, cy, c) {
    var pw = 20, ph = 12, px0 = cx - 10, py0 = cy - 6 + 3;
    drawRect(px0, py0, pw, ph, c); vl(px0 + Math.trunc(pw / 3), py0, ph, c); vl(px0 + Math.trunc(2 * pw / 3), py0, ph, c); hl(px0, py0 + 6, pw, c);
    icZon(cx + 7, cy - 6, c, 4);
  }
  function navNetwerk(cx, cy, c) {
    var x1 = cx - 9, y1 = cy - 7, x2 = cx + 9, y2 = cy - 7, x3 = cx, y3 = cy + 8;
    drawLine(x1, y1, x2, y2, c); drawLine(x1, y1, x3, y3, c); drawLine(x2, y2, x3, y3, c);
    fillCircle(x1, y1, 3, c); fillCircle(x2, y2, 3, c); fillCircle(x3, y3, 3, c);
  }
  function icRegen(cx, cy) {
    icWolk(cx, cy - 4, RGB(90, 110, 140)); var r = RGB(80, 140, 220);
    drawLine(cx - 7, cy + 5, cx - 9, cy + 12, r); drawLine(cx, cy + 5, cx - 2, cy + 12, r); drawLine(cx + 7, cy + 5, cx + 5, cy + 12, r);
  }
  function icSneeuw(cx, cy) {
    icWolk(cx, cy - 5, RGB(115, 135, 165)); var s = RGB(190, 215, 255);
    for (var i = -1; i <= 1; i++) {
      var pX = cx + i * 7, pY = cy + 9;
      hl(pX - 4, pY, 8, s); vl(pX, pY - 4, 8, s); drawLine(pX - 3, pY - 3, pX + 3, pY + 3, s); drawLine(pX + 3, pY - 3, pX - 3, pY + 3, s);
    }
  }
  function icOnweer(cx, cy) {
    icWolk(cx, cy - 5, RGB(65, 80, 100)); var b = C_AMBER;
    drawLine(cx + 2, cy + 4, cx - 3, cy + 11, b); drawLine(cx + 3, cy + 4, cx - 2, cy + 11, b);
    drawLine(cx - 3, cy + 11, cx + 4, cy + 11, b); drawLine(cx - 2, cy + 11, cx + 5, cy + 11, b);
    drawLine(cx + 4, cy + 11, cx - 1, cy + 17, b); drawLine(cx + 5, cy + 11, cx, cy + 17, b);
  }
  function navMeteo(cx, cy) {
    var ui = S.ui || {};
    if (!ui.mg) { var c = RGB(80, 100, 120); fillRect(cx - 9, cy - 3, 18, 3, c); fillRect(cx - 6, cy + 3, 12, 3, c); return; }
    var code = ui.mc, dag = !!ui.md;
    if (code === 0 || code === 1) icZon(cx, cy, dag ? C_AMBER : RGB(170, 180, 220));
    else if (code === 2) { icZon(cx + 6, cy - 5, C_AMBER, 4); icWolk(cx - 3, cy + 3, RGB(110, 135, 165)); }
    else if (code === 3 || code === 45 || code === 48) icWolk(cx, cy, RGB(115, 135, 165));
    else if ((code >= 71 && code <= 77) || code === 85 || code === 86) icSneeuw(cx, cy);
    else if (code >= 95) icOnweer(cx, cy);
    else icRegen(cx, cy);
  }

  // ── Navigatiebalk ──────────────────────────────────────────────────────────
  var NB_SQ = NAV_H, NB_MX = 4 * NB_SQ, NB_MW = W - 8 * NB_SQ, NB_AW = 22, NB_KW = 100,
      NB_R0X = W - 4 * NB_SQ, NB_R1X = W - 3 * NB_SQ, NB_R2X = W - 2 * NB_SQ, NB_R3X = W - NB_SQ;
  var navScroll = 0;
  var navMidden = [];   // [{l,s}]
  var zones = [];       // klikzones, gevuld tijdens het tekenen
  function zone(x, y, w, h, fn) { zones.push({ x: x, y: y, w: w, h: h, fn: fn }); }
  function homeScherm() { return S.h ? SC.HAVEN : SC.MAIN; }
  function navBar(actief) {
    var y = NAV_Y, cy = y + NAV_H / 2;
    nbFill(0, y, W, NAV_H, C_NAVBAR, 165); hl(0, y, W, C_SURFACE2);
    navMidden = ((S.ui && S.ui.nav) ? S.ui.nav : []).slice();
    if (!navMidden.some(function (it) { return it.s === SC.BERICHT; })) navMidden.unshift({ l: 'BERICHT', s: SC.BERICHT });
    function sysKnop(x, act, scherm, uit) {
      if (act) nbFillActief(x + 1, y + 1, NB_SQ - 2, NAV_H - 2, C_SURFACE2); else nbFill(x + 1, y + 1, NB_SQ - 2, NAV_H - 2, uit ? C_NAVBAR : C_SURFACE);
      if (act) { hl(x + 4, y, NB_SQ - 8, C_CYAN); hl(x + 4, y + 1, NB_SQ - 8, C_CYAN); }
      else if (!uit) { hl(x + 1, y + 1, NB_SQ - 2, C_SURFACE3); vl(x + 1, y + 1, NAV_H - 2, C_SURFACE3); hl(x + 1, y + NAV_H - 2, NB_SQ - 2, C_NAVBAR); vl(x + NB_SQ - 2, y + 1, NAV_H - 2, C_NAVBAR); }
      if (!uit) zone(x, y, NB_SQ, NAV_H, function () { return { gaNaar: scherm === 'home' ? homeScherm() : scherm }; });
      return act;
    }
    // Alleen op de boordcomputer (niet in de webapp): gedimd, doorgestreept en niet aanklikbaar
    function uitStreep(x, w) { drawLine(x + 8, y + NAV_H - 8, x + w - 8, y + 8, C_DARK_GRAY); drawLine(x + 9, y + NAV_H - 8, x + w - 7, y + 8, C_DARK_GRAY); }
    var a;
    a = sysKnop(0, actief === SC.MAIN || actief === SC.HAVEN, 'home'); navPaneel(NB_SQ / 2, cy, a ? C_CYAN : C_TEXT_DIM); vl(NB_SQ - 1, y + 4, NAV_H - 8, C_SURFACE2);
    a = sysKnop(NB_SQ, actief === SC.IO, SC.IO); navIO(NB_SQ + NB_SQ / 2, cy, a ? C_CYAN : C_TEXT_DIM); vl(2 * NB_SQ - 1, y + 4, NAV_H - 8, C_SURFACE2);
    sysKnop(2 * NB_SQ, actief === SC.METEO, SC.METEO); navMeteo(2 * NB_SQ + NB_SQ / 2, cy); vl(3 * NB_SQ - 1, y + 4, NAV_H - 8, C_SURFACE2);
    a = sysKnop(3 * NB_SQ, actief === SC.VICTRON, SC.VICTRON); navSolar(3 * NB_SQ + NB_SQ / 2, cy, a ? C_CYAN : C_TEXT_DIM); vl(NB_MX - 1, y + 4, NAV_H - 8, C_SURFACE2);

    var n = navMidden.length, toonPijlen = n > Math.trunc((NB_MW - 2 * NB_AW) / NB_KW), appX0, zichtbaar;
    var NB_MAX_V = Math.trunc((NB_MW - 2 * NB_AW) / NB_KW);
    if (navScroll > Math.max(0, n - NB_MAX_V)) navScroll = Math.max(0, n - NB_MAX_V);
    if (toonPijlen) {
      appX0 = NB_MX + NB_AW; zichtbaar = NB_MAX_V;
      nbFill(NB_MX, y, NB_AW, NAV_H, C_SURFACE);
      tekst('<', NB_MX + 3, y + (NAV_H - 16) / 2, 2, navScroll > 0 ? C_TEXT : C_TEXT_DARK);
      var rpx = NB_MX + NB_AW + zichtbaar * NB_KW;
      nbFill(rpx, y, NB_AW, NAV_H, C_SURFACE);
      tekst('>', rpx + 3, y + (NAV_H - 16) / 2, 2, (navScroll + zichtbaar < n) ? C_TEXT : C_TEXT_DARK);
      zone(NB_MX, y, NB_AW, NAV_H, function () { return { navScroll: -1 }; });
      zone(rpx, y, NB_AW, NAV_H, function () { return { navScroll: 1 }; });
    } else { appX0 = NB_MX + ((NB_MW - n * NB_KW) / 2 | 0); zichtbaar = n; }
    nbFill(NB_MX, y + 1, NB_MW, NAV_H - 2, C_SURFACE); hl(NB_MX, y, NB_MW, C_SURFACE2);
    for (var vi = 0; vi < zichtbaar; vi++) {
      var ai = navScroll + vi; if (ai >= n) break;
      var it = navMidden[ai], bx = appX0 + vi * NB_KW, act = (it.s === actief && it.s !== SC.LUA), itUit = it.s === SC.LUA;
      if (act) { nbFillActief(bx + 1, y + 1, NB_KW - 2, NAV_H - 2, C_SURFACE2); hl(bx + 4, y, NB_KW - 8, C_CYAN); hl(bx + 4, y + 1, NB_KW - 8, C_CYAN); }
      else { hl(bx + 1, y + 1, NB_KW - 2, C_SURFACE3); vl(bx + 1, y + 1, NAV_H - 2, C_SURFACE3); hl(bx + 1, y + NAV_H - 2, NB_KW - 2, C_NAVBAR); vl(bx + NB_KW - 2, y + 1, NAV_H - 2, C_NAVBAR); }
      var lbl = (it.l || '').substr(0, 16), tw = lbl.length * 6;
      if (tw > NB_KW - 8) { var mc = Math.trunc((NB_KW - 8) / 6); lbl = lbl.substr(0, mc - 1) + '.'; tw = lbl.length * 6; }
      tekst(lbl, bx + ((NB_KW - tw) / 2 | 0), y + (NAV_H - 8) / 2, 1, act ? C_CYAN : itUit ? C_TEXT_DARK : C_TEXT_DIM);
      if (itUit) uitStreep(bx, NB_KW);
      if (vi < zichtbaar - 1) vl(bx + NB_KW - 1, y + 4, NAV_H - 8, C_SURFACE2);
      if (!itUit) zone(bx, y, NB_KW, NAV_H, (function (s) { return function () { return { gaNaar: s }; }; })(it.s));
    }
    vl(NB_R0X - 1, y + 4, NAV_H - 8, C_SURFACE2); vl(NB_R1X - 1, y + 4, NAV_H - 8, C_SURFACE2);
    vl(NB_R2X - 1, y + 4, NAV_H - 8, C_SURFACE2); vl(NB_R3X - 1, y + 4, NAV_H - 8, C_SURFACE2);
    a = sysKnop(NB_R0X, actief === SC.NETWERK, SC.NETWERK, true); navNetwerk(NB_R0X + NB_SQ / 2, cy, a ? C_CYAN : C_TEXT_DARK); uitStreep(NB_R0X, NB_SQ);
    a = sysKnop(NB_R1X, actief === SC.APPS, SC.APPS, true); navStore(NB_R1X + NB_SQ / 2, cy, a ? C_CYAN : C_TEXT_DARK); uitStreep(NB_R1X, NB_SQ);
    a = sysKnop(NB_R2X, actief === SC.CONFIG, SC.CONFIG); navConfig(NB_R2X + NB_SQ / 2, cy, a ? C_CYAN : C_TEXT_DIM, a ? C_SURFACE2 : C_SURFACE);
    a = sysKnop(NB_R3X, actief === SC.INFO, SC.INFO); navInfo(NB_R3X + NB_SQ / 2, cy, a ? C_CYAN : C_TEXT_DIM);
  }

  // ── Iconen op knoppen (screen_main.ino teken_icoon) ────────────────────────
  var I = { HAVEN: 0, ZEILEN: 1, MOTOR: 2, ANKER: 3, LICHT_UIT: 4, LICHT_AAN: 5, LICHT_AUTO: 6, USB: 7, V230: 8, TV: 9, WATER: 10, DEKLICHT: 11, LAMP: 12 };
  function icoon(type, cx, cy, k) {
    switch (type) {
      case I.HAVEN: case I.ANKER:
        drawCircle(cx, cy - 7, 3, k); vl(cx, cy - 4, 13, k); hl(cx - 7, cy + 1, 14, k); drawLine(cx - 7, cy + 1, cx - 4, cy + 8, k); drawLine(cx + 7, cy + 1, cx + 4, cy + 8, k); break;
      case I.ZEILEN:
        vl(cx - 1, cy - 9, 18, k); drawLine(cx - 1, cy - 9, cx + 8, cy + 8, k); drawLine(cx + 8, cy + 8, cx - 1, cy + 8, k); drawLine(cx - 1, cy - 4, cx - 7, cy + 8, k); break;
      case I.MOTOR:
        fillCircle(cx, cy, 3, k); drawLine(cx, cy, cx - 5, cy - 7, k); drawLine(cx, cy, cx + 7, cy - 2, k); drawLine(cx, cy, cx - 2, cy + 7, k); break;
      case I.LICHT_UIT:
        drawCircle(cx, cy, 7, k); drawLine(cx - 5, cy - 5, cx + 5, cy + 5, k); drawLine(cx - 5, cy + 5, cx + 5, cy - 5, k); break;
      case I.LICHT_AAN:
        fillCircle(cx, cy, 4, k); hl(cx - 8, cy, 4, k); hl(cx + 4, cy, 4, k); vl(cx, cy - 8, 4, k); vl(cx, cy + 4, 4, k);
        drawLine(cx - 5, cy - 5, cx - 3, cy - 3, k); drawLine(cx + 3, cy - 3, cx + 5, cy - 5, k);
        drawLine(cx - 5, cy + 5, cx - 3, cy + 3, k); drawLine(cx + 3, cy + 3, cx + 5, cy + 5, k); break;
      case I.LICHT_AUTO:
        drawCircle(cx, cy, 7, k); drawLine(cx, cy, cx, cy - 5, k); drawLine(cx, cy, cx + 4, cy + 2, k); break;
      case I.USB:
        drawRect(cx - 4, cy - 7, 8, 7, k); vl(cx, cy, 6, k); drawLine(cx, cy + 6, cx - 5, cy + 3, k); drawLine(cx, cy + 6, cx + 5, cy + 3, k);
        fillCircle(cx - 5, cy + 3, 2, k); fillCircle(cx + 5, cy + 3, 2, k); break;
      case I.V230:
        drawLine(cx + 3, cy - 8, cx - 2, cy, k); drawLine(cx - 2, cy, cx + 3, cy, k); drawLine(cx + 3, cy, cx - 4, cy + 8, k); break;
      case I.TV:
        drawRect(cx - 8, cy - 5, 16, 11, k); vl(cx, cy + 6, 3, k); hl(cx - 4, cy + 9, 8, k); break;
      case I.WATER:
        drawLine(cx, cy - 9, cx - 5, cy, k); drawLine(cx, cy - 9, cx + 5, cy, k); drawCircle(cx, cy + 3, 5, k); break;
      case I.DEKLICHT:
        fillTriangle(cx - 7, cy - 4, cx + 7, cy - 4, cx, cy - 10, k); fillCircle(cx, cy - 2, 3, k);
        drawLine(cx, cy + 1, cx, cy + 9, k); drawLine(cx - 2, cy + 1, cx - 7, cy + 8, k); drawLine(cx + 2, cy + 1, cx + 7, cy + 8, k); break;
    }
  }
  function glow(cx, cy, r, kleur, lagen) {
    for (var i = lagen; i > 0; i--) {
      var r_ = (kleur >> 11) & 0x1F, g_ = (kleur >> 5) & 0x3F, b_ = kleur & 0x1F;
      r_ = Math.trunc(r_ * i / (lagen + 1)); g_ = Math.trunc(g_ * i / (lagen + 1)); b_ = Math.trunc(b_ * i / (lagen + 1));
      drawCircle(cx, cy, r + lagen - i + 1, (r_ << 11) | (g_ << 5) | b_);
    }
  }
  function iconLamp(cx, cy, aan, rood, uitKleur) {
    var kleur = aan ? (rood ? C_LIGHT_ON_RED : C_WHITE) : uitKleur, by = cy - 2;
    if (aan) { fillCircle(cx, by, 6, kleur); glow(cx, by, 6, kleur, 2); } else drawCircle(cx, by, 6, kleur);
    vl(cx, by + 5, 3, kleur); hl(cx - 3, by + 8, 6, kleur); hl(cx - 3, by + 10, 6, kleur);
    if (aan) {
      drawLine(cx, by - 8, cx, by - 12, kleur); drawLine(cx - 6, by - 6, cx - 9, by - 9, kleur); drawLine(cx + 6, by - 6, cx + 9, by - 9, kleur);
      drawLine(cx - 8, by, cx - 12, by, kleur); drawLine(cx + 8, by, cx + 12, by, kleur);
    }
  }

  // ── Hoofdscherm (screen_main.ino) ──────────────────────────────────────────
  var BDX = 5, BDY = CONTENT_Y + 5, BDW = 380, BDH = CONTENT_H - 10;
  var CTRL_X = 400, CTRL_W = 400;
  var BOOT_N = 7, BOOT_D = 4, BOOT_BX_OFF = BDX + ((BDW - Math.trunc(120 * BOOT_N / BOOT_D)) >> 1), BOOT_BY_OFF = BDY + 20, BLICHT_R = 12;
  function bootBX(x) { return BOOT_BX_OFF + Math.trunc(x * BOOT_N / BOOT_D); }
  function bootBY(y) { return BOOT_BY_OFF + Math.trunc(y * BOOT_N / BOOT_D); }
  var MKNOP_W = Math.trunc((CTRL_W - 10 - 8) / 2), MKNOP_H = 68, MKNOP_GAP = 8, MKNOP_X1 = CTRL_X + 10, MKNOP_X2 = MKNOP_X1 + MKNOP_W + 8,
      MKNOP_Y1 = CONTENT_Y + 8, MKNOP_Y2 = MKNOP_Y1 + MKNOP_H + MKNOP_GAP;
  var AUTO_CX = MKNOP_X2 - 4, AUTO_CY = MKNOP_Y2 - (MKNOP_GAP >> 1), AUTO_R = 55, AUTO_HALO = AUTO_R + 4;
  var LKNOP_W = Math.trunc((CTRL_W - 11 - 12) / 3), LKNOP_H = 52, LKNOP_X1 = CTRL_X + 11, LKNOP_X2 = LKNOP_X1 + LKNOP_W + 6, LKNOP_X3 = LKNOP_X2 + LKNOP_W + 6,
      LKNOP_Y = MKNOP_Y2 + MKNOP_H + 14;
  var DKNOP_W = LKNOP_W, DKNOP_H = 52, DKNOP_X1 = CTRL_X + 11, DKNOP_Y1 = LKNOP_Y + LKNOP_H + 16, DKNOP_Y2 = DKNOP_Y1 + DKNOP_H + 6, DKNOP_Y3 = DKNOP_Y2 + DKNOP_H + 6;
  var INT_Y = DKNOP_Y2 + DKNOP_H + 8, INT_Y3 = DKNOP_Y3 + DKNOP_H + 8, INT_KNOP_W = 156;

  function catNu() { var u = S.ui || {}; return BK_CATS[(u.cat < BK_CATS.length) ? u.cat : 0] || BK_CATS[0]; }
  function modelNu() { var c = catNu(), u = S.ui || {}; return c.modellen[(u.mod < c.modellen.length) ? u.mod : 0]; }
  function segKleur(id) { return id === 1 ? RGB(120, 140, 170) : id === 2 ? RGB(160, 170, 190) : id === 3 ? C_CYAN : C_TEXT_DIM; }
  function bootPad(seg, kleur) {
    var d = seg, toggle = d.length >= 2 && d[0][0] === d[1][0] && d[0][1] === d[1][1];
    function X(x) { return bootBX(x); } function Y(y) { return bootBY(y); }
    if (toggle) {
      var pen = false, lx = -30000, ly = -30000;
      for (var i = 0; i < d.length; i++) {
        var x = d[i][0], y = d[i][1];
        if (x === lx && y === ly) pen = !pen; else if (pen) drawLine(X(lx), Y(ly), X(x), Y(y), kleur);
        lx = x; ly = y;
      }
    } else for (var j = 0; j + 1 < d.length; j++) drawLine(X(d[j][0]), Y(d[j][1]), X(d[j + 1][0]), Y(d[j + 1][1]), kleur);
  }
  function bootTeken() {
    fillRect(BDX, BDY, BDW, BDH, C_BG);
    var m = modelNu();
    m.segs.forEach(function (s) { bootPad(s[0], segKleur(s[1])); });
    var rk = segKleur(m.raamKleur);
    m.ramen.forEach(function (r) { var rr = Math.trunc(r[2] * BOOT_N / BOOT_D); if (rr < 1) rr = 1; drawCircle(bootBX(r[0]), bootBY(r[1]), rr, rk); });
  }
  function bootSector(cx, cy, r, a0, a1, c) {
    var n = Math.max(3, Math.trunc((a1 - a0) / 12)), da = (a1 - a0) / n;
    for (var i = 0; i < n; i++) {
      var f0 = (a0 + i * da) * (Math.PI / 180), f1 = (a0 + (i + 1) * da) * (Math.PI / 180);
      fillTriangle(cx, cy, cx + Math.trunc(r * Math.cos(f0)), cy + Math.trunc(r * Math.sin(f0)), cx + Math.trunc(r * Math.cos(f1)), cy + Math.trunc(r * Math.sin(f1)), c);
    }
  }
  function lichtIndicator(cx, cy, r, staat, sec, a0, a1) {
    var dr = Math.max(2, Math.trunc(r / 3));
    if (staat === 3) bootSector(cx, cy, r, a0, a1, sec);
    else if (staat === 1) fillCircle(cx, cy, dr, C_LIGHT_COOLING);
    else if (staat === 2) fillCircle(cx, cy, dr, C_LIGHT_PENDING);
    else fillCircle(cx, cy, dr, C_DARK_GRAY);
  }
  function bootLichten() {
    var u = S.ui || {}, lt = u.lt || [0, 0, 0, 0, 0], sa = lt[0], ss = lt[1], s3 = lt[2], sh = lt[3], sn = lt[4];
    var m = modelNu(), L = m.licht, prof = catNu().lp, r = BLICHT_R;
    var mast = (s3 === 3 || sa === 3) ? 3 : (s3 > sa ? s3 : sa);
    var cx = bootBX(L.anker[0]), cy = bootBY(L.anker[1]), dr = Math.max(2, Math.trunc(r / 3));
    if ((prof & 4) && s3 === 3) {
      bootSector(cx, cy, r, 120, 240, C_LIGHT_ON); bootSector(cx, cy, r, 240, 360, C_LIGHT_ON_RED); bootSector(cx, cy, r, 0, 120, C_LIGHT_ON_GRN);
    } else if (sa === 3) fillCircle(cx, cy, r, C_LIGHT_ON);
    else if (mast === 1) fillCircle(cx, cy, dr, C_LIGHT_COOLING);
    else if (mast === 2) fillCircle(cx, cy, dr, C_LIGHT_PENDING);
    else fillCircle(cx, cy, dr, C_DARK_GRAY);
    if (prof & 2) lichtIndicator(bootBX(L.stoom[0]), bootBY(L.stoom[1]), r, ss, C_LIGHT_ON, 240, 480);
    if (prof & 16) lichtIndicator(bootBX(L.hek[0]), bootBY(L.hek[1]), r, sh, C_LIGHT_ON, 120, 240);
    if (prof & 8) {
      var rcx = bootBX(L.naviR[0]), rcy = bootBY(L.naviR[1]), gcx = bootBX(L.naviG[0]), gcy = bootBY(L.naviG[1]);
      if (sn === 3) { bootSector(rcx, rcy, r, 240, 360, C_LIGHT_ON_RED); bootSector(gcx, gcy, r, 0, 120, C_LIGHT_ON_GRN); }
      else { var k = sn === 1 ? C_LIGHT_COOLING : sn === 2 ? C_LIGHT_PENDING : C_DARK_GRAY; fillCircle(rcx, rcy, dr, k); fillCircle(gcx, gcy, dr, k); }
    }
  }
  function maanSymbool(cx, cy, r, fase) {
    var cl = RGB(230, 210, 110), cd = RGB(20, 28, 50), cr = RGB(70, 70, 100);
    fillCircle(cx, cy, r, cd); drawCircle(cx, cy, r, cr);
    if (fase < 0.02 || fase > 0.98) return;
    var fi = (1 - Math.cos(2 * Math.PI * fase)) / 2, wassend = fase < 0.5;
    for (var dy = -r; dy <= r; dy++) {
      var xr = Math.trunc(Math.sqrt(r * r - dy * dy) + 0.5); if (xr === 0) continue;
      var term = xr * (1 - 2 * fi);
      if (wassend) { var sx = Math.trunc(term), len2 = cx + xr - (cx + sx) + 1; if (len2 > 0) hl(cx + sx, cy + dy, len2, cl); }
      else { var ex = Math.trunc(-term), len3 = (cx + ex) - (cx - xr) + 1; if (len3 > 0) hl(cx - xr, cy + dy, len3, cl); }
    }
  }
  function meteoStrip() {
    var u = S.ui || {}, w = u.w || {}, sx = BDX, sh = 56, sy = BDY + BDH - sh, sw = BDW;
    fillRect(sx, sy, sw, sh, C_SURFACE); drawRect(sx, sy, sw, sh, C_SURFACE2);
    var lx = sx + 4, ly = sy + 4;
    if (u.mg) {
      tekst(w.o || '', lx, ly, 1, C_TEXT_DIM);
      tekst(w.t || '', lx, ly + 12, 2, C_TEXT); tekst(DEG + 'C', lx + (w.t || '').length * 12, ly + 12, 1, C_TEXT_DIM);
      tekst('max ', lx, ly + 32, 1, C_TEXT_DIM); tekst((w.tm || '') + DEG, lx + 24, ly + 32, 1, C_TEXT);
      var mx = sx + 100;
      tekst('Wind', mx, ly, 1, C_TEXT_DIM); tekst(w.wr || '', mx, ly + 12, 1, C_TEXT);
      tekst(w.ws || '', mx, ly + 26, 1, C_TEXT_DIM); tekst(w.wm || '', mx, ly + 38, 1, C_TEXT_DIM);
    } else {
      tekst('Waterstand nu', lx, ly, 1, C_TEXT_DIM);
      tekst(w.nu || '', lx, ly + 10, 2, C_CYAN);
      var ax = lx + (w.nu || '').length * 12 + 6, ay = ly + 10 + 8;
      if (w.ri > 0) fillTriangle(ax + 5, ay - 8, ax, ay + 2, ax + 10, ay + 2, C_GREEN);
      else if (w.ri < 0) fillTriangle(ax + 5, ay + 2, ax, ay - 8, ax + 10, ay - 8, RGB(80, 150, 255));
      tekst(w.ri > 0 ? 'opkomend' : (w.ri < 0 ? 'afgaand' : ''), lx, ly + 30, 1, C_TEXT_DIM);
      if (!u.wifi || u.wifi < 2) tekst('geen WiFi', lx, ly + 40, 1, RGB(60, 70, 90));
    }
    var rx = sx + 200;
    tekst('Getij ', rx, ly, 1, C_TEXT_DIM); tekst(w.st || '', rx + 36, ly, 1, C_CYAN);
    maanSymbool(rx + 6, ly + 18, 5, w.mf || 0);
    tekst(w.mn || '', rx + 14, ly + 14, 1, RGB(200, 210, 150));
    var gt = w.gt || [];
    gt.forEach(function (t, i) { tekst(t, rx, ly + 28 + i * 13, 1, t.charAt(0) === 'H' ? C_BLUE : RGB(80, 150, 255)); });
    if (!gt.length) tekst('Geen getijdata', rx, ly + 28, 1, C_TEXT_DIM);
  }
  var VM_ALLE = [
    { naam: 'HAVEN', ic: I.HAVEN, kl: C_HAVEN, modus: 0, bit: 1 }, { naam: 'ZEILEN', ic: I.ZEILEN, kl: C_ZEILEN, modus: 1, bit: 2 },
    { naam: 'MOTOR', ic: I.MOTOR, kl: C_MOTOR, modus: 2, bit: 4 }, { naam: 'ANKER', ic: I.ANKER, kl: C_ANKER, modus: 3, bit: 8 }];
  function modusKnop(x, y, w, h, naam, ic, accent, actief) {
    fillRoundRect(x, y, w, h, KNOP_R, actief ? C_SURFACE2 : C_SURFACE);
    if (actief) { drawRoundRect(x, y, w, h, KNOP_R, accent); drawRoundRect(x + 1, y + 1, w - 2, h - 2, KNOP_R, accent); fillRoundRect(x, y, 5, h, KNOP_R, accent); }
    else drawRoundRect(x, y, w, h, KNOP_R, C_SURFACE2);
    var fg = actief ? accent : C_TEXT_DIM, cx = x + (w >> 1);
    icoon(ic, cx, y + Math.trunc(h * 3 / 8), fg);
    tekst(naam, cx - ((naam.length * 12) >> 1), y + Math.trunc(h * 6 / 8) - 8, 2, fg);
  }
  function pijlpunt(cx, cy, r, graden, kleur) {
    var rad = graden * Math.PI / 180, trad = rad + Math.PI / 2, pX = cx + Math.trunc(r * Math.cos(rad)), pY = cy + Math.trunc(r * Math.sin(rad));
    var tx = Math.trunc(Math.cos(trad) * 9), ty = Math.trunc(Math.sin(trad) * 9), rx = Math.trunc(Math.cos(rad) * 6), ry = Math.trunc(Math.sin(rad) * 6);
    fillTriangle(pX + tx, pY + ty, pX - tx + rx, pY - ty + ry, pX - tx - rx, pY - ty - ry, kleur);
  }
  function autoKnop() {
    var u = S.ui || {}, aan = !!u.auto;
    fillCircle(AUTO_CX, AUTO_CY, AUTO_HALO, C_BG);
    var bg = aan ? C_CYAN : C_SURFACE2, fg = aan ? C_TEXT_DARK : C_TEXT_DIM;
    fillCircle(AUTO_CX, AUTO_CY, AUTO_R, bg);
    drawCircle(AUTO_CX, AUTO_CY, AUTO_R, aan ? C_CYAN : C_SURFACE3); drawCircle(AUTO_CX, AUTO_CY, AUTO_R - 1, aan ? C_CYAN : C_SURFACE3);
    var lx = AUTO_CX - 4 * 6, ly = AUTO_CY - 8;
    tekst('AUTO', lx, ly, 2, fg);
    if (aan) {
      var rr = AUTO_R - 8;
      fillArc(AUTO_CX, AUTO_CY, rr, rr - 3, 25, 155, fg); fillArc(AUTO_CX, AUTO_CY, rr, rr - 3, 205, 335, fg);
      pijlpunt(AUTO_CX, AUTO_CY, rr - 1, 155, fg); pijlpunt(AUTO_CX, AUTO_CY, rr - 1, 335, fg);
    } else {
      var x0 = lx - 6, x1 = lx + 4 * 12 + 6, y0 = ly - 4, y1 = ly + 16 + 4;
      for (var d = -1; d <= 1; d++) { drawLine(x0, y0 + d, x1, y1 + d, C_RED_BRIGHT); drawLine(x0, y1 + d, x1, y0 + d, C_RED_BRIGHT); }
    }
    zones.unshift({ cirkel: true, x: AUTO_CX, y: AUTO_CY, r: AUTO_HALO, fn: function () { return { stuur: { t: 'set_auto', a: aan ? 0 : 1 } }; } });
  }
  function modusKnoppen() {
    tekst('VAARMODUS', MKNOP_X1, CONTENT_Y + 2, 1, C_TEXT_DIM);
    var cxs = [MKNOP_X1, MKNOP_X2, MKNOP_X1, MKNOP_X2], cys = [MKNOP_Y1, MKNOP_Y1, MKNOP_Y2, MKNOP_Y2], mask = catNu().vm, n = 0;
    VM_ALLE.forEach(function (m) {
      if (!(mask & m.bit)) return;
      modusKnop(cxs[n], cys[n], MKNOP_W, MKNOP_H, m.naam, m.ic, m.kl, S.modus === m.modus);
      zone(cxs[n], cys[n], MKNOP_W, MKNOP_H, function () { return { stuur: { t: 'set_modus', m: m.modus } }; });
      n++;
    });
    autoKnop();
  }
  function schakelaarsKnop(x, y, w, h, label, ic, accent, actief) {
    fillRoundRect(x, y, w, h, KNOP_R, actief ? C_SURFACE2 : C_SURFACE);
    if (actief) { drawRoundRect(x, y, w, h, KNOP_R, accent); fillRoundRect(x, y, 5, h, 3, accent); } else drawRoundRect(x, y, w, h, KNOP_R, C_SURFACE2);
    var fg = actief ? accent : C_TEXT_DIM, cy = y + (h >> 1);
    icoon(ic, x + 18, cy, fg); tekst(label, x + 36, cy - 8, 2, fg);
  }
  function lichtKnoppen() {
    tekst('VERLICHTING', LKNOP_X1, LKNOP_Y - 12, 1, C_TEXT_DIM);
    [['UIT', I.LICHT_UIT, C_GRAY, 0, LKNOP_X1], ['AAN', I.LICHT_AAN, C_GREEN, 1, LKNOP_X2], ['AUTO', I.LICHT_AUTO, C_CYAN, 2, LKNOP_X3]].forEach(function (k) {
      schakelaarsKnop(k[4], LKNOP_Y, LKNOP_W, LKNOP_H, k[0], k[1], k[2], S.licht === k[3]);
      zone(k[4], LKNOP_Y, LKNOP_W, LKNOP_H, function () { return { stuur: { t: 'set_licht', l: k[3] } }; });
    });
  }
  function paneelRect(idx, totaal) {
    if (totaal < 1) totaal = 1; if (totaal > 9) totaal = 9;
    var rijen = Math.trunc((totaal + 2) / 3), basis = Math.trunc(totaal / rijen), extra = totaal % rijen, rij = 0, k = 0, pos = 0, cum = 0;
    for (var r = 0; r < rijen; r++) { var rk = basis + (r < extra ? 1 : 0); if (idx < cum + rk) { rij = r; k = rk; pos = idx - cum; break; } cum += rk; }
    var rowW = k * DKNOP_W + (k - 1) * 6, startX = CTRL_X + ((CTRL_W - rowW) >> 1);
    return { x: startX + pos * (DKNOP_W + 6), y: [DKNOP_Y1, DKNOP_Y2, DKNOP_Y3][rij], w: DKNOP_W, h: DKNOP_H };
  }
  function paneelKnop(x, y, w, h, label, ic, aan, mix) {
    fillRoundRect(x, y, w, h, KNOP_R, aan ? C_SURFACE2 : C_SURFACE);
    if (aan) { drawRoundRect(x, y, w, h, KNOP_R, C_CYAN); fillRoundRect(x, y, 5, h, 3, C_CYAN); } else drawRoundRect(x, y, w, h, KNOP_R, C_SURFACE2);
    var fg = aan ? C_CYAN : C_TEXT_DIM, tw = label.length * 12;
    if (ic === I.LAMP) { iconLamp(x + (w >> 1), y + Math.trunc(h * 3 / 8), aan, !!(S.ui && S.ui.ik), fg); tekst(label, x + ((w - tw) >> 1), y + Math.trunc(h * 6 / 8) - 8, 2, fg); }
    else if (ic >= 0) { icoon(ic, x + (w >> 1), y + Math.trunc(h * 3 / 8), fg); tekst(label, x + ((w - tw) >> 1), y + Math.trunc(h * 6 / 8) - 8, 2, fg); }
    else tekst(label, x + ((w - tw) >> 1), y + (h >> 1) - 8, 2, fg);
    if (mix) fillRoundRect(x + 4, y + h - 6, w - 8, 4, 2, C_ORANGE);
  }
  function apparaatKnoppen() {
    tekst('APPARATEN', DKNOP_X1, DKNOP_Y1 - 12, 1, C_TEXT_DIM);
    var lijst = S.paneel || [], totaal = Math.min(9, lijst.length);
    for (var i = 0; i < totaal; i++) {
      var p = lijst[i], r = paneelRect(i, totaal);
      paneelKnop(r.x, r.y, r.w, r.h, (p.lb != null ? p.lb : p.naam) || '', p.ic == null ? -1 : p.ic, p.staat === 2, p.staat === 1);
      zone(r.x, r.y, r.w, r.h, (function (idx) { return function () { return { stuur: { t: 'paneel_toggle', i: idx } }; }; })(i));
    }
  }
  function interieurStatus() {
    var u = S.ui || {}, y = ((S.paneel || []).length > 6) ? INT_Y3 : INT_Y;
    if (y + 30 > NAV_Y) return;
    var x = CTRL_X + 10, w = CTRL_W - 20, h = 38;
    fillRoundRect(x, y, w, h, 6, C_SURFACE); drawRoundRect(x, y, INT_KNOP_W, h, 6, C_SURFACE3);
    var wit = !!u.iw, rood = !!u.ir, cy = y + (h >> 1);
    fillCircle(x + 18, cy, 10, wit ? C_WHITE : C_DARK_GRAY); fillCircle(x + 46, cy, 10, rood ? C_LIGHT_ON_RED : C_DARK_GRAY);
    if (wit) glow(x + 18, cy, 10, C_WHITE, 2); if (rood) glow(x + 46, cy, 10, C_LIGHT_ON_RED, 2);
    tekst('W', x + 11, cy - 3, 1, wit ? C_TEXT_DARK : C_TEXT_DIM); tekst('R', x + 40, cy - 3, 1, rood ? C_TEXT_DARK : C_TEXT_DIM);
    var im = u.im, txt = im === 1 ? 'INT: WIT' : im === 2 ? 'INT: ROOD' : im === 3 ? 'INT: AUTO' : 'INT: UIT';
    var kl = im === 1 ? C_WHITE : im === 2 ? C_LIGHT_ON_RED : im === 3 ? C_CYAN : C_TEXT_DIM;
    tekst(txt, x + 68, cy - 3, 1, kl);
    zone(x, y, INT_KNOP_W, 38, function () { return { stuur: { t: 'interieur_cyclus' } }; });
    if (u.vic) {
      var vx = x + 156; vl(vx - 4, y + 4, h - 8, C_SURFACE2);
      tekst('ACCU', vx, y + 4, 1, C_TEXT_DIM); tekst('ZON', vx + 72, y + 4, 1, C_TEXT_DIM); tekst('DAG', vx + 140, y + 4, 1, C_TEXT_DIM);
      var vk = u.vic.v ? C_TEXT : C_TEXT_DIM;
      tekst(u.vic.a[0], vx, y + 16, 1, vk); tekst(u.vic.a[1], vx + 72, y + 16, 1, vk); tekst(u.vic.a[2], vx + 140, y + 16, 1, vk);
    }
  }
  function schermMain() {
    fillRect(0, 0, W, H, C_BG);
    statusBarMain();
    for (var i = 0; i < 3; i++) vl(CTRL_X - 2 + i, CONTENT_Y, CONTENT_H, C_SURFACE2);
    bootTeken(); bootLichten(); meteoStrip();
    modusKnoppen(); lichtKnoppen(); apparaatKnoppen(); interieurStatus();
    navBar(SC.MAIN);
  }
  // Schermen die (nog) niet nagebouwd zijn: echte statusbalk + navigatiebalk, het midden is voor de HTML-inhoud
  function schermLeeg(titel, actief) {
    fillRect(0, 0, W, H, C_BG);
    sbSchermTeken(titel, C_CYAN);
    navBar(actief);
  }


  // ── HAVEN-dashboard (screen_haven.ino + haven_achtergrond.ino) ─────────────
  // De achtergrondfoto staat op het apparaat als RGB565-framebuffer; hier is dat een offscreen
  // canvas van 800x480 dat naar RGB565 wordt teruggebracht (zelfde afronding als de JPEG-decoder).
  var foto565 = null;
  function fotoPx(x, y) {
    if (!foto565) return C_BG;
    if (x < 0) x = 0; else if (x >= W) x = W - 1; if (y < 0) y = 0; else if (y >= H) y = H - 1;
    return foto565[y * W + x];
  }
  function meng(foto, r5d, g6d, b5d, sterkte) {
    var fr = (foto >> 11) & 31, fg = (foto >> 5) & 63, fb_ = foto & 31;
    var r = fr + Math.trunc((r5d - fr) * sterkte / 255), g = fg + Math.trunc((g6d - fg) * sterkte / 255), b = fb_ + Math.trunc((b5d - fb_) * sterkte / 255);
    return ((r << 11) | (g << 5) | b) & 0xFFFF;
  }
  function tintHaven(x, y, w, h, kleur, sterkte) {
    var r5 = (kleur >> 11) & 31, g6 = (kleur >> 5) & 63, b5 = kleur & 31;
    for (var ry = 0; ry < h; ry++) for (var rx = 0; rx < w; rx++) px(x + rx, y + ry, meng(fotoPx(x + rx, y + ry), r5, g6, b5, sterkte));
  }
  var HV_GAP = 8, HV_TILE_H = 72, HV_SECTIE_H = 18, HV_START_Y = CONTENT_Y, HV_LIST_BOT = NAV_Y - 8, UI_SB_W = 34, UI_SB_BTN = 40;
  var HV_ALG_GAP_KL = 8, HV_ALG_GAP_GR = 24, HV_BACK_W = 112, HV_BACK_H = 26, HV_BACK_GAP = 8, HV_BACK_X = WEB_ZWART_X - HV_BACK_W - HV_BACK_GAP, HV_BACK_Y = HV_BACK_GAP;
  var HV_CONTENT_UIT = 0, hvScroll = 0, hvMaxScroll = 0;
  S.lampen = { hoofdAanwezig: false, hoofdAan: false, kleur: 0, overrule: -1, items: [] };
  S.huis = [];

  function busyheid(x, y, w, h) {
    var vorige = -1, som = 0, n = 0;
    for (var gy = 0; gy < 4; gy++) {
      var sy = y + Math.trunc(gy * h / 4) + Math.trunc(h / 8);
      for (var gx = 0; gx < 6; gx++) {
        var sx = x + Math.trunc(gx * w / 6) + Math.trunc(w / 12), p = fotoPx(sx, sy);
        var r5 = (p >> 11) & 31, g6 = (p >> 5) & 63, b5 = p & 31, hd = Math.trunc((r5 * 8 + g6 * 4 + b5 * 8) / 3);
        if (vorige >= 0) { som += Math.abs(hd - vorige); n++; }
        vorige = hd;
      }
    }
    return n > 0 ? Math.trunc(som / n) : 0;
  }
  function sterkteVoor(x, y, w, h, basis, max) {
    var busy = Math.max(0, Math.min(40, busyheid(x, y, w, h)));
    return basis + Math.trunc((max - basis) * busy / 40);
  }
  function tegelFrame(x, y, w, h, aan) {
    var st = aan ? sterkteVoor(x, y, w, h, 150, 235) : sterkteVoor(x, y, w, h, 128, 225);
    for (var ry = 0; ry < h; ry++) for (var rx = 0; rx < w; rx++) {
      var p = fotoPx(x + rx, y + ry);
      px(x + rx, y + ry, aan ? meng(p, 10, 63, 10, st) : meng(p, 31, 63, 31, st));
    }
    var r = KNOP_R;   // hoekjes buiten de afronding: weer de originele fotokleur
    for (var dy = 0; dy < r; dy++) { var cdy = r - dy; for (var dx = 0; dx < r; dx++) { var cdx = r - dx; if (cdx * cdx + cdy * cdy <= r * r) continue;
      px(x + dx, y + dy, fotoPx(x + dx, y + dy)); px(x + w - 1 - dx, y + dy, fotoPx(x + w - 1 - dx, y + dy));
      px(x + dx, y + h - 1 - dy, fotoPx(x + dx, y + h - 1 - dy)); px(x + w - 1 - dx, y + h - 1 - dy, fotoPx(x + w - 1 - dx, y + h - 1 - dy)); } }
    if (aan) { drawRoundRect(x, y, w, h, KNOP_R, C_CYAN); fillRoundRect(x, y, 5, h, 3, C_CYAN); } else drawRoundRect(x, y, w, h, KNOP_R, C_SURFACE2);
  }
  function hvPeertje(cx, cy, r, kleur, aan) {
    var by = cy - 2, h2 = Math.trunc(r / 2);
    if (aan) { fillCircle(cx, by, r, kleur); glow(cx, by, r, kleur, 2); } else drawCircle(cx, by, r, kleur);
    vl(cx, by + r - 1, h2, kleur); hl(cx - h2, by + r + h2 - 2, r, kleur);
    if (aan) {
      var e = r + h2;
      drawLine(cx, by - r - 2, cx, by - e - 2, kleur); drawLine(cx - r, by - h2, cx - e, by - r, kleur); drawLine(cx + r, by - h2, cx + e, by - r, kleur);
      drawLine(cx - e, by, cx - r - 2, by, kleur); drawLine(cx + e, by, cx + r + 2, by, kleur);
    }
  }
  function hvAanSymbool(cx, cy, r, k) {
    var core = Math.trunc(r * 4 / 7);
    fillCircle(cx, cy, core, k); hl(cx - r, cy, r - core, k); hl(cx + core, cy, r - core, k); vl(cx, cy - r, r - core, k); vl(cx, cy + core, r - core, k);
    var d1 = Math.trunc(core * 0.9), d2 = Math.trunc(r * 0.9);
    drawLine(cx - d2, cy - d2, cx - d1, cy - d1, k); drawLine(cx + d1, cy - d1, cx + d2, cy - d2, k);
    drawLine(cx - d2, cy + d2, cx - d1, cy + d1, k); drawLine(cx + d1, cy + d1, cx + d2, cy + d2, k);
  }
  function hvUitSymbool(cx, cy, r, k) {
    drawCircle(cx, cy, r, k); var d = Math.trunc(r * 0.7);
    drawLine(cx - d, cy - d, cx + d, cy + d, k); drawLine(cx - d, cy + d, cx + d, cy - d, k);
  }
  function io_(v, s) { return Math.trunc(v * s + (v >= 0 ? 0.5 : -0.5)); }
  function hvAppIcoon(ic, cx, cy, s, k) {
    var i, d;
    switch (ic) {
      case I.USB: {
        var o2 = Math.max(2, io_(2, s)), o3 = io_(3, s), o4 = io_(4, s), o5 = io_(5, s), o7 = io_(7, s), o9 = io_(9, s);
        var oTri = Math.trunc(io_(5, s) / 2), rZij = Math.max(1, o2 - 1);
        fillCircle(cx, cy + o7, o2, k); vl(cx, cy + o2, o7 - o2, k);
        drawLine(cx, cy + o2, cx - o4, cy - o2, k); vl(cx - o4, cy - o3, o3 - o2, k); fillCircle(cx - o4, cy - o3 - rZij, rZij, k);
        vl(cx, cy - o9, o9 + o2, k); fillTriangle(cx - oTri, cy - o9, cx + oTri, cy - o9, cx, cy - o9 - oTri, k);
        drawLine(cx, cy + o2, cx + o4, cy - o2, k); vl(cx + o4, cy - o5, o5 - o2, k); fillRect(cx + o4 - rZij, cy - o5 - 2 * rZij, 2 * rZij, 2 * rZij, k);
        break;
      }
      case I.V230: {
        var a1 = io_(1, s), a2 = io_(2, s), a3 = io_(3, s), a4 = io_(4, s), a9 = io_(9, s), th = a3;
        var p1x = cx - th, p1y = cy - a9 + 5, p2x = cx + th, p2y = cy - a9, p3x = cx + a1, p3y = cy - a2, p4x = cx + a4, p4y = cy - a2,
            p5x = cx - a2, p5y = cy + a9, p6x = cx - a1, p6y = cy + a2, p7x = cx - a4, p7y = cy + a2;
        var licht = RGB(255, 240, 150), donker = RGB(200, 130, 10);
        fillTriangle(p3x, p3y, p4x, p4y, p5x, p5y, k); fillTriangle(p3x, p3y, p5x, p5y, p6x, p6y, k); fillTriangle(p3x, p3y, p6x, p6y, p7x, p7y, k);
        fillTriangle(p3x, p3y, p7x, p7y, p1x, p1y, k); fillTriangle(p3x, p3y, p1x, p1y, p2x, p2y, k);
        drawLine(p2x, p2y, p3x, p3y, licht); drawLine(p5x, p5y, p6x, p6y, donker);
        break;
      }
      case I.TV: {
        var dik = Math.max(2, io_(2, s)), t3 = io_(3, s), t4 = io_(4, s), t5 = io_(5, s), t7 = io_(7, s), t8 = io_(8, s), t11 = io_(11, s), t16 = io_(16, s), t10 = io_(10, s);
        var bx = cx - t8, by = cy - t5;
        for (d = -1; d <= 1; d++) { drawLine(cx - t3 + d, by, cx - t7 + d, cy - t10, k); drawLine(cx + t3 + d, by, cx + t7 + d, cy - t10, k); }
        for (i = 0; i < dik; i++) drawRect(bx + i, by + i, t16 - 2 * i, t11 - 2 * i, k);
        for (i = 0; i < dik; i++) drawRoundRect(bx + i, by + i, t16 - 2 * i, t11 - 2 * i, t3, k);
        var tvOnder = by + t11, botW = Math.max(6, t16 - 2 * dik - 2), topW = Math.max(4, Math.trunc(botW * 3 / 5)), vTop = tvOnder, vBot = tvOnder + t4;
        fillTriangle(cx - Math.trunc(topW / 2), vTop, cx + Math.trunc(topW / 2), vTop, cx - Math.trunc(botW / 2), vBot, k);
        fillTriangle(cx + Math.trunc(topW / 2), vTop, cx + Math.trunc(botW / 2), vBot, cx - Math.trunc(botW / 2), vBot, k);
        var voetW = botW + t3; for (i = 0; i < dik; i++) hl(cx - Math.trunc(voetW / 2), vBot + i, voetW, k);
        break;
      }
      case I.WATER: {
        var w1 = io_(1, s), w2 = io_(2, s), w5 = io_(5, s), w9 = io_(9, s), dcx = cx, dcy = cy + w2;
        var licht2 = RGB(195, 228, 255), donker2 = RGB(15, 85, 170);
        var buiten = Math.max(w2 + 1, w5 - w1), binnen = Math.max(1, buiten - w2), mid = binnen + Math.trunc((buiten - binnen) / 2), tip = 12;
        var dbr = Math.max(w2 + 1, w5 + w1 - 3);
        fillTriangle(cx, cy - w9, cx - dbr, cy + w2, cx + dbr, cy + w2, k); fillCircle(dcx, dcy, w5, k);
        fillArc(dcx, dcy, buiten, binnen, 195 + tip, 255 - tip, licht2); fillArc(dcx, dcy, buiten, mid, 195, 195 + tip, licht2); fillArc(dcx, dcy, mid, binnen, 255 - tip, 255, licht2);
        fillArc(dcx, dcy, buiten, binnen, 15 + tip, 75 - tip, donker2); fillArc(dcx, dcy, buiten, mid, 15, 15 + tip, donker2); fillArc(dcx, dcy, mid, binnen, 75 - tip, 75, donker2);
        break;
      }
      case I.DEKLICHT: {
        var e7 = io_(7, s), e4 = io_(4, s), e10 = io_(10, s), e3 = io_(3, s), e2 = io_(2, s), e1 = io_(1, s), e9 = io_(9, s), e8 = io_(8, s);
        fillTriangle(cx - e7, cy - e4, cx + e7, cy - e4, cx, cy - e10, k); fillCircle(cx, cy - e2, e3, k);
        drawLine(cx, cy + e1, cx, cy + e9, k); drawLine(cx - e2, cy + e1, cx - e7, cy + e8, k); drawLine(cx + e2, cy + e1, cx + e7, cy + e8, k);
        break;
      }
      default: icoon(ic, cx, cy, k);
    }
  }
  function hvIcoonKleur(ic, std) { return ic === I.WATER ? RGB(70, 165, 255) : ic === I.V230 ? RGB(255, 205, 40) : std; }
  function hvLabel(label, x, y, w, aan) {
    var maxch = Math.trunc((w - 8) / 6); if (label.length > maxch && maxch > 0) label = label.substr(0, maxch);
    tekst(label, x + ((w - label.length * 6) >> 1), y, 1, aan ? C_CYAN : HV_CONTENT_UIT);
  }
  function hvLampTegel(it, x, y, w, h) {
    var aan = !!it.aan; tegelFrame(x, y, w, h, aan);
    iconLamp(x + (w >> 1), y + Math.trunc(h * 3 / 8), aan, !!S.lampen.kleur, HV_CONTENT_UIT);
    hvLabel(it.naam || '', x, y + Math.trunc(h * 6 / 8) - 2, w, aan);
  }
  function hvOverigTegel(x, y, w, h) {
    var aan = !!S.lampen.hoofdAan; tegelFrame(x, y, w, h, aan);
    iconLamp(x + (w >> 1), y + Math.trunc(h * 3 / 8), aan, !!S.lampen.kleur, HV_CONTENT_UIT);
    hvLabel('OVERIGE LAMPEN', x, y + Math.trunc(h * 6 / 8) - 2, w, aan);
  }
  function hvPaneelTegel(p, x, y, w, h) {
    var aan = p.staat === 2, mix = p.staat === 1; tegelFrame(x, y, w, h, aan);
    var fg = aan ? C_CYAN : HV_CONTENT_UIT, lbl = p.lb != null ? p.lb : (p.naam || ''), ic = p.ic == null ? -1 : p.ic;
    if (ic >= 0) {
      var sc = Math.max(1.2, Math.min(4.5, Math.min(w, h) / 32));
      hvAppIcoon(ic, x + (w >> 1), y + Math.trunc(h * 2 / 5), sc, hvIcoonKleur(ic, fg));
      tekst(lbl, x + ((w - lbl.length * 6) >> 1), y + h - 16, 1, fg);
    } else tekst(lbl, x + ((w - lbl.length * 12) >> 1), y + (h >> 1) - 8, 2, fg);
    if (mix) fillRoundRect(x + 4, y + h - 6, w - 8, 4, 2, C_ORANGE);
  }
  function scrollbarTeken(x, y, h, scroll, maxScroll) {
    if (maxScroll <= 0) return;
    var btn = Math.min(UI_SB_BTN, h >> 1);
    fillRect(x, y, UI_SB_W, h, C_SURFACE2);
    fillRoundRect(x + 2, y + 2, UI_SB_W - 4, btn - 4, 4, C_SURFACE3); fillRoundRect(x + 2, y + h - btn + 2, UI_SB_W - 4, btn - 4, 4, C_SURFACE3);
    tekst('^', x + ((UI_SB_W - 12) >> 1), y + ((btn - 16) >> 1), 2, C_TEXT); tekst('v', x + ((UI_SB_W - 12) >> 1), y + h - btn + ((btn - 16) >> 1), 2, C_TEXT);
    var track = h - 2 * btn;
    if (track > 0) {
      var th = Math.max(14, Math.trunc(track * track / (track + maxScroll))), ty = y + btn + Math.trunc((track - th) * scroll / maxScroll);
      fillRoundRect(x + 5, ty, UI_SB_W - 10, th, 4, C_CYAN);
    }
  }
  function schermHaven() {
    if (S.ui && S.ui.hf) laadFoto(S.ui.hf, function () { if (opties.opnieuw) opties.opnieuw(); });
    fillRect(0, 0, W, H, C_BG);
    // achtergrondfoto over het hele scherm (de balken tinten er straks overheen)
    for (var i = 0; i < W * H; i++) fb[i] = LUT[foto565 ? foto565[i] : C_BG];
    tintFn = tintHaven;
    sbSchermTeken('HAVEN', C_CYAN);
    // "< TERUG"-knop (ui_knop)
    fillRoundRect(HV_BACK_X, HV_BACK_Y, HV_BACK_W, HV_BACK_H, KNOP_R, C_SURFACE2);
    tekst('< TERUG', HV_BACK_X + ((HV_BACK_W - 7 * 12) >> 1), HV_BACK_Y + ((HV_BACK_H - 16) >> 1), 2, C_TEXT_DIM);
    zone(HV_BACK_X, 0, HV_BACK_W + HV_BACK_GAP, SB_H, function () { return { gaNaar: SC.MAIN }; });

    var colW = Math.trunc((W - UI_SB_W - 24) / 2), rightX = 8 + colW + HV_GAP;
    var cols = Math.max(1, Math.min(2, Math.trunc(colW / 150))), tileW = Math.trunc((colW - (cols - 1) * HV_GAP) / cols);
    var y0 = HV_START_Y - hvScroll;
    var lampen = S.lampen.items || [], huis = S.huis || [];
    var licht = [], apps = [];
    huis.forEach(function (p, i) { if (p.ic === I.LAMP) return; if (p.licht) licht.push({ p: p, i: i }); else apps.push({ p: p, i: i }); });
    var totaal = lampen.length + licht.length + (S.lampen.hoofdAanwezig ? 1 : 0);

    // VERLICHTING — ALGEMEEN
    if (y0 + HV_SECTIE_H > HV_START_Y && y0 < HV_LIST_BOT) tekst('VERLICHTING \xE2\x80\x94 ALGEMEEN', 8, y0 + 4, 1, C_TEXT_DIM);
    var sq = Math.trunc((colW - 2 * HV_ALG_GAP_KL - HV_ALG_GAP_GR) / 4), rowY = y0 + HV_SECTIE_H;
    var bx = [8, 0, 0, 0]; bx[1] = bx[0] + sq + HV_ALG_GAP_KL; bx[2] = bx[1] + sq + HV_ALG_GAP_GR; bx[3] = bx[2] + sq + HV_ALG_GAP_KL;
    if (!(rowY + sq <= HV_START_Y || rowY >= HV_LIST_BOT)) {
      var ov = S.lampen.overrule, kl = !!S.lampen.kleur, r = Math.max(4, Math.trunc(sq / 5));
      tegelFrame(bx[0], rowY, sq, sq, ov === 0); hvPeertje(bx[0] + (sq >> 1), rowY + (sq >> 1), r, C_WHITE, !kl);
      tegelFrame(bx[1], rowY, sq, sq, ov === 1); hvPeertje(bx[1] + (sq >> 1), rowY + (sq >> 1), r, C_LIGHT_ON_RED, kl);
      tegelFrame(bx[2], rowY, sq, sq, false); hvAanSymbool(bx[2] + (sq >> 1), rowY + (sq >> 1), Math.max(4, Math.trunc(sq / 4)), C_GREEN);
      tegelFrame(bx[3], rowY, sq, sq, false); hvUitSymbool(bx[3] + (sq >> 1), rowY + (sq >> 1), Math.max(4, Math.trunc(sq / 4)), HV_CONTENT_UIT);
      zone(bx[0], rowY, sq, sq, function () { return { stuur: { t: 'interieur_kleur', rood: 0 } }; });
      zone(bx[1], rowY, sq, sq, function () { return { stuur: { t: 'interieur_kleur', rood: 1 } }; });
      zone(bx[2], rowY, sq, sq, function () { return { stuur: { t: 'lamp_alles', aan: 1 } }; });
      zone(bx[3], rowY, sq, sq, function () { return { stuur: { t: 'lamp_alles', aan: 0 } }; });
    }
    var gridTop = rowY + sq + 10;
    for (var t = 0; t < totaal; t++) {
      var tx = 8 + (t % cols) * (tileW + HV_GAP), ty = gridTop + Math.trunc(t / cols) * (HV_TILE_H + HV_GAP);
      if (ty + HV_TILE_H <= HV_START_Y || ty >= HV_LIST_BOT) continue;
      if (t < lampen.length) { hvLampTegel(lampen[t], tx, ty, tileW, HV_TILE_H); zone(tx, ty, tileW, HV_TILE_H, (function (nr) { return function () { return { stuur: { t: 'lamp_toggle', nr: nr } }; }; })(lampen[t].nr)); }
      else if (t < lampen.length + licht.length) { var lp = licht[t - lampen.length]; hvPaneelTegel(lp.p, tx, ty, tileW, HV_TILE_H); zone(tx, ty, tileW, HV_TILE_H, (function (i) { return function () { return { stuur: { t: 'huispaneel_toggle', i: i } }; }; })(lp.i)); }
      else { hvOverigTegel(tx, ty, tileW, HV_TILE_H); zone(tx, ty, tileW, HV_TILE_H, function () { return { stuur: { t: 'interieur_toggle' } }; }); }
    }
    var rijen = Math.trunc((totaal + cols - 1) / cols), hLinks = (gridTop - y0) + rijen * (HV_TILE_H + HV_GAP);

    // APPARATEN (vast 3x3-raster)
    tekst('APPARATEN', rightX, HV_START_Y + 4, 1, C_TEXT_DIM);
    var aGrid = HV_START_Y + HV_SECTIE_H, aSqW = Math.trunc((colW - 8 - 2 * HV_GAP) / 3), aSqH = Math.trunc(((HV_LIST_BOT - aGrid) - 2 * HV_GAP) / 3), aSq = Math.min(aSqW, aSqH);
    for (var a = 0; a < Math.min(apps.length, 9); a++) {
      var ax = rightX + (a % 3) * (aSq + HV_GAP), ay = aGrid + Math.trunc(a / 3) * (aSq + HV_GAP);
      hvPaneelTegel(apps[a].p, ax, ay, aSq, aSq);
      zone(ax, ay, aSq, aSq, (function (i) { return function () { return { stuur: { t: 'huispaneel_toggle', i: i } }; }; })(apps[a].i));
    }
    hvMaxScroll = Math.max(0, (HV_START_Y + hLinks) - HV_LIST_BOT);
    hvScroll = Math.max(0, Math.min(hvScroll, hvMaxScroll));
    scrollbarTeken(W - UI_SB_W, HV_START_Y, HV_LIST_BOT - HV_START_Y, hvScroll, hvMaxScroll);
    if (hvMaxScroll > 0) {
      zones.unshift({ x: W - UI_SB_W, y: HV_START_Y, w: UI_SB_W, h: HV_LIST_BOT - HV_START_Y, fn: function () { return null; } });
      zones.unshift({ x: W - UI_SB_W, y: HV_START_Y, w: UI_SB_W, h: UI_SB_BTN, fn: function () { return { hvScroll: -30 }; } });
      zones.unshift({ x: W - UI_SB_W, y: HV_LIST_BOT - UI_SB_BTN, w: UI_SB_W, h: UI_SB_BTN, fn: function () { return { hvScroll: 30 }; } });
    }
    navBar(SC.HAVEN);
    tintFn = null;
  }
  // ── INFO (screen_info.ino) ─────────────────────────────────────────────────
  var INFO_TAB_H = 36, INFO_TAB_W = (W / 3) | 0, VELD_START_Y = CONTENT_Y + INFO_TAB_H + 4, VELD_H = 50, VELD_LABEL_W = 120;
  var infoTab = 0;
  function meterNaarFt(v) {
    var m = parseFloat(String(v).replace(',', '.')); if (isNaN(m)) return '';
    var t = m * 3.28084, ft = Math.trunc(t), inch = Math.trunc((t - ft) * 12 + 0.5);
    return '(' + ft + "'" + inch + '")';
  }
  function infoVeld(idx, y, label, waarde, numeriek, verborgen) {
    fillRect(10, y, W - 20, VELD_H - 2, (idx % 2 === 0) ? C_SURFACE : C_BG);
    tekst(label, 18, y + ((VELD_H - 8) >> 1), 1, C_TEXT_DIM);
    if (verborgen) { tekst('(alleen voor de eigenaar)', VELD_LABEL_W + 18, y + ((VELD_H - 8) >> 1), 1, C_DARK_GRAY); return; }
    if (numeriek && waarde) {
      tekst(waarde + ' m', VELD_LABEL_W + 18, y + 6, 2, C_TEXT);
      tekst(meterNaarFt(waarde), VELD_LABEL_W + 18, y + 28, 1, C_TEXT_DIM);
    } else {
      tekst(waarde ? waarde : '(niet ingevuld)', VELD_LABEL_W + 18, y + ((VELD_H - 16) >> 1), 2, waarde ? C_TEXT : C_DARK_GRAY);
    }
  }
  var SYS_KLEUR = function (c) { return [C_TEXT, C_CYAN, C_GREEN, C_AMBER, C_RED_BRIGHT, C_TEXT_DIM][c] || C_TEXT; };
  function sysRijen(rijen, x, w, y0) {
    var y = y0, RH = 32;
    for (var i = 0; i < rijen.length && y + RH < NAV_Y; i++) {
      var r = rijen[i];
      fillRect(x, y, w, RH, (i % 2 === 0) ? C_SURFACE : C_BG);
      if (r[0]) { tekst(r[0], x + 8, y + 12, 1, C_TEXT_DIM); tekst(r[1], x + 8 + r[0].length * 6 + 6, y + 12, 1, SYS_KLEUR(r[2])); }
      else tekst(r[1], x + 8, y + 12, 1, SYS_KLEUR(r[2]));
      y += RH + 2;
    }
  }
  function schermInfo() {
    fillRect(0, 0, W, H, C_BG);
    sbSchermTeken('BOOT & EIGENAAR', C_CYAN);
    // Op de plek van de BEWERK-knop: terug naar de klassieke weergave van de webapp
    var kx = WEB_ZWART_X - 120, ky = (SB_H - 26) >> 1;
    fillRoundRect(kx, ky, 112, 26, KNOP_R, C_SURFACE2);
    tekst('KLASSIEK', kx + ((112 - 8 * 12) >> 1), ky + 5, 2, C_TEXT_DIM);
    zone(kx, 0, 112 + 8, SB_H, function () { return { weergave: true }; });
    var labels = ['BOOT', 'EIGENAAR', 'SYSTEEM'];
    for (var i = 0; i < 3; i++) {
      var act = infoTab === i, x = i * INFO_TAB_W;
      fillRect(x, CONTENT_Y, INFO_TAB_W, INFO_TAB_H, act ? C_SURFACE2 : C_SURFACE);
      if (act) { hl(x + 10, CONTENT_Y, INFO_TAB_W - 20, C_CYAN); hl(x + 10, CONTENT_Y + 1, INFO_TAB_W - 20, C_CYAN); }
      tekst(labels[i], x + ((INFO_TAB_W - labels[i].length * 12) >> 1), CONTENT_Y + ((INFO_TAB_H - 16) >> 1), 2, act ? C_CYAN : C_TEXT_DIM);
      zone(x, CONTENT_Y, INFO_TAB_W, INFO_TAB_H, (function (t) { return function () { return { infoTab: t }; }; })(i));
    }
    hl(0, CONTENT_Y + INFO_TAB_H, W, C_SURFACE2);
    var d = S.info, fy = VELD_START_Y;
    if (!d) { tekst('Gegevens worden geladen...', 18, fy + 16, 2, C_TEXT_DIM); navBar(SC.INFO); return; }
    if (infoTab === 0) {
      (d.boot || []).forEach(function (v, k) { infoVeld(k, fy, v[0], v[1], !!v[2], false); fy += VELD_H; });
    } else if (infoTab === 1) {
      (d.eig || []).forEach(function (v, k) { infoVeld(k, fy, v[0], v[1] || '', false, v[1] === null); fy += VELD_H; });
    } else if (!d.sys) {
      fillRect(10, fy, W - 20, VELD_H - 2, C_SURFACE);
      tekst('Alleen zichtbaar voor de eigenaar', 18, fy + ((VELD_H - 16) >> 1), 2, C_TEXT_DIM);
      var bx = W - 10 - 150, by = fy + ((VELD_H - 2 - 30) >> 1);
      fillRoundRect(bx, by, 140, 30, KNOP_R, C_CYAN); tekst('INLOGGEN', bx + ((140 - 96) >> 1), by + 7, 2, C_TEXT_DARK);
      zone(bx, by, 140, 30, function () { return { slot: true }; });
    } else {
      var sy = d.sys, ny = VELD_START_Y;
      fillRect(10, ny, W - 20, VELD_H - 2, C_SURFACE2);
      tekst('Apparaatnaam', 18, ny + ((VELD_H - 8) >> 1) - 5, 1, C_TEXT_DIM);
      var nm = sy.naam && sy.naam !== 'BKOS-NUI' ? sy.naam : '';
      tekst(nm || '(niet ingesteld)', 18 + VELD_LABEL_W, ny + ((VELD_H - 16) >> 1) + 2, 2, nm ? C_CYAN : C_DARK_GRAY);
      var ry = ny + VELD_H + 4;
      for (var j = 0; j < 3; j++) vl(398 + j - 1, ry, NAV_Y - ry, j === 1 ? C_SURFACE2 : C_BG);
      sysRijen(sy.l || [], 10, 370, ry);
      sysRijen(sy.r || [], 402, 388, ry);
    }
    navBar(SC.INFO);
  }

  // ── BERICHT (screen_bericht.ino) ───────────────────────────────────────────
  var BER_HDR_H = 28, BER_KOL = 2, BER_GAP = 8, BER_NOOD_LBL_H = 18;
  function berNood() {
    var b = S.ber || {}, uit = [];
    (b.nood || []).forEach(function (n, i) { if (!n.alleenAnker || S.modus === 3) uit.push(i); });
    return uit;
  }
  function berRect(i, nP, nN) {
    var top = CONTENT_Y + BER_HDR_H + 22, rijenP = Math.ceil(nP / BER_KOL), rijen = rijenP + Math.ceil(nN / BER_KOL);
    var gh = Math.trunc((NAV_Y - 6 - top - BER_NOOD_LBL_H - BER_GAP * (rijen - 1)) / rijen), gw = Math.trunc((W - BER_GAP * (BER_KOL + 1)) / BER_KOL);
    var y0 = top, k = i;
    if (i >= nP) { k = i - nP; y0 += rijenP * (gh + BER_GAP) + BER_NOOD_LBL_H; }
    return { x: BER_GAP + (k % BER_KOL) * (gw + BER_GAP), y: y0 + Math.trunc(k / BER_KOL) * (gh + BER_GAP), w: gw, h: gh };
  }
  function berKnop(r, t, rand, aan) {
    fillRoundRect(r.x, r.y, r.w, r.h, 6, C_SURFACE); drawRoundRect(r.x, r.y, r.w, r.h, 6, rand);
    var size = 2, tw = t.length * 12;
    if (tw > r.w - 12) { size = 1; tw = t.length * 6; }
    if (tw > r.w - 8) { var mx = Math.trunc((r.w - 8) / 6); t = t.substr(0, mx - 1) + '.'; tw = t.length * 6; }
    tekst(t, r.x + ((r.w - tw) >> 1), r.y + (r.h >> 1) - (size === 2 ? 8 : 4), size, aan ? C_TEXT : C_DARK_GRAY);
  }
  function schermBericht() {
    fillRect(0, 0, W, H, C_BG);
    sbSchermTeken('BERICHT', C_CYAN);
    fillRect(0, CONTENT_Y, W, BER_HDR_H, C_SURFACE2);
    tekst('BERICHT AAN EIGENAAR', 10, CONTENT_Y + ((BER_HDR_H - 16) >> 1), 2, C_CYAN);
    var b = S.ber;
    if (!b) { tekst('Berichten worden geladen...', 10, CONTENT_Y + BER_HDR_H + 5, 1, C_TEXT_DIM); navBar(SC.BERICHT); return; }
    var aan = !!b.aan, presets = b.presets || [], nood = berNood(), nP = presets.length;
    if (!aan) tekst('Berichten staan UIT op deze boordcomputer - de eigenaar kan ze aanzetten via CONFIG', 10, CONTENT_Y + BER_HDR_H + 5, 1, C_AMBER);
    else tekst('Tik een bericht om het naar de eigenaar te sturen (je vult daarna je naam en telefoon in)', 10, CONTENT_Y + BER_HDR_H + 5, 1, C_TEXT_DIM);
    presets.forEach(function (t, i) {
      var r = berRect(i, nP, nood.length); berKnop(r, t, aan ? C_CYAN : C_SURFACE3, aan);
      if (aan) zone(r.x, r.y, r.w, r.h, function () { return { bericht: i }; });
    });
    if (nood.length) {
      var r0 = berRect(nP, nP, nood.length);
      tekst('NOODGEVAL - gaat meteen als alarm naar de eigenaar', BER_GAP + 2, r0.y - BER_NOOD_LBL_H + ((BER_NOOD_LBL_H - 8) >> 1), 1, C_RED_BRIGHT);
      nood.forEach(function (ni, k) {
        var r = berRect(nP + k, nP, nood.length); berKnop(r, b.nood[ni].tekst, aan ? C_RED_BRIGHT : C_SURFACE3, aan);
        if (aan) zone(r.x, r.y, r.w, r.h, function () { return { nood: ni }; });
      });
    }
    var f = S.berFlits;
    if (f && Date.now() < f.tot) { fillRect(0, NAV_Y - 20, W, 20, f.kl === 'fout' ? C_RED_BRIGHT : f.kl === 'nood' ? C_AMBER : C_GREEN); tekst(f.txt, 12, NAV_Y - 19, 2, C_BG); }
    navBar(SC.BERICHT);
  }

  // Foto laden uit een URL en terugbrengen naar RGB565 (r>>3, g>>2, b>>3 zoals de JPEG-decoder in de firmware)
  var fotoUrl = null;
  function laadFoto(url, klaar) {
    if (!url || url === fotoUrl) return; fotoUrl = url;
    var img = new Image();
    img.onload = function () {
      var c = document.createElement('canvas'); c.width = W; c.height = H; var x = c.getContext('2d');
      x.drawImage(img, 0, 0, W, H); var d = x.getImageData(0, 0, W, H).data, o = new Uint16Array(W * H);
      for (var i = 0; i < W * H; i++) o[i] = ((d[i * 4] >> 3) << 11) | ((d[i * 4 + 1] >> 2) << 5) | (d[i * 4 + 2] >> 3);
      foto565 = o; if (klaar) klaar();
    };
    img.onerror = function () { fotoUrl = null; };
    img.src = url;
  }

  // ── Publieke API ───────────────────────────────────────────────────────────
  var cv, ctx, imgData, u32, opties = {};
  function init(canvas, opt) {
    cv = canvas; opties = opt || {}; cv.width = W; cv.height = H; ctx = cv.getContext('2d');
    imgData = ctx.createImageData(W, H); u32 = new Uint32Array(imgData.data.buffer);
    cv.addEventListener('click', function (e) {
      var r = cv.getBoundingClientRect(), x = Math.floor((e.clientX - r.left) * W / r.width), y = Math.floor((e.clientY - r.top) * H / r.height);
      klik(x, y);
    });
  }
  function klik(x, y) {
    for (var i = 0; i < zones.length; i++) {
      var z = zones[i], hit = z.cirkel ? ((x - z.x) * (x - z.x) + (y - z.y) * (y - z.y) <= z.r * z.r) : (x >= z.x && x < z.x + z.w && y >= z.y && y < z.y + z.h);
      if (!hit) continue;
      var a = z.fn(); if (!a) return;
      if (a.navScroll) { navScroll = Math.max(0, navScroll + a.navScroll); teken(); return; }
      if (a.hvScroll) { hvScroll = Math.max(0, Math.min(hvMaxScroll, hvScroll + a.hvScroll)); teken(); return; }
      if (a.infoTab != null) { infoTab = a.infoTab; teken(); return; }
      if (opties.actie) opties.actie(a);
      return;
    }
  }
  function zet(m) {
    if (!m || !m.t) return;
    if (m.t === 'ui') { S.ui = m; if (m.pal) zetPalet(m.pal); }   // foto: pas bij het HAVEN-scherm (schermHaven)
    else if (m.t === 'lampen') S.lampen = m;
    else if (m.t === 'huispaneel') S.huis = m.items || [];
    else if (m.t === 'state') { S.modus = m.m; S.licht = m.l; S.h = m.h; }
    else if (m.t === 'paneel') S.paneel = m.items || [];
    else if (m.t === 'info') { S.bootnaam = m.boot || ''; S.versie = m.ver || ''; }
    else if (m.t === 'info_scherm') S.info = m;
    else if (m.t === 'ber') S.ber = m;
  }
  // Geeft true terug als dit scherm volledig door de canvas getekend wordt
  function volledig(scherm) { return scherm === SC.MAIN || scherm === SC.HAVEN || scherm === SC.INFO || scherm === SC.BERICHT; }
  function teken(scherm, titel) {
    if (scherm == null) scherm = S.scherm; S.scherm = scherm; zones = [];
    tintFn = null;
    if (scherm === SC.MAIN) schermMain(); else if (scherm === SC.HAVEN) schermHaven();
    else if (scherm === SC.INFO) schermInfo(); else if (scherm === SC.BERICHT) schermBericht();
    else schermLeeg(titel || 'BKOS', scherm);
    u32.set(fb); ctx.putImageData(imgData, 0, 0);
  }
  return { _zetFoto: function (a) { foto565 = a; }, init: init, zet: zet, teken: teken, volledig: volledig, S: S, SC: SC, W: W, H: H, SB_H: SB_H, NAV_Y: NAV_Y, _fb: fb, _lut: LUT };
})();
