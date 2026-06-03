-- migrations/0040_working_set_snapshot.sql
-- v2.0.0 C4: per-session working-set manifest captured at PreCompact, rebuilt at
-- PostCompact (pinned-only, hard-capped). Project DB.
CREATE TABLE IF NOT EXISTS working_set_snapshot (
    session_id    TEXT    NOT NULL,
    ts            INTEGER NOT NULL,
    manifest_json TEXT    NOT NULL,
    pinned_json   TEXT    NOT NULL,
    PRIMARY KEY (session_id, ts)
);
CREATE INDEX IF NOT EXISTS idx_wss_session ON working_set_snapshot(session_id);
