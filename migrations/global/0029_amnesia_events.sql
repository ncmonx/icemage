-- v1.37.0 A7: amnesia counter. Stop hook detects AI re-ask of decided
-- topics → logs event. Next UserPromptSubmit injects warning + prior answer.
CREATE TABLE IF NOT EXISTS amnesia_events (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id  TEXT NOT NULL DEFAULT '',
    topic       TEXT NOT NULL,
    prior_node  INTEGER,
    matched_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_amnesia_topic   ON amnesia_events(topic);
CREATE INDEX IF NOT EXISTS idx_amnesia_session ON amnesia_events(session_id);
