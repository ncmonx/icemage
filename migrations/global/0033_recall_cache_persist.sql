-- v1.78.2: RAM-brain write-through + warm-reload persist layer.
-- Stores LRU recall cache entries on disk so daemon restart hydrates instantly
-- instead of cold-start recompute.
--
-- Scope = per-project via scope_hash column (xxh64 of project DB path).
-- Encryption-at-rest covered by SQLCipher (v1.76, default ON).
-- Idempotent via IF NOT EXISTS (anti v1.66 fresh-init dup-column class).

CREATE TABLE IF NOT EXISTS recall_cache_persist (
    scope_hash  TEXT    NOT NULL,
    key         TEXT    NOT NULL,
    value       BLOB    NOT NULL,
    hit_count   INTEGER NOT NULL DEFAULT 1,
    last_used   INTEGER NOT NULL,
    byte_size   INTEGER NOT NULL,
    PRIMARY KEY (scope_hash, key)
);

CREATE INDEX IF NOT EXISTS idx_rcp_scope_hits
    ON recall_cache_persist(scope_hash, hit_count DESC);
