-- v1.21.1 (FB1): user-facing feedback loop — record predicted vs actual
-- corrections so future similar prediction paths can search past mistakes.
-- Separate from `feedback` table (which is recall-reranking weight per node).
CREATE TABLE IF NOT EXISTS feedbacks (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    topic          TEXT NOT NULL,
    predicted      TEXT NOT NULL,
    actual         TEXT NOT NULL,
    note           TEXT,
    applied_count  INTEGER NOT NULL DEFAULT 0,
    created_at     INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_feedbacks_topic   ON feedbacks(topic);
CREATE INDEX IF NOT EXISTS idx_feedbacks_created ON feedbacks(created_at);
