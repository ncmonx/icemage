-- v1.37.0 active drift correction. Stop hook scans AI response for
-- contradiction vs decisions table; logs event + emits correction in
-- next UserPromptSubmit additionalContext.
CREATE TABLE IF NOT EXISTS drift_corrections (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id    TEXT NOT NULL DEFAULT '',
    decision_id   INTEGER NOT NULL,
    stance        TEXT NOT NULL,
    contradicted_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    emitted       INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_drift_session ON drift_corrections(session_id);
CREATE INDEX IF NOT EXISTS idx_drift_emitted ON drift_corrections(emitted);
