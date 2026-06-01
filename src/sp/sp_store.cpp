#include "sp_store.hpp"
#include <chrono>
#include <sstream>
#include <algorithm>

namespace icmg::sp {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int64_t nowEpoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// Minimal JSON array serializer (no special-char escaping needed for table/SP names)
std::string SpStore::jsonArray(const std::vector<std::string>& v) {
    std::string out = "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += ",";
        out += "\"";
        for (char c : v[i]) {
            if (c == '"')  out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else           out += c;
        }
        out += "\"";
    }
    return out + "]";
}

// Minimal JSON array parser (handles ["a","b","c"])
std::vector<std::string> SpStore::parseJsonArray(const std::string& json) {
    std::vector<std::string> out;
    if (json.empty() || json == "[]") return out;
    size_t i = 0, n = json.size();
    while (i < n) {
        size_t q = json.find('"', i);
        if (q == std::string::npos) break;
        size_t e = json.find('"', q + 1);
        while (e != std::string::npos && json[e - 1] == '\\') e = json.find('"', e + 1);
        if (e == std::string::npos) break;
        out.push_back(json.substr(q + 1, e - q - 1));
        i = e + 1;
    }
    return out;
}

std::string SpStore::jsonParams(const std::vector<SpParameter>& v) {
    std::string out = "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += ",";
        auto esc = [](const std::string& s) {
            std::string r;
            for (char c : s) {
                if (c == '"') r += "\\\"";
                else r += c;
            }
            return r;
        };
        out += "{\"name\":\"" + esc(v[i].name) + "\""
             + ",\"type\":\"" + esc(v[i].type) + "\""
             + ",\"direction\":\"" + esc(v[i].direction) + "\""
             + ",\"default\":\"" + esc(v[i].default_val) + "\"}";
    }
    return out + "]";
}

std::vector<SpParameter> SpStore::parseJsonParams(const std::string& json) {
    // Simple key-value extraction; not a full JSON parser
    std::vector<SpParameter> out;
    if (json.empty() || json == "[]") return out;

    auto extractField = [&](const std::string& blob, const std::string& key) -> std::string {
        std::string pat = "\"" + key + "\":\"";
        auto pos = blob.find(pat);
        if (pos == std::string::npos) return "";
        pos += pat.size();
        auto end = blob.find('"', pos);
        while (end != std::string::npos && blob[end - 1] == '\\') end = blob.find('"', end + 1);
        if (end == std::string::npos) return "";
        return blob.substr(pos, end - pos);
    };

    // Split into objects
    size_t i = 0;
    while ((i = json.find('{', i)) != std::string::npos) {
        auto e = json.find('}', i);
        if (e == std::string::npos) break;
        std::string obj = json.substr(i, e - i + 1);
        SpParameter p;
        p.name        = extractField(obj, "name");
        p.type        = extractField(obj, "type");
        p.direction   = extractField(obj, "direction");
        p.default_val = extractField(obj, "default");
        if (!p.name.empty()) out.push_back(p);
        i = e + 1;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Row converters
// ---------------------------------------------------------------------------

StoredProcedure SpStore::rowToSp(const core::Row& r) const {
    StoredProcedure sp;
    if (r.size() > 0)  try { sp.id            = std::stoll(r[0]); } catch (...) {}
    if (r.size() > 1)       sp.name           = r[1];
    if (r.size() > 2)       sp.db_type        = r[2];
    if (r.size() > 3)       sp.database_name  = r[3];
    if (r.size() > 4)       sp.content        = r[4];
    if (r.size() > 5)       sp.context        = r[5];
    if (r.size() > 6)       sp.parameters     = parseJsonParams(r[6]);
    if (r.size() > 7)       sp.return_type    = r[7];
    if (r.size() > 8)       sp.tables_used    = parseJsonArray(r[8]);
    if (r.size() > 9)       sp.sp_dependencies= parseJsonArray(r[9]);
    if (r.size() > 10)      sp.scope_path     = r[10];
    if (r.size() > 11)      sp.tags           = r[11];
    if (r.size() > 12) try { sp.version       = std::stoi(r[12]); } catch (...) {}
    if (r.size() > 13) try { sp.created_at    = std::stoll(r[13]); } catch (...) {}
    if (r.size() > 14) try { sp.updated_at    = std::stoll(r[14]); } catch (...) {}
    return sp;
}

SpVersion SpStore::rowToVer(const core::Row& r) const {
    SpVersion v;
    if (r.size() > 0) try { v.id          = std::stoll(r[0]); } catch (...) {}
    if (r.size() > 1) try { v.sp_id       = std::stoll(r[1]); } catch (...) {}
    if (r.size() > 2) try { v.version     = std::stoi(r[2]);  } catch (...) {}
    if (r.size() > 3)      v.content      = r[3];
    if (r.size() > 4)      v.change_note  = r[4];
    if (r.size() > 5) try { v.created_at  = std::stoll(r[5]); } catch (...) {}
    return v;
}

static const std::string SELECT_SP =
    "SELECT id,name,db_type,database_name,content,context,parameters,"
    "return_type,tables_used,sp_dependencies,scope_path,tags,version,"
    "created_at,updated_at FROM stored_procedures";

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

SpStore::SpStore(core::Db& db) : db_(db) {}

// ---------------------------------------------------------------------------
// add()
// ---------------------------------------------------------------------------

int64_t SpStore::add(const StoredProcedure& sp) {
    int64_t now = nowEpoch();

    // Check if exists
    bool exists = false;
    int64_t existing_id = 0;
    int cur_version = 0;
    db_.query("SELECT id, version FROM stored_procedures WHERE name=? AND database_name=?",
              {sp.name, sp.database_name},
              [&](const core::Row& r) {
                  if (!r.empty()) {
                      exists = true;
                      try { existing_id = std::stoll(r[0]); } catch (...) {}
                      if (r.size() > 1) try { cur_version = std::stoi(r[1]); } catch (...) {}
                  }
              });

    if (exists) {
        // Save current version to history
        db_.query(SELECT_SP + " WHERE id=?", {std::to_string(existing_id)},
                  [&](const core::Row& r) {
                      auto old = rowToSp(r);
                      db_.run("INSERT INTO sp_versions(sp_id,version,content,change_note) VALUES(?,?,?,?)",
                              {std::to_string(existing_id),
                               std::to_string(old.version),
                               old.content, ""});
                  });

        // Update
        db_.run("UPDATE stored_procedures SET db_type=?,content=?,context=?,parameters=?,"
                "return_type=?,tables_used=?,sp_dependencies=?,scope_path=?,tags=?,"
                "version=?,updated_at=? WHERE id=?",
                {sp.db_type, sp.content, sp.context,
                 jsonParams(sp.parameters),
                 sp.return_type,
                 jsonArray(sp.tables_used),
                 jsonArray(sp.sp_dependencies),
                 sp.scope_path, sp.tags,
                 std::to_string(cur_version + 1),
                 std::to_string(now),
                 std::to_string(existing_id)});
        return existing_id;
    }

    db_.run("INSERT INTO stored_procedures(name,db_type,database_name,content,context,"
            "parameters,return_type,tables_used,sp_dependencies,scope_path,tags,"
            "version,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,1,?,?)",
            {sp.name, sp.db_type, sp.database_name, sp.content, sp.context,
             jsonParams(sp.parameters),
             sp.return_type,
             jsonArray(sp.tables_used),
             jsonArray(sp.sp_dependencies),
             sp.scope_path, sp.tags,
             std::to_string(now), std::to_string(now)});
    return db_.lastInsertId();
}

// ---------------------------------------------------------------------------
// update()
// ---------------------------------------------------------------------------

bool SpStore::update(const std::string& name, const std::string& sql,
                     const std::string& note) {
    auto existing = get(name);
    if (!existing) return false;

    int64_t now = nowEpoch();

    // Save old to history
    db_.run("INSERT INTO sp_versions(sp_id,version,content,change_note) VALUES(?,?,?,?)",
            {std::to_string(existing->id),
             std::to_string(existing->version),
             existing->content, note});

    // Re-parse is done in sp_cmd before calling update()
    db_.run("UPDATE stored_procedures SET content=?,version=?,updated_at=? WHERE id=?",
            {sql, std::to_string(existing->version + 1),
             std::to_string(now), std::to_string(existing->id)});
    return true;
}

// ---------------------------------------------------------------------------
// remove()
// ---------------------------------------------------------------------------

bool SpStore::remove(const std::string& name) {
    int cnt = 0;
    db_.query("SELECT COUNT(*) FROM stored_procedures WHERE name=?", {name},
              [&](const core::Row& r) { if (!r.empty()) try { cnt = std::stoi(r[0]); } catch (...) {} });
    if (cnt == 0) return false;
    db_.run("DELETE FROM stored_procedures WHERE name=?", {name});
    return true;
}

// ---------------------------------------------------------------------------
// get()
// ---------------------------------------------------------------------------

std::optional<StoredProcedure> SpStore::get(const std::string& name) const {
    std::optional<StoredProcedure> result;
    db_.query(SELECT_SP + " WHERE name=?", {name},
              [&](const core::Row& r) { result = rowToSp(r); });
    return result;
}

// ---------------------------------------------------------------------------
// list()
// ---------------------------------------------------------------------------

std::vector<StoredProcedure> SpStore::list(const std::string& db_type,
                                             const std::string& database) const {
    std::vector<StoredProcedure> result;
    if (!db_type.empty() && !database.empty()) {
        db_.query(SELECT_SP + " WHERE db_type=? AND database_name=? ORDER BY name",
                  {db_type, database},
                  [&](const core::Row& r) { result.push_back(rowToSp(r)); });
    } else if (!db_type.empty()) {
        db_.query(SELECT_SP + " WHERE db_type=? ORDER BY name", {db_type},
                  [&](const core::Row& r) { result.push_back(rowToSp(r)); });
    } else if (!database.empty()) {
        db_.query(SELECT_SP + " WHERE database_name=? ORDER BY name", {database},
                  [&](const core::Row& r) { result.push_back(rowToSp(r)); });
    } else {
        db_.query(SELECT_SP + " ORDER BY name", {},
                  [&](const core::Row& r) { result.push_back(rowToSp(r)); });
    }
    return result;
}

// ---------------------------------------------------------------------------
// search()
// ---------------------------------------------------------------------------

std::vector<StoredProcedure> SpStore::search(const std::string& query,
                                              int limit) const {
    std::string like = "%" + query + "%";
    std::vector<StoredProcedure> result;
    db_.query(SELECT_SP + " WHERE name LIKE ? OR context LIKE ? OR tables_used LIKE ?"
              " OR tags LIKE ? ORDER BY name LIMIT ?",
              {like, like, like, like, std::to_string(limit)},
              [&](const core::Row& r) { result.push_back(rowToSp(r)); });
    return result;
}

// ---------------------------------------------------------------------------
// usesTable()
// ---------------------------------------------------------------------------

std::vector<StoredProcedure> SpStore::usesTable(const std::string& table) const {
    std::string like = "%" + table + "%";
    std::vector<StoredProcedure> result;
    db_.query(SELECT_SP + " WHERE tables_used LIKE ? ORDER BY name", {like},
              [&](const core::Row& r) { result.push_back(rowToSp(r)); });
    return result;
}

// ---------------------------------------------------------------------------
// calledBy()
// ---------------------------------------------------------------------------

std::vector<StoredProcedure> SpStore::calledBy(const std::string& sp_name) const {
    std::string like = "%" + sp_name + "%";
    std::vector<StoredProcedure> result;
    db_.query(SELECT_SP + " WHERE sp_dependencies LIKE ? ORDER BY name", {like},
              [&](const core::Row& r) { result.push_back(rowToSp(r)); });
    return result;
}

// ---------------------------------------------------------------------------
// history()
// ---------------------------------------------------------------------------

std::vector<SpVersion> SpStore::history(const std::string& name) const {
    auto sp = get(name);
    if (!sp) return {};

    std::vector<SpVersion> result;
    db_.query("SELECT id,sp_id,version,content,change_note,created_at "
              "FROM sp_versions WHERE sp_id=? ORDER BY version DESC",
              {std::to_string(sp->id)},
              [&](const core::Row& r) { result.push_back(rowToVer(r)); });
    return result;
}

} // namespace icmg::sp
