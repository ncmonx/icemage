-- Phase: Gap #5 (2026-06-22) — query_history gains a token metric.
-- memory-history was double-logged with zero metrics. Dedup happens at read
-- time (queryHistoryDetailed); this column records the estimated token size of
-- each recall's result set so the history is actually a meter, not just text.
ALTER TABLE query_history ADD COLUMN tokens INTEGER NOT NULL DEFAULT 0;
