-- Phase 05: RTK token budget columns on commands table
-- Migration version: 3

ALTER TABLE commands ADD COLUMN total_original_lines INTEGER NOT NULL DEFAULT 0;
ALTER TABLE commands ADD COLUMN total_filtered_lines INTEGER NOT NULL DEFAULT 0;

-- Remove old avg_lines column is not supported by SQLite ALTER TABLE DROP COLUMN
-- (added in SQLite 3.35.0). We just keep it as a dead column. It defaults to 0.

CREATE INDEX IF NOT EXISTS idx_commands_command   ON commands(command);
CREATE INDEX IF NOT EXISTS idx_commands_last_used ON commands(last_used);
CREATE INDEX IF NOT EXISTS idx_commands_frequency ON commands(frequency DESC);
