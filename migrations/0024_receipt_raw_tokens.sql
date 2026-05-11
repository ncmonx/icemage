-- Add raw_tokens to token_receipts so savings_cmd can compute pack savings.
-- raw_tokens = estimated tokens WITHOUT pack filtering/scoring (the baseline).
-- est_tokens = what pack actually emitted. saved = raw - est.
ALTER TABLE token_receipts ADD COLUMN raw_tokens INTEGER NOT NULL DEFAULT 0;
