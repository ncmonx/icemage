#include "profile_store.hpp"
#include "profile_key.hpp"

namespace icmg::core {

ProfileStore::ProfileStore(Db& db) : db_(db) { ensure(); }

void ProfileStore::ensure() {
    db_.run("CREATE TABLE IF NOT EXISTS profile_entries("
            "user_id TEXT NOT NULL, zone TEXT NOT NULL, key TEXT NOT NULL,"
            "kind TEXT NOT NULL DEFAULT 'profile', content TEXT NOT NULL,"
            "updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
            "PRIMARY KEY(user_id, zone, key))");
    db_.run("CREATE INDEX IF NOT EXISTS ix_profile_zone ON profile_entries(user_id, zone)");
    // Provenance: add source column to legacy tables (guarded -- persona DB has no Migrator).
    bool hasSource = false;
    db_.query("PRAGMA table_info(profile_entries)", {},
              [&](const Row& r) { if (r.size() >= 2 && r[1] == "source") hasSource = true; });
    if (!hasSource)
        db_.run("ALTER TABLE profile_entries ADD COLUMN source TEXT NOT NULL DEFAULT 'unknown'");

    // FTS5 ranked search over content. External-content table keyed on profile_entries.rowid,
    // kept in sync by triggers. Guarded: persona DB has no Migrator, and SQLite may lack FTS5.
    try {
        bool ftsExisted = false;
        db_.query("SELECT 1 FROM sqlite_master WHERE type='table' AND name='profile_fts'", {},
                  [&](const Row&) { ftsExisted = true; });
        db_.run("CREATE VIRTUAL TABLE IF NOT EXISTS profile_fts USING fts5("
                "content, content='profile_entries', content_rowid='rowid')");
        db_.run("CREATE TRIGGER IF NOT EXISTS profile_fts_ai AFTER INSERT ON profile_entries BEGIN "
                "INSERT INTO profile_fts(rowid, content) VALUES(new.rowid, new.content); END");
        db_.run("CREATE TRIGGER IF NOT EXISTS profile_fts_ad AFTER DELETE ON profile_entries BEGIN "
                "INSERT INTO profile_fts(profile_fts, rowid, content) VALUES('delete', old.rowid, old.content); END");
        db_.run("CREATE TRIGGER IF NOT EXISTS profile_fts_au AFTER UPDATE ON profile_entries BEGIN "
                "INSERT INTO profile_fts(profile_fts, rowid, content) VALUES('delete', old.rowid, old.content); "
                "INSERT INTO profile_fts(rowid, content) VALUES(new.rowid, new.content); END");
        if (!ftsExisted)   // backfill rows written before the FTS table existed
            db_.run("INSERT INTO profile_fts(profile_fts) VALUES('rebuild')");
        ftsAvailable_ = true;
    } catch (const DbError&) {
        ftsAvailable_ = false;   // SQLite without FTS5 -> searchFts() degrades to LIKE
    }
}

void ProfileStore::put(const std::string& user, const std::string& zone, const std::string& key,
                       const std::string& kind, const std::string& content,
                       const std::string& source) {
    db_.run("INSERT INTO profile_entries(user_id,zone,key,kind,content,updated_at,source) "
            "VALUES(?,?,?,?,?,strftime('%s','now'),?) "
            "ON CONFLICT(user_id,zone,key) DO UPDATE SET "
            "kind=excluded.kind, content=excluded.content, updated_at=excluded.updated_at, source=excluded.source",
            {user, normalizeZone(zone), normalizeKey(key), validKind(kind), content,
             source.empty() ? std::string("unknown") : source});
}

bool ProfileStore::get(const std::string& user, const std::string& zone, const std::string& key,
                       std::string& content_out, std::string& kind_out) {
    bool found = false;
    db_.query("SELECT content, kind FROM profile_entries WHERE user_id=? AND zone=? AND key=?",
              {user, normalizeZone(zone), normalizeKey(key)},
              [&](const Row& r) { if (r.size() >= 2) { content_out = r[0]; kind_out = r[1]; found = true; } });
    return found;
}

bool ProfileStore::get(const std::string& user, const std::string& zone, const std::string& key,
                       std::string& content_out, std::string& kind_out, std::string& source_out) {
    bool found = false;
    db_.query("SELECT content, kind, source FROM profile_entries WHERE user_id=? AND zone=? AND key=?",
              {user, normalizeZone(zone), normalizeKey(key)},
              [&](const Row& r) { if (r.size() >= 3) { content_out = r[0]; kind_out = r[1]; source_out = r[2]; found = true; } });
    return found;
}

std::vector<ProfileRow> ProfileStore::listZone(const std::string& user, const std::string& zone) {
    std::vector<ProfileRow> out;
    db_.query("SELECT zone,key,kind,content,updated_at,source FROM profile_entries "
              "WHERE user_id=? AND zone=? ORDER BY updated_at DESC",
              {user, normalizeZone(zone)},
              [&](const Row& r) {
                  if (r.size() >= 6) out.push_back({r[0], r[1], r[2], r[3], std::stoll(r[4]), r[5]});
              });
    return out;
}

std::vector<ProfileRow> ProfileStore::search(const std::string& user, const std::string& query) {
    std::vector<ProfileRow> out;
    db_.query("SELECT zone,key,kind,content,updated_at FROM profile_entries "
              "WHERE user_id=? AND content LIKE ? ORDER BY updated_at DESC LIMIT 20",
              {user, "%" + query + "%"},
              [&](const Row& r) {
                  if (r.size() >= 5) out.push_back({r[0], r[1], r[2], r[3], std::stoll(r[4])});
              });
    return out;
}

std::vector<ProfileRow> ProfileStore::searchFts(const std::string& user, const std::string& query, int limit) {
    if (!ftsAvailable_ || query.empty()) return search(user, query);
    // Quote the whole query as an FTS5 phrase so operator chars (- " *) can't trigger a syntax error.
    std::string phrase = "\"";
    for (char c : query) { if (c == '"') phrase += '"'; phrase += c; }
    phrase += "\"";
    std::vector<ProfileRow> out;
    try {
        db_.query("SELECT e.zone,e.key,e.kind,e.content,e.updated_at,e.source "
                  "FROM profile_fts f JOIN profile_entries e ON e.rowid=f.rowid "
                  "WHERE profile_fts MATCH ? AND e.user_id=? "
                  "ORDER BY bm25(profile_fts) LIMIT ?",
                  {phrase, user, std::to_string(limit)},
                  [&](const Row& r) {
                      if (r.size() >= 6) out.push_back({r[0], r[1], r[2], r[3], std::stoll(r[4]), r[5]});
                  });
    } catch (const DbError&) {
        return search(user, query);   // malformed MATCH or runtime FTS error -> LIKE fallback
    }
    return out;
}

void ProfileStore::forget(const std::string& user, const std::string& zone, const std::string& key) {
    db_.run("DELETE FROM profile_entries WHERE user_id=? AND zone=? AND key=?",
            {user, normalizeZone(zone), normalizeKey(key)});
}

std::vector<std::pair<std::string,int>> ProfileStore::zoneCounts(const std::string& user) {
    std::vector<std::pair<std::string,int>> out;
    db_.query("SELECT zone, COUNT(*) FROM profile_entries WHERE user_id=? "
              "GROUP BY zone ORDER BY COUNT(*) DESC",
              {user},
              [&](const Row& r) {
                  if (r.size() >= 2) out.emplace_back(r[0], std::stoi(r[1]));
              });
    return out;
}

}  // namespace icmg::core
