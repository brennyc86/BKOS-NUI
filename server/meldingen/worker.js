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
const tekst = (v, max) => (typeof v === "string" ? v.slice(0, max) : "");

export default {
  async fetch(req, env) {
    const url = new URL(req.url);

    if (url.pathname === "/ingest" && req.method === "POST") {
      if (!(await gelijk(req.headers.get("x-ingest-key"), env.INGEST_KEY))) return json({ ok: false }, 401);
      let b;
      try { b = await req.json(); } catch { return json({ ok: false, reden: "json" }, 400); }
      if (!SOORTEN.includes(b.soort)) return json({ ok: false, reden: "soort" }, 400);
      const device = tekst(b.device, 32), inhoud = tekst(b.inhoud, MAX_INHOUD);
      if (!device || !inhoud) return json({ ok: false, reden: "leeg" }, 400);

      const uur = await env.DB.prepare("SELECT COUNT(*) n FROM meldingen WHERE device=? AND ontvangen > strftime('%Y-%m-%dT%H:%M:%SZ','now','-1 hour')").bind(device).first();
      const dag = await env.DB.prepare("SELECT COUNT(*) n FROM meldingen WHERE device=? AND ontvangen > strftime('%Y-%m-%dT%H:%M:%SZ','now','-1 day')").bind(device).first();
      if (uur.n >= MAX_UURLIMIET || dag.n >= MAX_DAGLIMIET) return json({ ok: false, reden: "limiet" }, 429);

      await env.DB.prepare("INSERT INTO meldingen (soort,boot,device,versie,inhoud) VALUES (?,?,?,?,?)")
        .bind(b.soort, tekst(b.boot, 64) || "(naam niet ingesteld)", device, tekst(b.versie, 32), inhoud).run();
      if (Math.random() < 0.02) // opschonen: bewaar 365 dagen
        await env.DB.prepare("DELETE FROM meldingen WHERE ontvangen < strftime('%Y-%m-%dT%H:%M:%SZ','now','-365 days')").run();
      return json({ ok: true });
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
      const r = await env.DB.prepare("SELECT id,ontvangen,titel,tekst FROM berichten WHERE (device=? OR device='*') AND id>? ORDER BY id ASC LIMIT 5").bind(device, na).all();
      return json({ ok: true, berichten: r.results });
    }
    // Beheerder (leessleutel) stuurt een bericht naar één boot of '*'.
    if (url.pathname === "/post/stuur" && req.method === "POST") {
      if (!(await gelijk(req.headers.get("x-read-key"), env.READ_KEY))) return json({ ok: false }, 401);
      let b;
      try { b = await req.json(); } catch { return json({ ok: false, reden: "json" }, 400); }
      const device = tekst(b.device, 32), titel = tekst(b.titel, 60), t = tekst(b.tekst, 600);
      if (!device || !titel || !t) return json({ ok: false, reden: "leeg" }, 400);
      const r = await env.DB.prepare("INSERT INTO berichten (device,titel,tekst) VALUES (?,?,?)").bind(device, titel, t).run();
      return json({ ok: true, id: r.meta.last_row_id });
    }

    return json({ ok: false }, 404);
  },
};
