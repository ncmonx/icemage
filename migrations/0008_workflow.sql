-- Phase 22: workflow integration tables
-- verifications: audit trail of test/lint/check runs (icmg verify --record)
-- phases: GSD-style phase lifecycle (icmg phase start/verify/ship)
-- designs: brainstorming gate registry (icmg design check)

CREATE TABLE IF NOT EXISTS verifications (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    phase       TEXT,
    command     TEXT NOT NULL,
    exit_code   INTEGER NOT NULL,
    output_hash TEXT,
    output_head TEXT,        -- first 1KB of output for evidence
    duration_ms INTEGER,
    recorded_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_verif_phase ON verifications(phase);
CREATE INDEX IF NOT EXISTS idx_verif_recorded ON verifications(recorded_at);

CREATE TABLE IF NOT EXISTS phases (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    num          TEXT NOT NULL UNIQUE,
    name         TEXT NOT NULL,
    goal         TEXT,
    plan_path    TEXT,
    status       TEXT NOT NULL DEFAULT 'pending',  -- pending|in-progress|done|blocked
    started_at   INTEGER,
    completed_at INTEGER,
    commit_sha   TEXT,
    notes        TEXT
);

CREATE TABLE IF NOT EXISTS designs (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    feature      TEXT NOT NULL UNIQUE,
    doc_path     TEXT,
    status       TEXT NOT NULL DEFAULT 'draft',   -- draft|approved|rejected
    approved_at  INTEGER,
    approved_by  TEXT,
    notes        TEXT
);
