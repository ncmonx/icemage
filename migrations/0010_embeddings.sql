-- Phase 23 Task 1: embeddings table for semantic recall
-- Stores 384-dim float32 vectors as BLOB; manual cosine in C++ (no sqlite-vec dep).
-- node_id references either memory_nodes.id (kind='memory') or graph_nodes.id (kind='graph').
CREATE TABLE IF NOT EXISTS embeddings (
    node_id     INTEGER NOT NULL,
    kind        TEXT    NOT NULL,           -- 'memory' | 'graph'
    vec         BLOB    NOT NULL,           -- packed float32[dim]
    dim         INTEGER NOT NULL,
    model       TEXT    NOT NULL DEFAULT 'all-MiniLM-L6-v2',
    body_hash   TEXT    NOT NULL DEFAULT '',-- for staleness detection
    created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    PRIMARY KEY (node_id, kind)
);

CREATE INDEX IF NOT EXISTS idx_embeddings_kind ON embeddings(kind);
CREATE INDEX IF NOT EXISTS idx_embeddings_hash ON embeddings(kind, body_hash);
