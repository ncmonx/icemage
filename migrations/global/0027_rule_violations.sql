-- v1.35.0 R4: rule violation telemetry table.
-- Counts hook-level rule violations per (rule_id, session_id) so the
-- enforcement layer can escalate often-violated rules and auto-pin
-- them to the UserPromptSubmit header (R8).
CREATE TABLE IF NOT EXISTS rule_violations (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    rule_id     TEXT    NOT NULL,
    session_id  TEXT    NOT NULL DEFAULT '',
    ctx         TEXT    NOT NULL DEFAULT '',
    occurred_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_rule_viol_rule    ON rule_violations(rule_id);
CREATE INDEX IF NOT EXISTS idx_rule_viol_session ON rule_violations(rule_id, session_id);
CREATE INDEX IF NOT EXISTS idx_rule_viol_when    ON rule_violations(occurred_at);
