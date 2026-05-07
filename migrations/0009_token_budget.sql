-- Phase 20: token-budget tracking.
-- Each tool invocation logs estimated input/output token usage so `icmg budget`
-- can show where tokens go and what filtering already saved.

CREATE TABLE IF NOT EXISTS tool_invocations (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp       INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    tool_name       TEXT    NOT NULL,
    command         TEXT,
    raw_bytes       INTEGER,    -- pre-filter
    filtered_bytes  INTEGER,    -- post-filter (0 if not applicable)
    est_tokens_in   INTEGER,
    est_tokens_out  INTEGER,
    saved_tokens    INTEGER     -- raw_tokens - filtered_tokens
);

CREATE INDEX IF NOT EXISTS idx_tinv_ts   ON tool_invocations(timestamp);
CREATE INDEX IF NOT EXISTS idx_tinv_tool ON tool_invocations(tool_name);
