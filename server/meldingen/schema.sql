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
