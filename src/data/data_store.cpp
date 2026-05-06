#include "data_store.hpp"
#include <sstream>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace icmg::data {

DataStore::DataStore(core::Db& db) : db_(db) {}

// ---- row helpers -----------------------------------------------------------

StructuredData DataStore::fromRow(const core::Row& r) {
    StructuredData d;
    if (r.size() > 0) try { d.id         = std::stoll(r[0]); } catch (...) {}
    if (r.size() > 1) d.data_type  = r[1];
    if (r.size() > 2) d.name       = r[2];
    if (r.size() > 3) d.scope_path = r[3];
    if (r.size() > 4) d.content    = r[4];
    if (r.size() > 5) d.version    = r[5];
    if (r.size() > 6) d.tags       = r[6];
    if (r.size() > 7) try { d.created_at = std::stoll(r[7]); } catch (...) {}
    if (r.size() > 8) try { d.updated_at = std::stoll(r[8]); } catch (...) {}
    return d;
}

DataVersion DataStore::versionFromRow(const core::Row& r) {
    DataVersion v;
    if (r.size() > 0) try { v.id          = std::stoll(r[0]); } catch (...) {}
    if (r.size() > 1) try { v.data_id     = std::stoll(r[1]); } catch (...) {}
    if (r.size() > 2) v.version     = r[2];
    if (r.size() > 3) v.content     = r[3];
    if (r.size() > 4) v.change_note = r[4];
    if (r.size() > 5) try { v.created_at  = std::stoll(r[5]); } catch (...) {}
    return v;
}

std::string DataStore::bumpVersion(const std::string& v) {
    // "1.0" → "1.1", "1.9" → "1.10", "2.3" → "2.4"
    auto dot = v.rfind('.');
    if (dot == std::string::npos) {
        try { return std::to_string(std::stoi(v) + 1); } catch (...) { return "2"; }
    }
    std::string major = v.substr(0, dot);
    std::string minor = v.substr(dot + 1);
    int m = 0;
    try { m = std::stoi(minor); } catch (...) {}
    return major + "." + std::to_string(m + 1);
}

// ---- add -------------------------------------------------------------------

int64_t DataStore::add(const StructuredData& d) {
    // Normalise scope_path
    std::string scope = d.scope_path;
    if (!scope.empty() && scope.back() != '/') scope += '/';

    try {
        db_.run(
            "INSERT INTO structured_data(data_type,name,scope_path,content,version,tags,"
            " created_at,updated_at)"
            " VALUES(?,?,?,?,?,?,strftime('%s','now'),strftime('%s','now'))",
            {d.data_type, d.name, scope, d.content, d.version, d.tags});
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        if (msg.find("UNIQUE") != std::string::npos)
            throw std::runtime_error("Structured data '" + d.name + "' already exists.");
        throw;
    }

    int64_t id = 0;
    db_.query("SELECT last_insert_rowid()", {}, [&](const core::Row& r) {
        if (!r.empty()) try { id = std::stoll(r[0]); } catch (...) {}
    });
    return id;
}

// ---- update ----------------------------------------------------------------

bool DataStore::update(const std::string& name, const std::string& content,
                       const std::string& note) {
    auto cur = get(name);
    if (!cur) return false;

    // Save old version to data_versions (A1)
    db_.run(
        "INSERT INTO data_versions(data_id,version,content,changed_by,created_at)"
        " VALUES(?,?,?,?,strftime('%s','now'))",
        {std::to_string(cur->id), cur->version, cur->content, note});

    std::string new_ver = bumpVersion(cur->version);
    db_.run(
        "UPDATE structured_data SET content=?,version=?,updated_at=strftime('%s','now')"
        " WHERE name=?",
        {content, new_ver, name});
    return true;
}

// ---- remove ----------------------------------------------------------------

bool DataStore::remove(const std::string& name) {
    db_.run("DELETE FROM structured_data WHERE name=?", {name});
    return true;
}

// ---- get -------------------------------------------------------------------

std::optional<StructuredData> DataStore::get(const std::string& name) const {
    std::optional<StructuredData> result;
    db_.query(
        "SELECT id,data_type,name,scope_path,content,version,tags,created_at,updated_at"
        " FROM structured_data WHERE name=?",
        {name},
        [&](const core::Row& r) { result = fromRow(r); });
    return result;
}

std::optional<StructuredData> DataStore::getById(int64_t id) const {
    std::optional<StructuredData> result;
    db_.query(
        "SELECT id,data_type,name,scope_path,content,version,tags,created_at,updated_at"
        " FROM structured_data WHERE id=?",
        {std::to_string(id)},
        [&](const core::Row& r) { result = fromRow(r); });
    return result;
}

// ---- list ------------------------------------------------------------------

std::vector<StructuredData> DataStore::list(const std::string& type,
                                             const std::string& scope) const {
    std::vector<StructuredData> result;
    std::string sql =
        "SELECT id,data_type,name,scope_path,content,version,tags,created_at,updated_at"
        " FROM structured_data WHERE 1=1";
    std::vector<std::string> params;

    if (!type.empty())  { sql += " AND data_type=?"; params.push_back(type); }
    if (!scope.empty()) { sql += " AND scope_path LIKE ?"; params.push_back(scope + "%"); }
    sql += " ORDER BY data_type,name";

    db_.query(sql, params, [&](const core::Row& r) { result.push_back(fromRow(r)); });
    return result;
}

// ---- search ----------------------------------------------------------------

std::vector<StructuredData> DataStore::search(const std::string& query, int limit) const {
    if (query.empty()) return list();

    // Tokenise query
    std::vector<std::string> tokens;
    std::istringstream ss(query);
    std::string tok;
    while (ss >> tok) {
        std::transform(tok.begin(), tok.end(), tok.begin(), ::tolower);
        tokens.push_back(tok);
    }

    auto all = list();

    // Score: count token hits in (name + content + tags), weighted by field
    auto score = [&](const StructuredData& d) -> double {
        std::string text = d.name + " " + d.data_type + " " + d.tags + " " + d.content;
        std::transform(text.begin(), text.end(), text.begin(), ::tolower);
        double s = 0;
        for (auto& t : tokens) {
            size_t pos = 0;
            while ((pos = text.find(t, pos)) != std::string::npos) { ++s; ++pos; }
            // Bonus: name match
            std::string lname = d.name; std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
            if (lname.find(t) != std::string::npos) s += 3;
        }
        return s;
    };

    std::vector<std::pair<double,StructuredData>> scored;
    for (auto& d : all) {
        double s = score(d);
        if (s > 0) scored.emplace_back(s, d);
    }
    std::sort(scored.begin(), scored.end(),
              [](auto& a, auto& b) { return a.first > b.first; });

    std::vector<StructuredData> result;
    int n = std::min((int)scored.size(), limit);
    for (int i = 0; i < n; ++i) result.push_back(scored[i].second);
    return result;
}

// ---- history ---------------------------------------------------------------

std::vector<DataVersion> DataStore::history(const std::string& name) const {
    auto cur = get(name);
    if (!cur) return {};

    std::vector<DataVersion> result;
    db_.query(
        "SELECT id,data_id,version,content,COALESCE(changed_by,''),created_at"
        " FROM data_versions WHERE data_id=? ORDER BY created_at DESC",
        {std::to_string(cur->id)},
        [&](const core::Row& r) { result.push_back(versionFromRow(r)); });
    return result;
}

// ---- revert ----------------------------------------------------------------

bool DataStore::revert(const std::string& name, const std::string& to_version) {
    auto cur = get(name);
    if (!cur) return false;

    // Find target version in history
    std::optional<DataVersion> target;
    db_.query(
        "SELECT id,data_id,version,content,COALESCE(changed_by,''),created_at"
        " FROM data_versions WHERE data_id=? AND version=? ORDER BY created_at DESC LIMIT 1",
        {std::to_string(cur->id), to_version},
        [&](const core::Row& r) { target = versionFromRow(r); });

    if (!target) return false;

    return update(name, target->content, "revert to " + to_version);
}

// ---- forFile ---------------------------------------------------------------

std::vector<StructuredData> DataStore::forFile(const std::string& file_path) const {
    std::vector<StructuredData> result;
    auto all = list();
    for (auto& d : all) {
        // scope_path="" or NULL → applies everywhere
        if (d.scope_path.empty() || file_path.find(d.scope_path) == 0)
            result.push_back(d);
    }
    return result;
}

} // namespace icmg::data
