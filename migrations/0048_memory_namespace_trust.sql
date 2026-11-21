-- Migration 0048 (#292): memory namespaces + trust level.
-- namespace = tenant scope for one service process serving many callers
--             ('user:12', 'team:finance', 'agent:<id>'); '' = shared (the old
--             behaviour). Recall with --ns sees ONLY the namespaces passed.
-- trust     = 'trusted' | 'untrusted'. Rows from web pages / files are
--             untrusted and recall marks them. '' (old rows) = trusted.

ALTER TABLE memory_nodes ADD COLUMN namespace TEXT NOT NULL DEFAULT '';
ALTER TABLE memory_nodes ADD COLUMN trust     TEXT NOT NULL DEFAULT '';
CREATE INDEX IF NOT EXISTS idx_memory_namespace ON memory_nodes(namespace);
