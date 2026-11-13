#include "ai_ref_store.hpp"

#include "../cli/ref_flow.hpp"
#include "../core/cross_turn_dedup.hpp"
#include "../core/db.hpp"
#include "../core/global_db.hpp"
#include "../core/persona_db.hpp"

#include <algorithm>
#include <ctime>

namespace icmg::imem {

namespace {

constexpr const char* kCols =
    "id,problem,solution,project,source_ref,verified_by,created_at,used_count,last_used";

long long toLL(const std::string& s) {
    try { return std::stoll(s); } catch (...) { return 0; }
}

AiRef rowToRef(const core::Row& r) {
    AiRef a;
    if (r.size() < 9) return a;
    a.id = toLL(r[0]);
    a.problem = r[1];
    a.solution = r[2];
    a.project = r[3];
    a.source_ref = r[4];
    a.verified_by = r[5];
    a.created_at = toLL(r[6]);
    a.used_count = (int)toLL(r[7]);
    a.last_used = toLL(r[8]);
    return a;
}

// FTS5 query: each word quoted, OR-joined -> operator chars can never parse.
std::string ftsQuery(const std::string& q) {
    std::string out;
    for (const auto& w : core::wordSet(q)) {
        if (!out.empty()) out += " OR ";
        out += "\"" + w + "\"";
    }
    return out;
}

long long nowSec() { return (long long)std::time(nullptr); }

}  // namespace

AiRefStore::AiRefStore(core::Db& db) : db_(db) { ensure(); }

void AiRefStore::ensure() {
    db_.run("CREATE TABLE IF NOT EXISTS ai_refs("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "problem TEXT NOT NULL,"
            "solution TEXT NOT NULL DEFAULT '',"
            "project TEXT NOT NULL DEFAULT '',"
            "source_ref TEXT NOT NULL DEFAULT '',"
            "verified_by TEXT NOT NULL DEFAULT '',"
            "created_at INTEGER NOT NULL,"
            "used_count INTEGER NOT NULL DEFAULT 0,"
            "last_used INTEGER NOT NULL DEFAULT 0)");
    try {
        db_.run("CREATE VIRTUAL TABLE IF NOT EXISTS ai_refs_fts USING fts5("
                "problem, solution, content='ai_refs', content_rowid='id')");
        db_.run("CREATE TRIGGER IF NOT EXISTS ai_refs_ai AFTER INSERT ON ai_refs BEGIN "
                "INSERT INTO ai_refs_fts(rowid, problem, solution) "
                "VALUES(new.id, new.problem, new.solution); END");
        db_.run("CREATE TRIGGER IF NOT EXISTS ai_refs_ad AFTER DELETE ON ai_refs BEGIN "
                "INSERT INTO ai_refs_fts(ai_refs_fts, rowid, problem, solution) "
                "VALUES('delete', old.id, old.problem, old.solution); END");
        db_.run("CREATE TRIGGER IF NOT EXISTS ai_refs_au AFTER UPDATE OF problem, solution ON ai_refs BEGIN "
                "INSERT INTO ai_refs_fts(ai_refs_fts, rowid, problem, solution) "
                "VALUES('delete', old.id, old.problem, old.solution); "
                "INSERT INTO ai_refs_fts(rowid, problem, solution) "
                "VALUES(new.id, new.problem, new.solution); END");
        fts_ = true;
    } catch (const core::DbError&) {
        fts_ = false;   // SQLite built without FTS5 -> LIKE fallback
    }
}

long long AiRefStore::findDuplicate(const std::string& problem) {
    const auto mine = core::wordSet(problem);
    if (mine.empty()) return 0;
    for (const auto& r : search(problem, 10)) {
        if (core::jaccardSets(mine, core::wordSet(r.problem)) >= cli::refflow::kRefDedupJaccard)
            return r.id;
    }
    return 0;
}

bool AiRefStore::isDuplicate(const std::string& problem) {
    return findDuplicate(problem) != 0;
}

long long AiRefStore::add(const AiRefInput& in) {
    if (core::wordSet(in.problem).empty()) return 0;
    if (long long dup = findDuplicate(in.problem)) return dup;
    db_.run("INSERT INTO ai_refs(problem,solution,project,source_ref,verified_by,created_at) "
            "VALUES(?,?,?,?,?,?)",
            {in.problem, in.solution, in.project, in.source_ref, in.verified_by,
             std::to_string(nowSec())});
    return db_.lastInsertId();
}

std::vector<AiRef> AiRefStore::search(const std::string& query, int limit) {
    std::vector<AiRef> out;
    if (limit <= 0) return out;
    const std::string fq = ftsQuery(query);
    if (fq.empty()) return out;
    if (fts_) {
        try {
            db_.query(std::string("SELECT ") +
                      "r.id,r.problem,r.solution,r.project,r.source_ref,r.verified_by,"
                      "r.created_at,r.used_count,r.last_used,-bm25(ai_refs_fts) "
                      "FROM ai_refs_fts f JOIN ai_refs r ON r.id=f.rowid "
                      "WHERE ai_refs_fts MATCH ? ORDER BY bm25(ai_refs_fts) LIMIT ?",
                      {fq, std::to_string(limit)},
                      [&](const core::Row& r) {
                          AiRef a = rowToRef(r);
                          if (r.size() >= 10) {
                              try { a.score = std::stod(r[9]); } catch (...) {}
                          }
                          out.push_back(a);
                      });
            return out;
        } catch (const core::DbError&) {
            out.clear();   // fall through to LIKE
        }
    }
    // LIKE fallback: score = matched-word count.
    const auto words = core::wordSet(query);
    db_.query(std::string("SELECT ") + kCols + " FROM ai_refs ORDER BY id DESC LIMIT 500", {},
              [&](const core::Row& r) {
                  AiRef a = rowToRef(r);
                  const auto doc = core::wordSet(a.problem + " " + a.solution);
                  int hit = 0;
                  for (const auto& w : words) if (doc.count(w)) ++hit;
                  if (hit > 0) { a.score = hit; out.push_back(a); }
              });
    std::stable_sort(out.begin(), out.end(),
                     [](const AiRef& x, const AiRef& y) { return x.score > y.score; });
    if ((int)out.size() > limit) out.resize(limit);
    return out;
}

bool AiRefStore::get(long long id, AiRef& out) {
    bool found = false;
    db_.query(std::string("SELECT ") + kCols + " FROM ai_refs WHERE id=?",
              {std::to_string(id)},
              [&](const core::Row& r) { out = rowToRef(r); found = true; });
    return found;
}

void AiRefStore::markUsed(long long id) {
    db_.run("UPDATE ai_refs SET used_count=used_count+1, last_used=? WHERE id=?",
            {std::to_string(nowSec()), std::to_string(id)});
}

bool AiRefStore::remove(long long id) {
    AiRef r;
    if (!get(id, r)) return false;
    db_.run("DELETE FROM ai_refs WHERE id=?", {std::to_string(id)});   // ai_refs_ad syncs FTS
    return true;
}

int AiRefStore::count() {
    int n = 0;
    db_.query("SELECT COUNT(*) FROM ai_refs", {},
              [&](const core::Row& r) { if (!r.empty()) n = (int)toLL(r[0]); });
    return n;
}

core::Db& modelAiRefDb() {
    if (core::personaDbAvailable()) return core::personaDb();
    return core::GlobalDb::instance().db();
}

}  // namespace icmg::imem
