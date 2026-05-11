-- Phase 15 gap: git SHA tagging on memory nodes.
-- Captures the git commit SHA at store time so memories are traceable to code state.
ALTER TABLE memory_nodes ADD COLUMN git_sha TEXT NOT NULL DEFAULT '';
CREATE INDEX IF NOT EXISTS idx_memory_nodes_git_sha ON memory_nodes(git_sha);
