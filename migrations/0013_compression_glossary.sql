-- Phase 39 T1: cached compression glossary per content hash.
-- Lossless reverse mapping (alias → original phrase) for `icmg compress`/`expand`.
CREATE TABLE IF NOT EXISTS compression_glossary (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    content_hash  TEXT NOT NULL,           -- FNV1a 64-bit of source text
    alias         TEXT NOT NULL,           -- e.g. @P1, $I3
    original      TEXT NOT NULL,           -- expanded phrase
    freq          INTEGER NOT NULL DEFAULT 1,
    created_at    INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_compress_hash_alias
    ON compression_glossary(content_hash, alias);
CREATE INDEX IF NOT EXISTS idx_compress_hash
    ON compression_glossary(content_hash);

-- Telemetry: per-call compression result for ROI tracking.
CREATE TABLE IF NOT EXISTS compression_telemetry (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    cmd           TEXT NOT NULL,           -- 'compress', 'pack', 'context', 'diff-summary'
    bytes_in      INTEGER NOT NULL,
    bytes_out     INTEGER NOT NULL,
    tok_in        INTEGER NOT NULL,
    tok_out       INTEGER NOT NULL,
    elapsed_ms    INTEGER NOT NULL,
    mode          TEXT NOT NULL DEFAULT 'lossless', -- lossless | aggressive | skipped
    created_at    INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_compress_telemetry_created
    ON compression_telemetry(created_at);
