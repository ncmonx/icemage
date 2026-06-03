#include "prompt_history.hpp"
#include "profile_key.hpp"   // reuse slugify for the normalized prompt key

namespace icmg::core {

PromptHistory::PromptHistory(Db& db) : db_(db) { ensure(); }

void PromptHistory::ensure() {
    db_.run("CREATE TABLE IF NOT EXISTS prompt_history("
            "user_id TEXT NOT NULL, zone TEXT NOT NULL, prompt TEXT NOT NULL,"
            "response TEXT NOT NULL, prompt_key TEXT NOT NULL,"
            "created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
            "PRIMARY KEY(user_id, zone, prompt_key))");
    db_.run("CREATE INDEX IF NOT EXISTS ix_ph_zone ON prompt_history(user_id, zone)");
}

void PromptHistory::record(const std::string& user, const std::string& zone,
                           const std::string& prompt, const std::string& response) {
    db_.run("INSERT INTO prompt_history(user_id,zone,prompt,response,prompt_key,created_at) "
            "VALUES(?,?,?,?,?,strftime('%s','now')) "
            "ON CONFLICT(user_id,zone,prompt_key) DO UPDATE SET "
            "prompt=excluded.prompt, response=excluded.response, created_at=excluded.created_at",
            {user, normalizeZone(zone), prompt, response, slugify(prompt)});
}

bool PromptHistory::recallExact(const std::string& user, const std::string& zone,
                                const std::string& prompt, std::string& response_out) {
    bool found = false;
    db_.query("SELECT response FROM prompt_history WHERE user_id=? AND zone=? AND prompt_key=?",
              {user, normalizeZone(zone), slugify(prompt)},
              [&](const Row& r) { if (!r.empty()) { response_out = r[0]; found = true; } });
    return found;
}

std::vector<QARow> PromptHistory::findSimilar(const std::string& user, const std::string& prompt,
                                              int limit) {
    // Lexical: OR'd LIKE over prompt terms (slug split on '-', terms len>=3). Simple +
    // deterministic; an FTS5 MATCH path can replace this later without API change.
    std::vector<QARow> out;
    const std::string slug = slugify(prompt);
    std::string where;
    std::vector<std::string> params{user};
    size_t pos = 0;
    while (pos <= slug.size()) {
        size_t dash = slug.find('-', pos);
        std::string tok = slug.substr(pos, dash == std::string::npos ? std::string::npos : dash - pos);
        if (tok.size() >= 3) {
            if (!where.empty()) where += " OR ";
            where += "prompt LIKE ?";
            params.push_back("%" + tok + "%");
        }
        if (dash == std::string::npos) break;
        pos = dash + 1;
    }
    if (where.empty()) return out;  // no usable terms
    std::string sql = "SELECT zone,prompt,response,created_at FROM prompt_history "
                      "WHERE user_id=? AND (" + where + ") ORDER BY created_at DESC LIMIT " +
                      std::to_string(limit);
    db_.query(sql, params, [&](const Row& r) {
        if (r.size() >= 4) out.push_back({r[0], r[1], r[2], std::stoll(r[3])});
    });
    return out;
}

}  // namespace icmg::core
