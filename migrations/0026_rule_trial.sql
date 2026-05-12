-- v0.42.1: rule trial/supersession lifecycle.
-- When a stricter rule supersedes an older one, it runs in "trial" mode.
-- After N prompts without user complaint, the old rule is auto-deleted.
ALTER TABLE rules ADD COLUMN supersedes_id   INTEGER;
ALTER TABLE rules ADD COLUMN trial_mode      INTEGER NOT NULL DEFAULT 0;
ALTER TABLE rules ADD COLUMN trial_prompts   INTEGER NOT NULL DEFAULT 0;
ALTER TABLE rules ADD COLUMN trial_threshold INTEGER NOT NULL DEFAULT 5;
CREATE INDEX IF NOT EXISTS idx_rules_trial ON rules(trial_mode) WHERE trial_mode=1;
