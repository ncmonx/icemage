-- Phase 03 amendments: access_count column + scan_runs table

BEGIN;

-- A10: access counter per graph node
ALTER TABLE graph_nodes ADD COLUMN access_count INTEGER NOT NULL DEFAULT 0;

-- A8: scan run history for graph diff
CREATE TABLE IF NOT EXISTS scan_runs (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    root_path   TEXT    NOT NULL,
    node_count  INTEGER NOT NULL DEFAULT 0,
    edge_count  INTEGER NOT NULL DEFAULT 0,
    created_at  INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_scan_runs_created ON scan_runs(created_at);

COMMIT;
