-- Migration 0046: Delta output snapshot for icmg run
-- Adds two columns to the commands table to store the last filtered output
-- and its hash, enabling delta-only display on repeated runs.
--
-- Semantics: REPLACE (not accumulate) -- only latest run snapshot is kept.
-- Size cap enforced in C++ (max 64 KB stored; hash-only path for larger output).

ALTER TABLE commands ADD COLUMN last_filtered_output TEXT;
ALTER TABLE commands ADD COLUMN last_filtered_hash   TEXT;
