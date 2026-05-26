// v1.48.0: chat persistence impl. Uses GlobalDb singleton.
#include "chat_persistence.hpp"
#include "../core/global_db.hpp"
#include "../core/db.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace icmg::llm {

namespace {

std::int64_t nowSec() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}


// v1.48.0: ensure schema present. Migrator stops manual at v6; downstream
// modules bootstrap their own tables via CREATE TABLE IF NOT EXISTS at
// first-use. Idempotent.
static bool s_schema_ready = false;
static void ensureSchema(core::Db& d) {
    if (s_schema_ready) return;
    try {
        d.run("CREATE TABLE IF NOT EXISTS local_llm_chats ("
              "id INTEGER PRIMARY KEY AUTOINCREMENT, "
              "user_id TEXT NOT NULL, session_id TEXT NOT NULL, "
              "ts INTEGER NOT NULL DEFAULT (strftime('%s','now')), "
              "role TEXT NOT NULL CHECK(role IN ('user','assistant','system')), "
              "content TEXT NOT NULL, model_id TEXT, "
              "tokens_in INTEGER DEFAULT 0, tokens_out INTEGER DEFAULT 0)");
        d.run("CREATE INDEX IF NOT EXISTS ix_local_llm_chats_session "
              "ON local_llm_chats(user_id, session_id, ts)");
        d.run("CREATE INDEX IF NOT EXISTS ix_local_llm_chats_ts "
              "ON local_llm_chats(user_id, ts DESC)");
        d.run("CREATE VIRTUAL TABLE IF NOT EXISTS local_llm_chats_fts USING fts5("
              "content, content='local_llm_chats', content_rowid='id')");
        d.run("CREATE TRIGGER IF NOT EXISTS local_llm_chats_ai "
              "AFTER INSERT ON local_llm_chats BEGIN "
              "INSERT INTO local_llm_chats_fts(rowid, content) "
              "VALUES (new.id, new.content); END");
        d.run("CREATE TRIGGER IF NOT EXISTS local_llm_chats_ad "
              "AFTER DELETE ON local_llm_chats BEGIN "
              "INSERT INTO local_llm_chats_fts(local_llm_chats_fts, rowid, content) "
              "VALUES('delete', old.id, old.content); END");
        s_schema_ready = true;
    } catch (...) {}
}

core::Db& gdb() {
    auto& g = core::GlobalDb::instance();
    g.init();
    ensureSchema(g.db());
    return g.db();
}

} // namespace

bool appendChatTurn(const std::string& user_id,
                    const std::string& session_id,
                    const std::string& role,
                    const std::string& content,
                    const std::string& model_id,
                    int tokens_in,
                    int tokens_out) {
    try {
        auto& d = gdb();
        d.run(
            "INSERT INTO local_llm_chats "
            "(user_id, session_id, ts, role, content, model_id, tokens_in, tokens_out) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            {user_id, session_id, std::to_string(nowSec()), role, content,
             model_id, std::to_string(tokens_in), std::to_string(tokens_out)});
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<std::pair<std::string, std::string>>
loadSessionHistory(const std::string& user_id,
                   const std::string& session_id,
                   int max_turns) {
    std::vector<std::pair<std::string, std::string>> out;
    try {
        gdb().query(
            "SELECT role, content FROM local_llm_chats "
            "WHERE user_id=? AND session_id=? "
            "ORDER BY ts ASC, id ASC LIMIT ?",
            {user_id, session_id, std::to_string(max_turns * 2)},
            [&](const core::Row& r) {
                if (r.size() >= 2) out.emplace_back(r[0], r[1]);
            });
    } catch (...) {}
    return out;
}

std::vector<std::pair<std::string, std::string>>
loadRecentTurns(const std::string& user_id, int max_turns) {
    std::vector<std::pair<std::string, std::string>> out;
    try {
        // Pull most recent rows DESC, reverse to ascending after.
        gdb().query(
            "SELECT role, content FROM local_llm_chats "
            "WHERE user_id=? ORDER BY ts DESC, id DESC LIMIT ?",
            {user_id, std::to_string(max_turns * 2)},
            [&](const core::Row& r) {
                if (r.size() >= 2) out.emplace_back(r[0], r[1]);
            });
        // Reverse so chronological ascending for ChatML.
        std::reverse(out.begin(), out.end());
    } catch (...) {}
    return out;
}

std::vector<ChatSession>
listRecentSessions(const std::string& user_id, int limit) {
    std::vector<ChatSession> out;
    try {
        gdb().query(
            "SELECT s.session_id, MAX(s.ts) AS last_ts, COUNT(*) AS n, "
            "       COALESCE((SELECT content FROM local_llm_chats "
            "                 WHERE user_id=s.user_id AND session_id=s.session_id "
            "                   AND role='user' "
            "                 ORDER BY ts ASC, id ASC LIMIT 1), '') AS preview "
            "FROM   local_llm_chats s "
            "WHERE  s.user_id=? "
            "GROUP  BY s.session_id "
            "ORDER  BY last_ts DESC "
            "LIMIT  ?",
            {user_id, std::to_string(limit)},
            [&](const core::Row& r) {
                if (r.size() < 4) return;
                ChatSession cs;
                cs.session_id = r[0];
                try { cs.ts = std::stoll(r[1]); } catch (...) {}
                try { cs.turn_count = std::stoi(r[2]); } catch (...) {}
                cs.preview = r[3].substr(0, 80);
                out.push_back(std::move(cs));
            });
    } catch (...) {}
    return out;
}

std::vector<ChatRecall>
bm25RecallChats(const std::string& user_id,
                const std::string& query,
                int top_k) {
    std::vector<ChatRecall> out;
    if (query.empty()) return out;
    try {
        // Escape double-quotes to avoid FTS5 syntax errors.
        std::string q;
        q.reserve(query.size() + 2);
        q += '"';
        for (char c : query) { if (c == '"') q += ' '; else q += c; }
        q += '"';

        gdb().query(
            "SELECT c.session_id, c.ts, c.content, bm25(local_llm_chats_fts) AS s "
            "FROM   local_llm_chats_fts f "
            "JOIN   local_llm_chats     c ON c.id = f.rowid "
            "WHERE  c.user_id=? AND local_llm_chats_fts MATCH ? "
            "ORDER  BY s ASC "
            "LIMIT  ?",
            {user_id, q, std::to_string(top_k)},
            [&](const core::Row& r) {
                if (r.size() < 4) return;
                ChatRecall cr;
                cr.session_id = r[0];
                try { cr.ts = std::stoll(r[1]); } catch (...) {}
                cr.content = r[2];
                try { cr.score = std::stod(r[3]); } catch (...) {}
                out.push_back(std::move(cr));
            });
    } catch (...) {}
    return out;
}

bool clearSession(const std::string& user_id, const std::string& session_id) {
    try {
        gdb().run(
            "DELETE FROM local_llm_chats WHERE user_id=? AND session_id=?",
            {user_id, session_id});
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace icmg::llm
