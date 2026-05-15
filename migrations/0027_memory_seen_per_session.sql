-- v1.1.0 Task 4: diff-aware memory_recall.
-- Per-session tracking column lets `icmg recall --unseen` return only
-- entries that have not yet been served in the current session.
-- TEXT NULL → zero storage cost on rows that never use the feature.

ALTER TABLE memory_nodes ADD COLUMN last_returned_session TEXT;

CREATE INDEX IF NOT EXISTS idx_memory_last_returned_session
    ON memory_nodes(last_returned_session);
