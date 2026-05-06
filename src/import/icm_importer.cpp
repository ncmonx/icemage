#include "base_importer.hpp"
#include "../core/registry.hpp"
#include <fstream>
#include <chrono>

// ICM importer: reads an ICM SQLite DB (from icm MCP tool) and maps to memory_nodes.
// Tries "memories" table first (current ICM schema), then falls back to any table
// that has topic+content columns.

namespace icmg {

class IcmImporter : public BaseImporter {
public:
    std::string name()        const override { return "icm"; }
    std::string description() const override {
        return "Import from ICM MCP tool SQLite database";
    }

protected:
    ImportStats doImport(const std::string& source,
                          core::Db& target_db,
                          const std::string& /*project_name*/) override {
        checkFileSize(source);

        // A3: SQLite magic check
        if (!isSQLiteFile(source)) {
            throw ImportError("Not a valid SQLite database: " + source);
        }

        // Open source DB
        core::Db src(source);

        ImportStats stats;
        int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // Try known ICM schema
        bool hasMemories = false;
        src.query("SELECT name FROM sqlite_master WHERE type='table' AND name='memories'",
                  {}, [&](const core::Row& r) { if (!r.empty()) hasMemories = true; });

        if (hasMemories) {
            importFromMemoriesTable(src, target_db, stats, now);
        } else {
            // Fallback: scan all tables for topic+content columns
            importFallback(src, target_db, stats, now);
        }

        return stats;
    }

private:
    static int64_t nowEpoch() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    void importFromMemoriesTable(core::Db& src, core::Db& dst,
                                  ImportStats& stats, int64_t now) {
        // ICM schema: id, topic, content, importance, keywords, last_used, frequency
        src.query("SELECT topic, content, importance, keywords, last_used, frequency "
                  "FROM memories",
                  {},
                  [&](const core::Row& r) {
                      if (r.size() < 2) return;
                      std::string topic   = r[0];
                      std::string content = r.size() > 1 ? r[1] : "";
                      std::string imp_str = r.size() > 2 ? r[2] : "1";
                      std::string kw      = r.size() > 3 ? r[3] : "";
                      std::string lu_str  = r.size() > 4 ? r[4] : "0";
                      std::string freq_s  = r.size() > 5 ? r[5] : "1";

                      try {
                          validateString(topic,   "topic");
                          validateString(content, "content");
                          validateString(kw,      "keywords");
                      } catch (const ValidationError& e) {
                          stats.errors++;
                          stats.error_messages.push_back(e.what());
                          return;
                      }

                      int importance = 1;
                      try { importance = std::stoi(imp_str); } catch (...) {}
                      importance = std::max(0, std::min(3, importance));

                      int64_t last_used = now;
                      try { last_used = std::stoll(lu_str); } catch (...) {}

                      int frequency = 1;
                      try { frequency = std::stoi(freq_s); } catch (...) {}

                      try {
                          dst.run("INSERT OR IGNORE INTO memory_nodes"
                                  "(topic,content,keywords,importance,frequency,"
                                  " last_used,created_at) VALUES(?,?,?,?,?,?,?)",
                                  {topic, content, kw,
                                   std::to_string(importance),
                                   std::to_string(frequency),
                                   std::to_string(last_used),
                                   std::to_string(now)});
                          stats.memory_nodes++;
                      } catch (const std::exception& e) {
                          stats.errors++;
                          stats.error_messages.push_back(
                              std::string("Insert failed: ") + e.what());
                      }
                  });
    }

    void importFallback(core::Db& src, core::Db& dst,
                         ImportStats& stats, int64_t now) {
        // Get all table names
        std::vector<std::string> tables;
        src.query("SELECT name FROM sqlite_master WHERE type='table'", {},
                  [&](const core::Row& r) { if (!r.empty()) tables.push_back(r[0]); });

        for (const auto& tbl : tables) {
            // Try to select topic+content
            bool ok = false;
            try {
                src.query("SELECT topic, content FROM \"" + tbl + "\" LIMIT 1", {},
                          [&](const core::Row&) { ok = true; });
            } catch (...) { continue; }

            if (!ok) continue;

            src.query("SELECT topic, content FROM \"" + tbl + "\"", {},
                      [&](const core::Row& r) {
                          if (r.size() < 2) return;
                          std::string topic   = r[0];
                          std::string content = r[1];

                          try {
                              validateString(topic,   "topic");
                              validateString(content, "content");
                          } catch (const ValidationError& e) {
                              stats.errors++;
                              stats.error_messages.push_back(e.what());
                              return;
                          }

                          try {
                              dst.run("INSERT OR IGNORE INTO memory_nodes"
                                      "(topic,content,keywords,importance,frequency,"
                                      " last_used,created_at) VALUES(?,?,?,?,?,?,?)",
                                      {topic, content, "", "1", "1",
                                       std::to_string(now),
                                       std::to_string(now)});
                              stats.memory_nodes++;
                          } catch (...) {
                              stats.errors++;
                          }
                      });
        }
    }
};

ICMG_REGISTER_IMPORTER("icm", IcmImporter);

} // namespace icmg
