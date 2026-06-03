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
}

void ProfileStore::put(const std::string& user, const std::string& zone, const std::string& key,
                       const std::string& kind, const std::string& content) {
    db_.run("INSERT INTO profile_entries(user_id,zone,key,kind,content,updated_at) "
            "VALUES(?,?,?,?,?,strftime('%s','now')) "
            "ON CONFLICT(user_id,zone,key) DO UPDATE SET "
            "kind=excluded.kind, content=excluded.content, updated_at=excluded.updated_at",
            {user, normalizeZone(zone), normalizeKey(key), validKind(kind), content});
}

bool ProfileStore::get(const std::string& user, const std::string& zone, const std::string& key,
                       std::string& content_out, std::string& kind_out) {
    bool found = false;
    db_.query("SELECT content, kind FROM profile_entries WHERE user_id=? AND zone=? AND key=?",
              {user, normalizeZone(zone), normalizeKey(key)},
              [&](const Row& r) { if (r.size() >= 2) { content_out = r[0]; kind_out = r[1]; found = true; } });
    return found;
}

std::vector<ProfileRow> ProfileStore::listZone(const std::string& user, const std::string& zone) {
    std::vector<ProfileRow> out;
    db_.query("SELECT zone,key,kind,content,updated_at FROM profile_entries "
              "WHERE user_id=? AND zone=? ORDER BY updated_at DESC",
              {user, normalizeZone(zone)},
              [&](const Row& r) {
                  if (r.size() >= 5) out.push_back({r[0], r[1], r[2], r[3], std::stoll(r[4])});
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
