-- Phase 47 T1: cached OCR results per image hash. 7d default TTL.
CREATE TABLE IF NOT EXISTS image_cache (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    image_hash      TEXT NOT NULL UNIQUE,
    bytes           INTEGER NOT NULL DEFAULT 0,
    ocr_text        TEXT NOT NULL DEFAULT '',
    ocr_confidence  INTEGER NOT NULL DEFAULT 0,
    classify_kind   TEXT NOT NULL DEFAULT 'unknown',
    hit_count       INTEGER NOT NULL DEFAULT 0,
    cached_at       INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    expires_at      INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_image_hash ON image_cache(image_hash);
CREATE INDEX IF NOT EXISTS idx_image_expires ON image_cache(expires_at);
