-- Phase 45 T1: tool-call result cache (content-hash keyed).
-- Saves recompute on repeat pack/recall/graph-context within TTL window.
CREATE TABLE IF NOT EXISTS tool_call_cache (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    cmd          TEXT NOT NULL,
    content_hash TEXT NOT NULL UNIQUE,
    result_blob  TEXT NOT NULL,
    hit_count    INTEGER NOT NULL DEFAULT 0,
    created_at   INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    expires_at   INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_tcc_hash ON tool_call_cache(content_hash);
CREATE INDEX IF NOT EXISTS idx_tcc_expires ON tool_call_cache(expires_at);
