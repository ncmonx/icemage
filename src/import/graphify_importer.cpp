#include "base_importer.hpp"
#include "../core/registry.hpp"
#include <fstream>
#include <sstream>
#include <regex>
#include <filesystem>
#include <chrono>

// Graphify importer: parses GRAPH_REPORT.md (output of the graphify tool).
// Maps file nodes → graph_nodes, community tags, and edges where parseable.

namespace icmg {

class GraphifyImporter : public BaseImporter {
public:
    std::string name()        const override { return "graphify"; }
    std::string description() const override {
        return "Import from Graphify GRAPH_REPORT.md";
    }

protected:
    ImportStats doImport(const std::string& source,
                          core::Db& target_db,
                          const std::string& project_name) override {
        namespace fs = std::filesystem;

        // source may be a directory or a .md file
        std::string mdPath = source;
        if (fs::is_directory(source)) {
            mdPath = (fs::path(source) / "GRAPH_REPORT.md").string();
        }

        if (!fs::exists(mdPath))
            throw ImportError("Graphify report not found: " + mdPath);

        checkFileSize(mdPath);

        std::ifstream f(mdPath);
        if (!f) throw ImportError("Cannot open: " + mdPath);

        ImportStats stats;
        int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // A2: compute project root for path validation
        std::string root = fs::absolute(fs::path(mdPath).parent_path()).string();
        if (!project_name.empty()) {
            // if caller specified project root, use it
        }

        // Parse report
        // Look for patterns:
        //   ## File: path/to/file.cpp
        //   - Language: cpp
        //   - Size: 1234 bytes
        //   - Community: 3
        //   - Imports: path/a.hpp, path/b.hpp

        std::regex fileHeading(R"(^#+\s+(?:File|Node):\s+(.+)$)");
        std::regex langLine(R"(^\s*-\s*[Ll]anguage:\s+(\S+))");
        std::regex sizeLine(R"(^\s*-\s*[Ss]ize:\s+(\d+))");
        std::regex commLine(R"(^\s*-\s*[Cc]ommunity(?:\s+[Ii][Dd])?:\s+(\d+))");
        std::regex importLine(R"(^\s*-\s*[Ii]mports?:\s+(.+)$)");

        struct NodeEntry {
            std::string path;
            std::string lang;
            int64_t size = 0;
            std::string community;
            std::vector<std::string> imports;
        };

        std::vector<NodeEntry> nodes;
        NodeEntry cur;
        bool inNode = false;

        std::string line;
        while (std::getline(f, line)) {
            std::smatch m;
            if (std::regex_match(line, m, fileHeading)) {
                if (inNode && !cur.path.empty()) nodes.push_back(cur);
                cur = NodeEntry{};
                cur.path = m[1].str();
                // trim whitespace
                while (!cur.path.empty() && cur.path.back() == ' ') cur.path.pop_back();
                inNode = true;
                continue;
            }
            if (!inNode) continue;
            if (std::regex_search(line, m, langLine))   { cur.lang = m[1].str(); continue; }
            if (std::regex_search(line, m, sizeLine))   {
                try { cur.size = std::stoll(m[1].str()); } catch (...) {}
                continue;
            }
            if (std::regex_search(line, m, commLine))   {
                cur.community = "community:" + m[1].str();
                continue;
            }
            if (std::regex_search(line, m, importLine)) {
                // comma or space separated
                std::string deps = m[1].str();
                std::istringstream ss(deps);
                std::string tok;
                while (std::getline(ss, tok, ',')) {
                    while (!tok.empty() && tok.front() == ' ') tok.erase(0,1);
                    while (!tok.empty() && tok.back()  == ' ') tok.pop_back();
                    if (!tok.empty()) cur.imports.push_back(tok);
                }
                continue;
            }
        }
        if (inNode && !cur.path.empty()) nodes.push_back(cur);

        // Insert nodes
        std::unordered_map<std::string, int64_t> pathToId;
        for (auto& n : nodes) {
            // A2: canonicalize + validate
            std::string canon = n.path;
            try {
                auto cp = fs::absolute(fs::path(root) / n.path);
                canon = cp.string();
            } catch (...) {}

            try { validateString(canon, "path"); } catch (const ValidationError& e) {
                stats.errors++;
                stats.error_messages.push_back(e.what());
                continue;
            }

            std::string tags = n.community;

            int64_t id = 0;
            try {
                // upsert node — store community tag in context
                std::string ctx = n.community; // "community:N"
                target_db.run("INSERT INTO graph_nodes(path,lang,context,symbols,size_bytes,"
                               "file_hash,updated_at,access_count) VALUES(?,?,?,?,?,?,?,?)"
                               " ON CONFLICT(path) DO UPDATE SET"
                               " lang=excluded.lang, context=excluded.context,"
                               " size_bytes=excluded.size_bytes, updated_at=excluded.updated_at",
                               {canon, n.lang, ctx, "{}", std::to_string(n.size),
                                "", std::to_string(now), "0"});
                // get id
                target_db.query("SELECT id FROM graph_nodes WHERE path=?", {canon},
                                [&](const core::Row& r) {
                                    if (!r.empty()) try { id = std::stoll(r[0]); } catch (...) {}
                                });
                pathToId[n.path] = id;
                stats.graph_nodes++;
            } catch (const std::exception& e) {
                stats.errors++;
                stats.error_messages.push_back(std::string("Node insert: ") + e.what());
            }
        }

        // Insert edges
        for (auto& n : nodes) {
            auto it = pathToId.find(n.path);
            if (it == pathToId.end()) continue;
            int64_t src_id = it->second;

            for (auto& dep : n.imports) {
                auto dit = pathToId.find(dep);
                int64_t dst_id = dit != pathToId.end() ? dit->second : -1;
                try {
                    target_db.run("INSERT OR IGNORE INTO graph_edges"
                                  "(src,dst,edge_type,weight) VALUES(?,?,?,?)",
                                  {std::to_string(src_id), std::to_string(dst_id),
                                   "imports", "1.0"});
                    stats.graph_edges++;
                } catch (...) {}
            }
        }

        return stats;
    }
};

ICMG_REGISTER_IMPORTER("graphify", GraphifyImporter);

} // namespace icmg
