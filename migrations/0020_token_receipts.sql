-- Phase 67 T1: per-pack token receipt — itemize emitted sections.
-- Each pack call writes one row per emitted section so users see which
-- subsystem (memory/graph/file/etc.) costs how many tokens.
CREATE TABLE IF NOT EXISTS token_receipts (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id  TEXT NOT NULL DEFAULT '',
    cmd         TEXT NOT NULL,           -- 'pack' | 'context' | 'recall'
    source      TEXT NOT NULL,           -- 'memory' | 'graph' | 'file' | 'fail' | 'distilled' | 'header'
    label       TEXT NOT NULL DEFAULT '', -- topic / path / symbol name
    est_tokens  INTEGER NOT NULL DEFAULT 0,
    useful_pct  INTEGER NOT NULL DEFAULT 0,  -- 0 until response feedback fills it
    ts          INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_receipts_ts ON token_receipts(ts);
CREATE INDEX IF NOT EXISTS idx_receipts_session ON token_receipts(session_id);
