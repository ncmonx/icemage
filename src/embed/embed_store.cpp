// Phase 23: embeddings table CRUD via raw sqlite BLOB binding.
#include "embed_store.hpp"
#include <sqlite3.h>
#include <cstring>

namespace icmg::embed {

void EmbedStore::put(int64_t node_id, const std::string& kind,
                     const std::vector<float>& vec, const std::string& model,
                     const std::string& body_hash) {
    auto* db = db_.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO embeddings(node_id,kind,vec,dim,model,body_hash,created_at) "
        "VALUES(?,?,?,?,?,?,strftime('%s','now')) "
        "ON CONFLICT(node_id,kind) DO UPDATE SET "
        "vec=excluded.vec,dim=excluded.dim,model=excluded.model,"
        "body_hash=excluded.body_hash,created_at=excluded.created_at";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    auto bytes = vec.size() * sizeof(float);
    sqlite3_bind_int64(stmt, 1, node_id);
    sqlite3_bind_text (stmt, 2, kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob (stmt, 3, vec.data(), (int)bytes, SQLITE_TRANSIENT);
    sqlite3_bind_int  (stmt, 4, (int)vec.size());
    sqlite3_bind_text (stmt, 5, model.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 6, body_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::vector<float> EmbedStore::get(int64_t node_id, const std::string& kind, int dim) {
    std::vector<float> out;
    auto* db = db_.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT vec FROM embeddings WHERE node_id=? AND kind=?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_int64(stmt, 1, node_id);
    sqlite3_bind_text (stmt, 2, kind.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(stmt, 0);
        int n = sqlite3_column_bytes(stmt, 0);
        if (blob && n >= (int)(dim * sizeof(float))) {
            out.resize(dim);
            std::memcpy(out.data(), blob, dim * sizeof(float));
        }
    }
    sqlite3_finalize(stmt);
    return out;
}

bool EmbedStore::fresh(int64_t node_id, const std::string& kind, const std::string& body_hash) {
    bool yes = false;
    db_.query("SELECT 1 FROM embeddings WHERE node_id=? AND kind=? AND body_hash=? LIMIT 1",
              {std::to_string(node_id), kind, body_hash},
              [&](const core::Row&) { yes = true; });
    return yes;
}

std::vector<std::pair<int64_t, std::vector<float>>>
EmbedStore::getMany(const std::string& kind, const std::vector<int64_t>& ids, int dim) {
    std::vector<std::pair<int64_t, std::vector<float>>> out;
    if (ids.empty()) return out;
    auto* db = db_.handle();
    std::string sql = "SELECT node_id, vec FROM embeddings WHERE kind=? AND node_id IN (";
    for (size_t i = 0; i < ids.size(); ++i) sql += (i ? ",?" : "?");
    sql += ")";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_text(stmt, 1, kind.c_str(), -1, SQLITE_TRANSIENT);
    for (size_t i = 0; i < ids.size(); ++i) {
        sqlite3_bind_int64(stmt, (int)(i + 2), ids[i]);
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int64_t id = sqlite3_column_int64(stmt, 0);
        const void* blob = sqlite3_column_blob(stmt, 1);
        int n = sqlite3_column_bytes(stmt, 1);
        if (blob && n >= (int)(dim * sizeof(float))) {
            std::vector<float> v(dim);
            std::memcpy(v.data(), blob, dim * sizeof(float));
            out.emplace_back(id, std::move(v));
        }
    }
    sqlite3_finalize(stmt);
    return out;
}

int EmbedStore::count(const std::string& kind) {
    int n = 0;
    db_.query("SELECT COUNT(*) FROM embeddings WHERE kind=?", {kind},
              [&](const core::Row& r) { if (!r.empty()) try { n = std::stoi(r[0]); } catch (...) {} });
    return n;
}

} // namespace icmg::embed
