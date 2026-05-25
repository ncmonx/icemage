-- v1.41.0 per-user persona storage. Multi-user single-server: each user
-- keeps own persona preference. Used by chat/agent/ask paths as system-
-- prompt prefix. Storage only — model still enforces own content policies.
CREATE TABLE IF NOT EXISTS user_personas (
    user_id     TEXT PRIMARY KEY,
    persona     TEXT NOT NULL DEFAULT '',
    traits      TEXT NOT NULL DEFAULT '',
    created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    updated_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS ix_user_personas_updated ON user_personas(updated_at DESC);
