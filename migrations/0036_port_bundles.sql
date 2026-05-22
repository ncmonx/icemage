-- v1.24.0 (P1): cross-project port bundles.
--
-- A port bundle is a transportable artifact containing N source files
-- compressed + glossary-deduped. Created by `icmg port export`, applied
-- to a different project by `icmg port apply <artifact>`.
--
-- The artifact itself lives outside the DB (a `.icmg-port` file on disk
-- with magic header `IPRT` + version + sha256 + zstd payload). This
-- table tracks bundles created on THIS machine for telemetry + listing.

CREATE TABLE IF NOT EXISTS port_bundles (
    id                     INTEGER PRIMARY KEY AUTOINCREMENT,
    name                   TEXT NOT NULL UNIQUE,
    source_project         TEXT NOT NULL,              -- cwd at export time
    file_count             INTEGER NOT NULL DEFAULT 0,
    total_bytes_raw        INTEGER NOT NULL DEFAULT 0,
    total_bytes_compressed INTEGER NOT NULL DEFAULT 0,
    artifact_path          TEXT NOT NULL,              -- where .icmg-port was written
    artifact_sha256        TEXT NOT NULL,              -- artifact integrity
    manifest               TEXT NOT NULL,              -- JSON file list (paths + sizes), NOT content
    applied_count          INTEGER NOT NULL DEFAULT 0,
    created_at             INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);

CREATE INDEX IF NOT EXISTS idx_port_bundles_created ON port_bundles(created_at);
CREATE INDEX IF NOT EXISTS idx_port_bundles_sha     ON port_bundles(artifact_sha256);
