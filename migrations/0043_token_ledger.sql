-- Phase: Gap #3 (2026-06-22) — real API token ledger.
-- `icmg savings` used to be a proxy estimate (filter/denial pipeline guesses).
-- This table holds the REAL per-response usage block every Anthropic API call
-- returns (input / output / cache-read / cache-creation tokens). The GUI
-- (icemage-code AgentLoop) flushes one row per turn via `icmg token-ledger
-- record`; savings reads it for an honest meter + cost.
CREATE TABLE IF NOT EXISTS token_ledger (
    id                    INTEGER PRIMARY KEY AUTOINCREMENT,
    ts                    INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    session_id            TEXT    NOT NULL DEFAULT '',
    model                 TEXT    NOT NULL DEFAULT '',
    source                TEXT    NOT NULL DEFAULT 'gui',  -- which app recorded it
    input_tokens          INTEGER NOT NULL DEFAULT 0,
    output_tokens         INTEGER NOT NULL DEFAULT 0,
    cache_read_tokens     INTEGER NOT NULL DEFAULT 0,
    cache_creation_tokens INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_token_ledger_ts ON token_ledger(ts);
CREATE INDEX IF NOT EXISTS idx_token_ledger_session ON token_ledger(session_id);
