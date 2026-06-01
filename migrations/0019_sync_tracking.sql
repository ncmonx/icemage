-- Phase 48 T1: sync log per shareable table.
CREATE TABLE IF NOT EXISTS sync_log (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    table_name  TEXT NOT NULL,
    op          TEXT NOT NULL,           -- push | pull | merge
    rows_in     INTEGER NOT NULL DEFAULT 0,
    rows_out    INTEGER NOT NULL DEFAULT 0,
    conflicts   INTEGER NOT NULL DEFAULT 0,
    elapsed_ms  INTEGER NOT NULL DEFAULT 0,
    actor       TEXT NOT NULL DEFAULT '',
    created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_sync_log_created ON sync_log(created_at);

-- row_version on graph_nodes for sync conflict detection.
ALTER TABLE graph_nodes ADD COLUMN row_version INTEGER NOT NULL DEFAULT 0;
ALTER TABLE graph_nodes ADD COLUMN created_by TEXT NOT NULL DEFAULT '';
