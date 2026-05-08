-- Phase 47 T4: multi-user MVP — tag rows with creator identity.
-- Empty string = legacy/shared (backward compat).
ALTER TABLE memory_nodes ADD COLUMN created_by TEXT NOT NULL DEFAULT '';
CREATE INDEX IF NOT EXISTS idx_memory_user ON memory_nodes(created_by);

-- Optimistic locking: incremented on update; reject stale writes (T7).
ALTER TABLE memory_nodes ADD COLUMN row_version INTEGER NOT NULL DEFAULT 0;
