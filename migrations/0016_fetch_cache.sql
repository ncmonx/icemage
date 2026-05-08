-- Phase 46 T1: fetched-URL cache (URL+ETag keyed, content-aware reduction).
-- Avoids re-download + re-reduce on repeat fetches within session.
CREATE TABLE IF NOT EXISTS fetch_cache (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    url          TEXT NOT NULL UNIQUE,
    etag         TEXT NOT NULL DEFAULT '',
    content_kind TEXT NOT NULL DEFAULT 'unknown',
    body_reduced TEXT NOT NULL,
    bytes_in     INTEGER NOT NULL DEFAULT 0,
    bytes_out    INTEGER NOT NULL DEFAULT 0,
    hit_count    INTEGER NOT NULL DEFAULT 0,
    fetched_at   INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    expires_at   INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_fetch_url ON fetch_cache(url);
CREATE INDEX IF NOT EXISTS idx_fetch_expires ON fetch_cache(expires_at);
