// `icmg template` — manifest-driven scaffolding workflow (Phase 25 T2+T3).
//
// Conservative defaults:
//   - extract prints to stdout unless --save-as given
//   - apply emits scaffold ONLY when --to file doesn't exist
//   - apply --check runs parity-equivalent diff; never writes
//   - body_hash staleness check warns when source has drifted since extract
//   - no auto-install hook; opt-in only
//
// Manifest schema (v1):
//   {
//     "name": "...",  "source": "...",  "version": 1,
//     "kind_counts":      {"method": N, "property": N, ...},
//     "required_symbols": [{"name": "...", "kind": "...", "signature": "..."}, ...],
//     "structural_markers": ["using ...", "DataGridView", ...],
//     "memoir_id": null
//   }

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../graph/graph_store.hpp"
#include "../../graph/graph_node.hpp"
#include "../../embed/embedder.hpp"           // fnv1a64
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <regex>
#include <chrono>
#include <set>
#include <map>

namespace fs = std::filesystem;
using nlohmann::json;

namespace icmg::cli {

// Local flagValue for unrecognized options w/ default.
static std::string fv(const std::vector<std::string>& a, const std::string& k, const std::string& def = "") {
    for (size_t i = 0; i < a.size(); ++i) if (a[i] == k && i + 1 < a.size()) return a[i + 1];
    return def;
}
static bool fl(const std::vector<std::string>& a, const std::string& k) {
    for (auto& x : a) if (x == k) return true;
    return false;
}

class TemplateCommand : public BaseCommand {
public:
    std::string name()        const override { return "template"; }
    std::string description() const override { return "Manifest-driven scaffold from a reference file"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg template <action> [args]\n\n"
            "Actions:\n"
            "  extract <ref-file> [--save-as NAME] [--memoir-id N]\n"
            "  list\n"
            "  show <name>\n"
            "  delete <name>\n"
            "  apply <name> --to <new-file> [--check] [--json]\n\n"
            "Notes:\n"
            "  - Source must be in graph_nodes (run `icmg graph scan` first).\n"
            "  - Manifest pins body_hash; staleness warning on apply.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        std::string action = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));

        if (action == "extract") return doExtract(db, rest);
        if (action == "list")    return doList(db);
        if (action == "show")    return doShow(db, rest);
        if (action == "delete")  return doDelete(db, rest);
        if (action == "apply")   return doApply(db, rest);

        std::cerr << "icmg template: unknown action: " << action << "\n";
        usage();
        return 1;
    }

private:
    // -----------------------------------------------------------------------
    // extract
    // -----------------------------------------------------------------------
    int doExtract(core::Db& db, const std::vector<std::string>& args) {
        std::string ref;
        for (auto& a : args) { if (a.empty() || a[0] == '-') continue; ref = a; break; }
        if (ref.empty()) { std::cerr << "icmg template extract: <ref-file> required\n"; return 1; }

        std::string save_as = fv(args, "--save-as");
        std::string memoir_id = fv(args, "--memoir-id");

        graph::GraphStore store(db);
        auto node = store.getNode(ref);
        if (!node) {
            std::cerr << "icmg template extract: file not in graph: " << ref
                      << " (run `icmg graph scan` first)\n";
            return 2;
        }
        auto syms = store.childrenOf(node->id);

        // structural_markers: read first 80 lines of file, pull `using`/`import`/class header.
        std::vector<std::string> markers = readStructuralMarkers(ref);

        // Build manifest
        json m;
        m["version"]            = 1;
        m["name"]               = save_as.empty() ? fs::path(ref).stem().string() : save_as;
        m["source"]             = ref;
        m["kind_counts"]        = json::object();
        m["required_symbols"]   = json::array();
        m["structural_markers"] = markers;
        m["memoir_id"]          = memoir_id.empty() ? json(nullptr) : json(memoir_id);

        std::map<std::string, int> kind_count;
        for (auto& s : syms) {
            ++kind_count[s.kind];
            m["required_symbols"].push_back({
                {"name",      s.symbol_name},
                {"kind",      s.kind},
                {"signature", s.signature}
            });
        }
        for (auto& [k, n] : kind_count) m["kind_counts"][k] = n;

        // Compute body_hash from source file content for staleness.
        std::string body_hash = hashFile(ref);

        if (save_as.empty()) {
            std::cout << m.dump(2) << "\n";
        } else {
            db.run("INSERT INTO templates(name, source_path, manifest_json, body_hash, memoir_id, updated_at) "
                   "VALUES(?,?,?,?,?,strftime('%s','now')) "
                   "ON CONFLICT(name) DO UPDATE SET source_path=excluded.source_path, "
                   "manifest_json=excluded.manifest_json, body_hash=excluded.body_hash, "
                   "memoir_id=excluded.memoir_id, updated_at=excluded.updated_at",
                   {save_as, ref, m.dump(), body_hash,
                    memoir_id.empty() ? "" : memoir_id});
            std::cout << "Saved template '" << save_as << "' from " << ref
                      << " (" << syms.size() << " symbols, " << markers.size()
                      << " markers).\n";
        }
        return 0;
    }

    // -----------------------------------------------------------------------
    // list
    // -----------------------------------------------------------------------
    int doList(core::Db& db) {
        std::cout << std::left << std::setw(20) << "NAME"
                  << std::setw(50) << "SOURCE" << "SYMBOLS" << "\n";
        std::cout << std::string(80, '-') << "\n";
        db.query("SELECT name, source_path, manifest_json FROM templates ORDER BY name", {},
                 [](const core::Row& r) {
                     if (r.size() < 3) return;
                     int sym_n = 0;
                     try {
                         auto j = json::parse(r[2]);
                         if (j.contains("required_symbols")) sym_n = (int)j["required_symbols"].size();
                     } catch (...) {}
                     std::cout << std::left << std::setw(20) << r[0]
                               << std::setw(50) << r[1] << sym_n << "\n";
                 });
        return 0;
    }

    // -----------------------------------------------------------------------
    // show
    // -----------------------------------------------------------------------
    int doShow(core::Db& db, const std::vector<std::string>& args) {
        std::string name;
        for (auto& a : args) { if (a.empty() || a[0] == '-') continue; name = a; break; }
        if (name.empty()) { std::cerr << "icmg template show: <name> required\n"; return 1; }
        std::string mj;
        db.query("SELECT manifest_json FROM templates WHERE name=?", {name},
                 [&](const core::Row& r) { if (!r.empty()) mj = r[0]; });
        if (mj.empty()) { std::cerr << "Not found: " << name << "\n"; return 1; }
        std::cout << json::parse(mj).dump(2) << "\n";
        return 0;
    }

    // -----------------------------------------------------------------------
    // delete
    // -----------------------------------------------------------------------
    int doDelete(core::Db& db, const std::vector<std::string>& args) {
        std::string name;
        for (auto& a : args) { if (a.empty() || a[0] == '-') continue; name = a; break; }
        if (name.empty()) { std::cerr << "icmg template delete: <name> required\n"; return 1; }
        db.run("DELETE FROM templates WHERE name=?", {name});
        std::cout << "Deleted template '" << name << "'\n";
        return 0;
    }

    // -----------------------------------------------------------------------
    // apply (parity-check OR scaffold-generate)
    // -----------------------------------------------------------------------
    int doApply(core::Db& db, const std::vector<std::string>& args) {
        std::string name;
        for (auto& a : args) { if (a.empty() || a[0] == '-') continue; name = a; break; }
        if (name.empty()) { std::cerr << "icmg template apply: <name> required\n"; return 1; }
        std::string to     = fv(args, "--to");
        bool check_only    = fl(args, "--check");
        bool json_out      = fl(args, "--json");
        if (to.empty()) { std::cerr << "icmg template apply: --to <new-file> required\n"; return 1; }

        // Load manifest.
        std::string mj, source_path, stored_hash;
        db.query("SELECT manifest_json, source_path, body_hash FROM templates WHERE name=?",
                 {name},
                 [&](const core::Row& r) {
                     if (r.size() < 3) return;
                     mj = r[0]; source_path = r[1]; stored_hash = r[2];
                 });
        if (mj.empty()) {
            std::cerr << "icmg template apply: not found: " << name << "\n";
            return 1;
        }
        json m;
        try { m = json::parse(mj); } catch (...) {
            std::cerr << "icmg template apply: corrupt manifest\n"; return 1;
        }

        // Staleness check (warn, do not block).
        if (!stored_hash.empty() && fs::exists(source_path)) {
            std::string current = hashFile(source_path);
            if (current != stored_hash) {
                std::cerr << "Warning: source " << source_path
                          << " changed since template was saved.\n"
                          << "         Run `icmg template extract " << source_path
                          << " --save-as " << name << "` to refresh.\n";
            }
        }

        // Mode: scaffold (target absent) vs check (target exists).
        if (!fs::exists(to)) {
            if (check_only) {
                std::cerr << "icmg template apply --check: file not found: " << to << "\n";
                return 1;
            }
            return scaffold(m, to);
        }

        // Parity check via graph (target must be scanned).
        graph::GraphStore store(db);
        auto target_node = store.getNode(to);
        if (!target_node) {
            std::cerr << "icmg template apply: target not in graph: " << to
                      << " (run `icmg graph scan` first)\n";
            return 2;
        }
        auto kids = store.childrenOf(target_node->id);
        std::set<std::string> have;
        for (auto& s : kids) have.insert(s.symbol_name);

        std::vector<std::pair<std::string, std::string>> missing;
        for (auto& rs : m["required_symbols"]) {
            std::string n = rs.value("name", "");
            std::string k = rs.value("kind", "");
            if (n.empty()) continue;
            if (!have.count(n)) missing.push_back({n, k});
        }

        if (json_out) {
            json out;
            out["template"] = name;
            out["target"]   = to;
            out["missing"]  = json::array();
            for (auto& [n, k] : missing)
                out["missing"].push_back({{"name", n}, {"kind", k}});
            std::cout << out.dump() << "\n";
        } else {
            std::cout << "Template apply check: " << name << " -> " << to << "\n";
            std::cout << "  MISSING (" << missing.size() << "):";
            for (auto& [n, k] : missing) std::cout << " " << n << "(" << k << ")";
            std::cout << "\n  Exit: " << missing.size() << "\n";
        }
        return (int)missing.size();
    }

    // -----------------------------------------------------------------------
    // helpers
    // -----------------------------------------------------------------------
    static std::string hashFile(const std::string& path) {
        if (!fs::exists(path)) return "";
        std::ifstream f(path, std::ios::binary);
        if (!f) return "";
        std::ostringstream s; s << f.rdbuf();
        return embed::fnv1a64(s.str());
    }

    static std::vector<std::string> readStructuralMarkers(const std::string& path) {
        std::vector<std::string> out;
        std::ifstream f(path);
        if (!f) return out;
        std::string line;
        std::regex re_using(R"(^\s*(using|import|#include|require|from)\s+[\w\.\:\<\>\s]+)",
                            std::regex::ECMAScript);
        std::regex re_class(R"(^\s*(public|internal|export\s+default|export|abstract\s+class|class|namespace)\s+\w+)",
                            std::regex::ECMAScript);
        int n = 0;
        while (std::getline(f, line) && n < 80) {
            if (line.size() > 200) line = line.substr(0, 200);
            if (std::regex_search(line, re_using) || std::regex_search(line, re_class)) {
                std::string t = line;
                while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(t.begin());
                if (!t.empty()) out.push_back(t);
            }
            ++n;
        }
        return out;
    }

    int scaffold(const json& m, const std::string& to) {
        std::ofstream f(to);
        if (!f) { std::cerr << "icmg template apply: cannot create " << to << "\n"; return 1; }
        std::string lang = guessLang(to);

        // Header: structural markers verbatim
        if (m.contains("structural_markers")) {
            for (auto& s : m["structural_markers"]) f << s.get<std::string>() << "\n";
            f << "\n";
        }
        std::string class_name = fs::path(to).stem().string();
        if (lang == "csharp") {
            f << "public class " << class_name << "\n{\n";
            for (auto& rs : m["required_symbols"]) {
                std::string n = rs.value("name", "");
                std::string k = rs.value("kind", "method");
                std::string sig = rs.value("signature", "");
                if (k == "method")        f << "    // TODO: implement\n    public void " << n << "() { }\n\n";
                else if (k == "property") f << "    public object " << n << " { get; set; }\n";
                else if (k == "field")    f << "    private object " << n << ";\n";
                (void)sig;
            }
            f << "}\n";
        } else if (lang == "typescript" || lang == "javascript") {
            f << "export class " << class_name << " {\n";
            for (auto& rs : m["required_symbols"]) {
                std::string n = rs.value("name", "");
                std::string k = rs.value("kind", "method");
                if (k == "method")   f << "  // TODO: implement\n  " << n << "() { }\n\n";
                else                 f << "  " << n << ": any;\n";
            }
            f << "}\n";
        } else {
            // Generic checklist comment.
            f << "// Scaffold from template '" << m.value("name", "") << "'\n";
            f << "// Required symbols (implement each):\n";
            for (auto& rs : m["required_symbols"]) {
                f << "//   - " << rs.value("name", "") << " (" << rs.value("kind", "") << ")\n";
            }
        }
        std::cerr << "Scaffold written to " << to << "\n"
                  << "  required symbols: " << m["required_symbols"].size() << "\n"
                  << "  language: " << lang << "\n";
        return 0;
    }

    static std::string guessLang(const std::string& path) {
        std::string ext = fs::path(path).extension().string();
        if (ext == ".cs")  return "csharp";
        if (ext == ".ts" || ext == ".tsx") return "typescript";
        if (ext == ".js" || ext == ".jsx") return "javascript";
        if (ext == ".py")  return "python";
        if (ext == ".sql") return "sql";
        return "unknown";
    }
};

ICMG_REGISTER_COMMAND("template", TemplateCommand);

} // namespace icmg::cli
