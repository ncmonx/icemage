-- Phase 18: function/symbol-level nodes (two-tier graph)
-- File nodes stay (kind='file'); symbol nodes (class/function/method/sp) are children.
-- parent_id links symbol → file. body_hash enables per-symbol staleness.

ALTER TABLE graph_nodes ADD COLUMN parent_id   INTEGER REFERENCES graph_nodes(id) ON DELETE CASCADE;
ALTER TABLE graph_nodes ADD COLUMN kind        TEXT NOT NULL DEFAULT 'file';
ALTER TABLE graph_nodes ADD COLUMN symbol_name TEXT;
ALTER TABLE graph_nodes ADD COLUMN signature   TEXT;
ALTER TABLE graph_nodes ADD COLUMN line_start  INTEGER;
ALTER TABLE graph_nodes ADD COLUMN line_end    INTEGER;
ALTER TABLE graph_nodes ADD COLUMN body_hash   TEXT;

CREATE INDEX IF NOT EXISTS idx_graph_parent ON graph_nodes(parent_id);
CREATE INDEX IF NOT EXISTS idx_graph_kind   ON graph_nodes(kind);
CREATE INDEX IF NOT EXISTS idx_graph_symbol ON graph_nodes(symbol_name) WHERE symbol_name IS NOT NULL;
