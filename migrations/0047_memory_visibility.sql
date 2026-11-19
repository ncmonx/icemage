-- Migration 0047 (#288): per-author memory visibility.
-- author     = OS account that wrote the row (Windows SID / POSIX uid:<n>).
-- visibility = 'private' (author only) or 'team' (everyone).
-- Existing rows get author '' and visibility 'team' = the old shared behaviour.

ALTER TABLE memory_nodes ADD COLUMN author     TEXT NOT NULL DEFAULT '';
ALTER TABLE memory_nodes ADD COLUMN visibility TEXT NOT NULL DEFAULT 'team';
CREATE INDEX IF NOT EXISTS idx_memory_visibility ON memory_nodes(visibility, author);
