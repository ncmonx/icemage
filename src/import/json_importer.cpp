#include "base_importer.hpp"
#include "../core/registry.hpp"
#include <fstream>
#include <filesystem>
#include <chrono>
#include <nlohmann/json.hpp>

// JSON importer: reads icmg export format (or flat arrays with type inference).

namespace icmg {

class JsonImporter : public BaseImporter {
public:
    std::string name()        const override { return "json"; }
    std::string description() const override {
        return "Import from icmg JSON export or flat JSON array";
    }

protected:
    ImportStats doImport(const std::string& source,
                          core::Db& target_db,
                          const std::string& /*project_name*/) override {
        checkFileSize(source);

        std::ifstream f(source);
        if (!f) throw ImportError("Cannot open: " + source);

        nlohmann::json doc;
        try {
            f >> doc;
        } catch (const std::exception& e) {
            throw ImportError(std::string("JSON parse error: ") + e.what());
        }

        ImportStats stats;
        int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        if (doc.is_array()) {
            // Flat array — infer type from fields
            importFlatArray(doc, target_db, stats, now);
        } else if (doc.is_object()) {
            // Full icmg export format
            if (doc.contains("memory_nodes") && doc["memory_nodes"].is_array())
                importMemoryNodes(doc["memory_nodes"], target_db, stats, now);
            if (doc.contains("graph_nodes") && doc["graph_nodes"].is_array())
                importGraphNodes(doc["graph_nodes"], target_db, stats, now);
            if (doc.contains("graph_edges") && doc["graph_edges"].is_array())
                importGraphEdges(doc["graph_edges"], target_db, stats, now);
            if (doc.contains("abbreviations") && doc["abbreviations"].is_array())
                importAbbreviations(doc["abbreviations"], target_db, stats, now);
            if (doc.contains("stored_procedures") && doc["stored_procedures"].is_array())
                importStoredProcs(doc["stored_procedures"], target_db, stats, now);
            if (doc.contains("rules") && doc["rules"].is_array())
                importRules(doc["rules"], target_db, stats, now);
        } else {
            throw ImportError("JSON must be an object or array");
        }

        return stats;
    }

private:
    // A5: field validation helpers
    static std::string reqStr(const nlohmann::json& j, const std::string& key) {
        if (!j.contains(key) || !j[key].is_string())
            throw ValidationError("Missing required string field: " + key);
        std::string v = j[key].get<std::string>();
        if (v.size() > 1024 * 1024)
            throw ValidationError("Field '" + key + "' exceeds 1MB limit");
        return v;
    }

    static std::string optStr(const nlohmann::json& j, const std::string& key,
                               const std::string& def = "") {
        if (!j.contains(key) || !j[key].is_string()) return def;
        return j[key].get<std::string>();
    }

    static int64_t optInt64(const nlohmann::json& j, const std::string& key,
                             int64_t def = 0) {
        if (!j.contains(key)) return def;
        if (j[key].is_number_integer()) return j[key].get<int64_t>();
        if (j[key].is_string()) {
            try { return std::stoll(j[key].get<std::string>()); } catch (...) {}
        }
        return def;
    }

    static int optInt(const nlohmann::json& j, const std::string& key, int def = 0) {
        return static_cast<int>(optInt64(j, key, def));
    }

    void importMemoryNodes(const nlohmann::json& arr, core::Db& db,
                            ImportStats& stats, int64_t now) {
        for (const auto& item : arr) {
            if (!item.is_object()) continue;
            try {
                std::string topic   = reqStr(item, "topic");
                std::string content = reqStr(item, "content");
                std::string kw      = optStr(item, "keywords");
                int imp             = optInt(item, "importance", 1);
                int freq            = optInt(item, "frequency",  1);
                int64_t lu          = optInt64(item, "last_used", now);

                validateString(topic,   "topic");
                validateString(content, "content");
                validateString(kw,      "keywords");

                imp = std::max(0, std::min(3, imp));

                db.run("INSERT OR IGNORE INTO memory_nodes"
                       "(topic,content,keywords,importance,frequency,"
                       " last_used,created_at) VALUES(?,?,?,?,?,?,?)",
                       {topic, content, kw,
                        std::to_string(imp), std::to_string(freq),
                        std::to_string(lu), std::to_string(now)});
                stats.memory_nodes++;
            } catch (const ValidationError& e) {
                stats.errors++;
                stats.error_messages.push_back(std::string("memory_node: ") + e.what());
            } catch (const std::exception& e) {
                stats.errors++;
                stats.error_messages.push_back(std::string("memory_node insert: ") + e.what());
            }
        }
    }

    void importGraphNodes(const nlohmann::json& arr, core::Db& db,
                           ImportStats& stats, int64_t now) {
        for (const auto& item : arr) {
            if (!item.is_object()) continue;
            try {
                std::string path = reqStr(item, "path");
                std::string lang = optStr(item, "lang");
                std::string ctx  = optStr(item, "context");
                std::string sym  = optStr(item, "symbols", "{}");
                int64_t sz       = optInt64(item, "size_bytes", 0);

                validateString(path, "path");
                validateString(lang, "lang");

                db.run("INSERT INTO graph_nodes(path,lang,context,symbols,size_bytes,"
                        "file_hash,updated_at,access_count) VALUES(?,?,?,?,?,?,?,?)"
                        " ON CONFLICT(path) DO UPDATE SET"
                        " lang=excluded.lang, context=excluded.context,"
                        " symbols=excluded.symbols, size_bytes=excluded.size_bytes,"
                        " updated_at=excluded.updated_at",
                        {path, lang, ctx, sym, std::to_string(sz),
                         "", std::to_string(now), "0"});
                stats.graph_nodes++;
            } catch (const ValidationError& e) {
                stats.errors++;
                stats.error_messages.push_back(std::string("graph_node: ") + e.what());
            } catch (const std::exception& e) {
                stats.errors++;
                stats.error_messages.push_back(std::string("graph_node insert: ") + e.what());
            }
        }
    }

    void importGraphEdges(const nlohmann::json& arr, core::Db& db,
                           ImportStats& stats, int64_t /*now*/) {
        for (const auto& item : arr) {
            if (!item.is_object()) continue;
            try {
                // Edges reference paths, resolve to IDs
                std::string src_path = optStr(item, "src_path");
                std::string dst_path = optStr(item, "dst_path");
                std::string etype    = optStr(item, "edge_type", "imports");
                double weight        = 1.0;
                if (item.contains("weight") && item["weight"].is_number())
                    weight = item["weight"].get<double>();

                int64_t src_id = -1, dst_id = -1;
                db.query("SELECT id FROM graph_nodes WHERE path=?", {src_path},
                         [&](const core::Row& r) {
                             if (!r.empty()) try { src_id = std::stoll(r[0]); } catch (...) {}
                         });
                db.query("SELECT id FROM graph_nodes WHERE path=?", {dst_path},
                         [&](const core::Row& r) {
                             if (!r.empty()) try { dst_id = std::stoll(r[0]); } catch (...) {}
                         });

                if (src_id < 0) { stats.errors++; continue; }

                db.run("INSERT OR IGNORE INTO graph_edges(src,dst,edge_type,weight)"
                       " VALUES(?,?,?,?)",
                       {std::to_string(src_id), std::to_string(dst_id),
                        etype, std::to_string(weight)});
                stats.graph_edges++;
            } catch (const std::exception& e) {
                stats.errors++;
                stats.error_messages.push_back(std::string("graph_edge: ") + e.what());
            }
        }
    }

    void importAbbreviations(const nlohmann::json& arr, core::Db& db,
                              ImportStats& stats, int64_t now) {
        for (const auto& item : arr) {
            if (!item.is_object()) continue;
            try {
                std::string sf  = reqStr(item, "short_form");
                std::string ff  = reqStr(item, "full_form");
                std::string dom = optStr(item, "domain", "general");
                std::string sp  = optStr(item, "scope_path");
                int freq        = optInt(item, "frequency", 1);

                validateString(sf, "short_form");
                validateString(ff, "full_form");

                db.run("INSERT OR IGNORE INTO abbreviations"
                       "(short_form,full_form,domain,scope_path,frequency,created_at)"
                       " VALUES(?,?,?,?,?,?)",
                       {sf, ff, dom, sp, std::to_string(freq), std::to_string(now)});
                stats.abbreviations++;
            } catch (const ValidationError& e) {
                stats.errors++;
                stats.error_messages.push_back(std::string("abbreviation: ") + e.what());
            } catch (const std::exception& e) {
                stats.errors++;
                stats.error_messages.push_back(std::string("abbreviation insert: ") + e.what());
            }
        }
    }

    void importStoredProcs(const nlohmann::json& arr, core::Db& db,
                            ImportStats& stats, int64_t now) {
        for (const auto& item : arr) {
            if (!item.is_object()) continue;
            try {
                std::string nm  = reqStr(item, "name");
                std::string cnt = reqStr(item, "content");
                std::string dbt = optStr(item, "db_type", "mssql");
                std::string dbn = optStr(item, "database_name");
                std::string ctx = optStr(item, "context");

                validateString(nm,  "name");
                validateString(cnt, "content");

                db.run("INSERT OR IGNORE INTO stored_procedures"
                       "(name,db_type,database_name,content,context,"
                       " parameters,return_type,tables_used,sp_dependencies,"
                       " scope_path,tags,version,created_at,updated_at)"
                       " VALUES(?,?,?,?,?,?,?,?,?,?,?,1,?,?)",
                       {nm, dbt, dbn, cnt, ctx,
                        "[]", "", "[]", "[]", "", "",
                        std::to_string(now), std::to_string(now)});
                stats.stored_procs++;
            } catch (const ValidationError& e) {
                stats.errors++;
                stats.error_messages.push_back(std::string("stored_proc: ") + e.what());
            } catch (const std::exception& e) {
                stats.errors++;
                stats.error_messages.push_back(std::string("stored_proc insert: ") + e.what());
            }
        }
    }

    void importRules(const nlohmann::json& arr, core::Db& db,
                      ImportStats& stats, int64_t now) {
        for (const auto& item : arr) {
            if (!item.is_object()) continue;
            try {
                std::string scope    = reqStr(item, "scope_path");
                std::string rtype    = optStr(item, "rule_type", "custom");
                std::string rname    = optStr(item, "name", "imported");
                std::string content  = reqStr(item, "content");
                int priority         = optInt(item, "priority", 0);

                validateString(scope,   "scope_path");
                validateString(content, "content");

                db.run("INSERT OR IGNORE INTO rules"
                       "(scope_path,rule_type,name,content,priority,active,created_at)"
                       " VALUES(?,?,?,?,?,1,?)",
                       {scope, rtype, rname, content,
                        std::to_string(priority), std::to_string(now)});
                stats.rules++;
            } catch (const ValidationError& e) {
                stats.errors++;
                stats.error_messages.push_back(std::string("rule: ") + e.what());
            } catch (const std::exception& e) {
                stats.errors++;
                stats.error_messages.push_back(std::string("rule insert: ") + e.what());
            }
        }
    }

    void importFlatArray(const nlohmann::json& arr, core::Db& db,
                          ImportStats& stats, int64_t now) {
        // Infer type from presence of key fields
        for (const auto& item : arr) {
            if (!item.is_object()) continue;
            bool hasTopic    = item.contains("topic") && item.contains("content");
            bool hasPath     = item.contains("path") && !item.contains("topic");
            bool hasShort    = item.contains("short_form") && item.contains("full_form");
            bool hasSp       = item.contains("name") && item.contains("content") &&
                               item.contains("db_type");
            bool hasScope    = item.contains("scope_path") && item.contains("content") &&
                               !item.contains("path") && !item.contains("topic");

            nlohmann::json wrapper = nlohmann::json::array();
            wrapper.push_back(item);

            if (hasTopic)      importMemoryNodes(wrapper, db, stats, now);
            else if (hasPath)  importGraphNodes(wrapper, db, stats, now);
            else if (hasShort) importAbbreviations(wrapper, db, stats, now);
            else if (hasSp)    importStoredProcs(wrapper, db, stats, now);
            else if (hasScope) importRules(wrapper, db, stats, now);
            else {
                stats.errors++;
                stats.error_messages.push_back("Cannot infer type for item (no known fields)");
            }
        }
    }
};

ICMG_REGISTER_IMPORTER("json", JsonImporter);

} // namespace icmg
