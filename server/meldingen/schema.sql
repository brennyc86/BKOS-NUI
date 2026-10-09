CREATE TABLE IF NOT EXISTS meldingen (
  id       INTEGER PRIMARY KEY AUTOINCREMENT,
  ontvangen TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
  soort    TEXT NOT NULL,   -- fout | feedback | suggestie | schakellog
  boot     TEXT NOT NULL,
  device   TEXT NOT NULL,
  versie   TEXT NOT NULL,
  inhoud   TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_meldingen_tijd   ON meldingen(ontvangen);
CREATE INDEX IF NOT EXISTS idx_meldingen_device ON meldingen(device, ontvangen);

-- Terugweg: berichten van de ontwikkelaar aan één boot (device) of aan iedereen ('*').
CREATE TABLE IF NOT EXISTS berichten (
  id        INTEGER PRIMARY KEY AUTOINCREMENT,
  ontvangen TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
  device    TEXT NOT NULL,   -- device-ID van de boot, of '*' voor alle boten
  titel     TEXT NOT NULL,
  tekst     TEXT NOT NULL,
  actie     TEXT             -- optioneel JSON: {type:instellingen|update,...}
);
CREATE INDEX IF NOT EXISTS idx_berichten_device ON berichten(device, id);

-- Geweigerde verzoeken (zonder sleutel of inhoud) voor diagnose.
CREATE TABLE IF NOT EXISTS afgewezen (
  id        INTEGER PRIMARY KEY AUTOINCREMENT,
  ontvangen TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
  status    INTEGER, reden TEXT, pad TEXT, ua TEXT, lengte INTEGER, extra TEXT
);

-- Actie bij een bericht (voorstel voor instellingen of een update); ALTER voor bestaande databases:
--   ALTER TABLE berichten ADD COLUMN actie TEXT;
