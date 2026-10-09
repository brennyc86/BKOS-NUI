// BKOS meldingen — write-only invoer voor boordcomputers, lezen alleen met aparte sleutel.
// Secrets (Worker): INGEST_KEY (zit in de firmware, kan alleen toevoegen), READ_KEY (alleen beheerder).
// Binding: DB (D1).
const SOORTEN = ["fout", "feedback", "suggestie", "schakellog"];
const MAX_INHOUD = 8000, MAX_UURLIMIET = 60, MAX_DAGLIMIET = 400;

const json = (o, s = 200) => new Response(JSON.stringify(o), { status: s, headers: { "content-type": "application/json" } });

async function gelijk(a, b) {
  const enc = new TextEncoder();
  const [x, y] = [enc.encode(a || ""), enc.encode(b || "")];
  if (x.length !== y.length || x.length === 0) return false;
  return crypto.subtle.timingSafeEqual(x, y);
}
// Geweigerde verzoeken bijhouden (zonder sleutels/inhoud), zodat een boordcomputer die "niets" lijkt te
// versturen achteraf verklaard kan worden. Beheerder leest via GET /afgewezen.
async function weiger(env, req, status, reden, extra = "") {
  try {
    await env.DB.prepare("INSERT INTO afgewezen (status,reden,pad,ua,lengte,extra) VALUES (?,?,?,?,?,?)")
      .bind(status, reden, new URL(req.url).pathname, (req.headers.get("user-agent") || "").slice(0, 80), parseInt(req.headers.get("content-length") || "0", 10) || 0, String(extra).slice(0, 120)).run();
    if (Math.random() < 0.05) await env.DB.prepare("DELETE FROM afgewezen WHERE id < (SELECT MAX(id) FROM afgewezen) - 300").run();
  } catch {}
  return json({ ok: false, reden }, status);
}
const tekst = (v, max) => (typeof v === "string" ? v.slice(0, max) : "");

export default {
  async fetch(req, env) {
    const url = new URL(req.url);

    if (url.pathname === "/ingest" && req.method === "POST") {
      if (!(await gelijk(req.headers.get("x-ingest-key"), env.INGEST_KEY))) return weiger(env, req, 401, "sleutel", req.headers.has("x-ingest-key") ? "sleutel-fout" : "geen-sleutel");
      let b, ruw = "";
      try { ruw = await req.text(); b = JSON.parse(ruw); } catch (e) { return weiger(env, req, 400, "json", ruw.slice(0, 100)); }
      if (!SOORTEN.includes(b.soort)) return weiger(env, req, 400, "soort", String(b.soort));
      const device = tekst(b.device, 32), inhoud = tekst(b.inhoud, MAX_INHOUD);
      if (!device || !inhoud) return weiger(env, req, 400, "leeg");

      const uur = await env.DB.prepare("SELECT COUNT(*) n FROM meldingen WHERE device=? AND ontvangen > strftime('%Y-%m-%dT%H:%M:%SZ','now','-1 hour')").bind(device).first();
      const dag = await env.DB.prepare("SELECT COUNT(*) n FROM meldingen WHERE device=? AND ontvangen > strftime('%Y-%m-%dT%H:%M:%SZ','now','-1 day')").bind(device).first();
      if (uur.n >= MAX_UURLIMIET || dag.n >= MAX_DAGLIMIET) return weiger(env, req, 429, "limiet", device);

      await env.DB.prepare("INSERT INTO meldingen (soort,boot,device,versie,inhoud) VALUES (?,?,?,?,?)")
        .bind(b.soort, tekst(b.boot, 64) || "(naam niet ingesteld)", device, tekst(b.versie, 32), inhoud).run();
      if (Math.random() < 0.02) // opschonen: bewaar 365 dagen
        await env.DB.prepare("DELETE FROM meldingen WHERE ontvangen < strftime('%Y-%m-%dT%H:%M:%SZ','now','-365 days')").run();
      return json({ ok: true });
    }

    if (url.pathname === "/afgewezen" && req.method === "GET") {
      if (!(await gelijk(req.headers.get("x-read-key"), env.READ_KEY))) return json({ ok: false }, 401);
      const r = await env.DB.prepare("SELECT * FROM afgewezen ORDER BY id DESC LIMIT 30").all();
      return json({ ok: true, afgewezen: r.results });
    }

    if (url.pathname === "/lees" && req.method === "GET") {
      if (!(await gelijk(req.headers.get("x-read-key"), env.READ_KEY))) return json({ ok: false }, 401);
      const w = [], p = [];
      for (const k of ["soort", "boot", "device"]) if (url.searchParams.get(k)) { w.push(`${k}=?`); p.push(url.searchParams.get(k)); }
      if (url.searchParams.get("na_id")) { w.push("id>?"); p.push(parseInt(url.searchParams.get("na_id"), 10) || 0); }
      const limit = Math.min(parseInt(url.searchParams.get("limit") || "50", 10) || 50, 200);
      const r = await env.DB.prepare(`SELECT * FROM meldingen ${w.length ? "WHERE " + w.join(" AND ") : ""} ORDER BY id DESC LIMIT ${limit}`).bind(...p).all();
      return json({ ok: true, meldingen: r.results });
    }

    // ─── Terugweg: boordcomputer haalt berichten voor zichzelf op ────────────────
    // Alleen berichten voor dit device-ID (of '*'); device-ID is een afgeleide hash
    // en niet te raden. Auth: dezelfde invoer-sleutel als /ingest.
    if (url.pathname === "/post" && req.method === "GET") {
      if (!(await gelijk(req.headers.get("x-ingest-key"), env.INGEST_KEY))) return json({ ok: false }, 401);
      const device = tekst(url.searchParams.get("device"), 32);
      const na = parseInt(url.searchParams.get("na") || "0", 10) || 0;
      if (!device) return json({ ok: false, reden: "device" }, 400);
      const r = await env.DB.prepare("SELECT id,ontvangen,titel,tekst,actie FROM berichten WHERE (device=? OR device='*') AND id>? ORDER BY id ASC LIMIT 5").bind(device, na).all();
      return json({ ok: true, berichten: r.results });
    }
    // Beheerder (leessleutel) stuurt een bericht naar één boot of '*'.
    if (url.pathname === "/post/stuur" && req.method === "POST") {
      if (!(await gelijk(req.headers.get("x-read-key"), env.READ_KEY))) return json({ ok: false }, 401);
      let b;
      try { b = await req.json(); } catch { return json({ ok: false, reden: "json" }, 400); }
      const device = tekst(b.device, 32), titel = tekst(b.titel, 60), t = tekst(b.tekst, 600);
      if (!device || !titel || !t) return json({ ok: false, reden: "leeg" }, 400);
      // Optionele actie: {"type":"instellingen","items":[{"k":"...","v":N}]} of {"type":"update"}.
      // De boordcomputer past alleen toe wat in zijn eigen whitelist staat, en altijd met pincode
      // (updates: pincode tenzij de eigenaar "updates zonder pincode" heeft aangezet).
      let actie = null;
      if (b.actie != null) {
        const a = b.actie;
        if (!a || !["instellingen", "update"].includes(a.type)) return json({ ok: false, reden: "actie-type" }, 400);
        if (a.type === "instellingen" && (!Array.isArray(a.items) || a.items.length < 1 || a.items.length > 6 ||
            a.items.some(i => typeof i.k !== "string" || !Number.isFinite(i.v)))) return json({ ok: false, reden: "actie-items" }, 400);
        actie = JSON.stringify(a);
        if (actie.length > 260) return json({ ok: false, reden: "actie-lengte" }, 400);
      }
      const r = await env.DB.prepare("INSERT INTO berichten (device,titel,tekst,actie) VALUES (?,?,?,?)").bind(device, titel, t, actie).run();
      return json({ ok: true, id: r.meta.last_row_id });
    }

    return json({ ok: false }, 404);
  },
};
