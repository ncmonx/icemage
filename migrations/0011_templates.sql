-- Phase 25 T2: persisted template manifests for clone-parity workflow.
-- Manifest JSON includes required_symbols + structural_markers + body_hash
-- of source file (staleness detection on apply).
CREATE TABLE IF NOT EXISTS templates (
    name          TEXT    PRIMARY KEY,
    source_path   TEXT    NOT NULL,
    manifest_json TEXT    NOT NULL,
    body_hash     TEXT    NOT NULL DEFAULT '',
    memoir_id     INTEGER,
    created_at    INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    updated_at    INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_templates_source ON templates(source_path);
