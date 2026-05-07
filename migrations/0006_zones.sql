-- Phase 17: zone partitioning
-- Each node belongs to exactly one zone (subsystem/layer/bounded-context).
-- Auto-detected from path prefix; manual override via `icmg zone assign`.
-- Default zone = 'default' so existing rows are not nullable.

ALTER TABLE graph_nodes  ADD COLUMN zone TEXT NOT NULL DEFAULT 'default';
ALTER TABLE memory_nodes ADD COLUMN zone TEXT NOT NULL DEFAULT 'default';

CREATE INDEX IF NOT EXISTS idx_graph_zone  ON graph_nodes(zone);
CREATE INDEX IF NOT EXISTS idx_memory_zone ON memory_nodes(zone);

CREATE TABLE IF NOT EXISTS zone_config (
    zone        TEXT PRIMARY KEY,
    description TEXT,
    path_glob   TEXT,
    color       TEXT,
    created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);

-- Seed minimal default zone
INSERT OR IGNORE INTO zone_config(zone, description) VALUES('default', 'Catch-all zone');
