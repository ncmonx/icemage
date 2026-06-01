CREATE TABLE IF NOT EXISTS token_counts (
    path        TEXT PRIMARY KEY,
    tokens      INTEGER NOT NULL,
    bytes       INTEGER NOT NULL,
    mtime       INTEGER NOT NULL,
    computed_at INTEGER DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_token_counts_mtime ON token_counts(mtime);
