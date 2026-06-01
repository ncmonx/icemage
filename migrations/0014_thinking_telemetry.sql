-- Phase 41 T4: thinking-budget telemetry per pack/agent invocation.
CREATE TABLE IF NOT EXISTS thinking_telemetry (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    cmd             TEXT NOT NULL,                      -- pack | agent | recall
    task            TEXT NOT NULL DEFAULT '',
    intent          TEXT NOT NULL DEFAULT 'unknown',    -- simple | complex | unknown
    no_think        INTEGER NOT NULL DEFAULT 0,         -- bool: directive applied
    concise         INTEGER NOT NULL DEFAULT 0,         -- bool: concise mode
    input_bytes     INTEGER NOT NULL DEFAULT 0,         -- est input size
    elapsed_ms      INTEGER NOT NULL DEFAULT 0,
    created_at      INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_think_telemetry_created
    ON thinking_telemetry(created_at);
CREATE INDEX IF NOT EXISTS idx_think_telemetry_intent
    ON thinking_telemetry(intent);
