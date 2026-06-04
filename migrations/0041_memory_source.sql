-- v2.x provenance (Lapis 1): memory_nodes carries a free-text source (who/what
-- supplied the info: 'user' / 'hook-auto' / 'ai-inference' / 'web:url' / ...).
-- Default 'unknown' (backward-compat; existing rows read the default).
-- Source is metadata only -- NOT indexed into keywords/FTS, ranking unaffected.
-- (MemoryStore ctor also guarded-ALTERs this for DBs that skip the migrator.)
ALTER TABLE memory_nodes ADD COLUMN source TEXT NOT NULL DEFAULT 'unknown';
