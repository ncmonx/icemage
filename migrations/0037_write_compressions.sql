-- v1.25.0 (W4): compressed-write telemetry.
--
-- Per `port apply` records bytes saved vs hypothetical naive emission so
-- `icmg savings --layer write` can quantify the cost reduction. Failure
-- rows (parse error, SHA mismatch, etc.) are also recorded with ok=0 so
-- we can spot AI compliance drift.

CREATE TABLE IF NOT EXISTS write_compressions (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    mode             TEXT NOT NULL,                 -- diff|template|glossary|raw
    base_path        TEXT NOT NULL,
    bytes_compressed INTEGER NOT NULL DEFAULT 0,    -- AI-emitted size
    bytes_expanded   INTEGER NOT NULL DEFAULT 0,    -- final on-disk size
    ok               INTEGER NOT NULL DEFAULT 1,    -- 1 = expand succeeded
    err              TEXT,                          -- non-NULL when ok=0
    created_at       INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);

CREATE INDEX IF NOT EXISTS idx_write_compressions_created ON write_compressions(created_at);
CREATE INDEX IF NOT EXISTS idx_write_compressions_mode    ON write_compressions(mode);
