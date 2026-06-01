-- v0.42.0: context_nodes — structured CLAUDE.md sections + skill index stored as graph nodes.
-- Replaces full CLAUDE.md load with targeted BM25 injection via hooks.
-- tier: 'hot' (always inject at session start) | 'cold' (on-demand) | 'skill' (skill index)
CREATE TABLE IF NOT EXISTS context_nodes (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    node_key    TEXT    NOT NULL UNIQUE,
    title       TEXT    NOT NULL,
    content     TEXT    NOT NULL,
    source_file TEXT    NOT NULL DEFAULT '',
    tier        TEXT    NOT NULL DEFAULT 'cold',
    tags        TEXT    NOT NULL DEFAULT '[]',
    active      INTEGER NOT NULL DEFAULT 1,
    created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    updated_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_context_nodes_tier   ON context_nodes(tier);
CREATE INDEX IF NOT EXISTS idx_context_nodes_active ON context_nodes(active);
CREATE INDEX IF NOT EXISTS idx_context_nodes_source ON context_nodes(source_file);
