-- v1.37.0 C2: intent cache (regex-immediate + LLM-backfill async).
-- Hot path (PreToolUse/UserPromptSubmit) does PRIMARY KEY lookup <0.5ms.
-- Miss = regex classify + queue for warm LLM upgrade.
CREATE TABLE IF NOT EXISTS intent_cache (
    prompt_hash TEXT PRIMARY KEY,
    intent      TEXT NOT NULL,
    source      TEXT NOT NULL,
    created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    updated_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE TABLE IF NOT EXISTS intent_backfill_queue (
    prompt_hash TEXT PRIMARY KEY,
    prompt_text TEXT NOT NULL,
    queued_at   INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_intent_updated ON intent_cache(updated_at);
