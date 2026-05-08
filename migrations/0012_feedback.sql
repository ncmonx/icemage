-- Phase 27 T3: feedback recording for recall reranking.
CREATE TABLE IF NOT EXISTS feedback (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    node_id     INTEGER NOT NULL,
    query       TEXT NOT NULL DEFAULT '',
    score       INTEGER NOT NULL,
    created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_feedback_node    ON feedback(node_id);
CREATE INDEX IF NOT EXISTS idx_feedback_created ON feedback(created_at);
