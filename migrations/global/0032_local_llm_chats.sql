-- v1.48.0: persistent local-LLM chat history. Stored in global.db (cross-
-- project). MUST NOT be merged into project memory_nodes — user constraint.
-- Each turn appends one row; chat_cmd reads recent rows on REPL start to
-- seed history, then appends after each successful infer.
CREATE TABLE IF NOT EXISTS local_llm_chats (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    TEXT NOT NULL,
    session_id TEXT NOT NULL,
    ts         INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    role       TEXT NOT NULL CHECK(role IN ('user','assistant','system')),
    content    TEXT NOT NULL,
    model_id   TEXT,
    tokens_in  INTEGER DEFAULT 0,
    tokens_out INTEGER DEFAULT 0
);
CREATE INDEX IF NOT EXISTS ix_local_llm_chats_session
    ON local_llm_chats(user_id, session_id, ts);
CREATE INDEX IF NOT EXISTS ix_local_llm_chats_ts
    ON local_llm_chats(user_id, ts DESC);

-- FTS5 virtual table for BM25 recall over past chat content (cross-session,
-- per-user). Maintained via triggers on local_llm_chats.
CREATE VIRTUAL TABLE IF NOT EXISTS local_llm_chats_fts USING fts5(
    content,
    content='local_llm_chats',
    content_rowid='id'
);

CREATE TRIGGER IF NOT EXISTS local_llm_chats_ai
    AFTER INSERT ON local_llm_chats BEGIN
    INSERT INTO local_llm_chats_fts(rowid, content)
    VALUES (new.id, new.content);
END;

CREATE TRIGGER IF NOT EXISTS local_llm_chats_ad
    AFTER DELETE ON local_llm_chats BEGIN
    INSERT INTO local_llm_chats_fts(local_llm_chats_fts, rowid, content)
    VALUES('delete', old.id, old.content);
END;
