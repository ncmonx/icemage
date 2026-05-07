// `icmg memoir` — long-form narrative memory (story / essay / case-study).
// Built atop existing memory_nodes with topic prefix "memoir:<title>".
// Differences from short memory:
//   - Never deduplicated (always force=true)
//   - Default importance = 2 (high decay resistance)
//   - Content not truncated in listing — separate `show` command for full
//   - Optional `--link <id>` cross-references prior memoir via keywords
//
// Use case: capture context that's too rich for short memory_nodes —
// post-mortems, architecture rationales, customer interview synthesis.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../imem/memory_store.hpp"
#include "../../imem/memory_node.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace icmg::cli {

class MemoirCommand : public BaseCommand {
public:
    std::string name()        const override { return "memoir"; }
    std::string description() const override { return "Long-form narrative memory (essays, post-mortems)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg memoir <action> [args]\n\n"
            "Actions:\n"
            "  add --title T [--content-file F | --content TEXT] [--keywords K] [--link ID]\n"
            "  list [--limit N] [--zone Z]\n"
            "  show <id>\n"
            "  search <query> [--limit N]\n"
            "  link <id> --to <other-id>     Cross-reference\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        std::string action = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore store(db);

        if (action == "add")    return add(store, rest);
        if (action == "list")   return list(db, rest);
        if (action == "show")   return show(store, rest);
        if (action == "search") return search(store, rest);
        if (action == "link")   return linkCmd(db, rest);

        std::cerr << "icmg memoir: unknown action: " << action << "\n";
        usage();
        return 1;
    }

private:
    int add(imem::MemoryStore& store, const std::vector<std::string>& args) {
        std::string title    = flagValue(args, "--title");
        std::string content  = flagValue(args, "--content");
        std::string content_file = flagValue(args, "--content-file");
        std::string keywords = flagValue(args, "--keywords");
        std::string zone     = flagValue(args, "--zone", "default");
        std::string link     = flagValue(args, "--link");

        if (title.empty()) { std::cerr << "icmg memoir add: --title required\n"; return 1; }
        if (!content_file.empty()) {
            std::ifstream f(content_file);
            if (!f) { std::cerr << "icmg memoir add: cannot read " << content_file << "\n"; return 1; }
            std::ostringstream s; s << f.rdbuf(); content = s.str();
        }
        if (content.empty()) {
            // Read from stdin as fallback for piping.
            std::ostringstream s; s << std::cin.rdbuf(); content = s.str();
        }
        if (content.empty()) { std::cerr << "icmg memoir add: --content or --content-file required\n"; return 1; }

        imem::MemoryNode n;
        n.topic   = "memoir:" + title;
        n.content = content;
        n.keywords = keywords.empty() ? "memoir" : "memoir," + keywords;
        if (!link.empty()) n.keywords += ",linked:" + link;
        n.importance = 2;     // high decay resistance
        n.zone = zone;
        int64_t id = store.store(n, /*force=*/true);
        std::cout << "Memoir #" << id << " stored: " << title << " (" << content.size() << " bytes)\n";
        return 0;
    }

    int list(core::Db& db, const std::vector<std::string>& args) {
        int limit = 20;
        try { limit = std::stoi(flagValue(args, "--limit", "20")); } catch (...) {}
        std::string zone = flagValue(args, "--zone");
        std::string sql = "SELECT id, topic, importance, frequency, last_used, "
                          "       LENGTH(content) FROM memory_nodes "
                          "WHERE topic LIKE 'memoir:%' AND deleted_at IS NULL";
        std::vector<std::string> params;
        if (!zone.empty()) { sql += " AND zone=?"; params.push_back(zone); }
        sql += " ORDER BY last_used DESC LIMIT ?";
        params.push_back(std::to_string(limit));

        std::cout << "ID  Importance  Used  Bytes  Title\n";
        std::cout << "--  ----------  ----  -----  -----\n";
        db.query(sql, params, [](const core::Row& r) {
            if (r.size() < 6) return;
            std::cout << std::left << std::setw(4) << r[0]
                      << std::setw(12) << r[2]
                      << std::setw(6) << r[3]
                      << std::setw(7) << r[5]
                      << r[1].substr(7) << "\n";   // strip "memoir:" prefix
        });
        return 0;
    }

    int show(imem::MemoryStore& store, const std::vector<std::string>& args) {
        if (args.empty()) { std::cerr << "icmg memoir show: requires <id>\n"; return 1; }
        int64_t id; try { id = std::stoll(args[0]); } catch (...) { return 1; }
        auto n = store.get(id);
        if (n.id == 0) { std::cerr << "Not found.\n"; return 1; }
        if (n.topic.find("memoir:") != 0) {
            std::cerr << "Node #" << id << " is not a memoir.\n";
            return 1;
        }
        std::cout << "# " << n.topic.substr(7) << "\n\n";
        std::cout << "Importance: " << n.importance
                  << "  Frequency: " << n.frequency
                  << "  Zone: " << n.zone << "\n";
        if (!n.keywords.empty()) std::cout << "Keywords: " << n.keywords << "\n";
        std::cout << "\n" << n.content << "\n";
        return 0;
    }

    int search(imem::MemoryStore& store, const std::vector<std::string>& args) {
        if (args.empty()) { std::cerr << "icmg memoir search: <query> required\n"; return 1; }
        int limit = 5;
        try { limit = std::stoi(flagValue(args, "--limit", "5")); } catch (...) {}
        std::string query;
        for (auto& a : args) { if (a.empty() || a[0] == '-') continue; query = a; break; }
        if (query.empty()) return 1;
        // BM25 recall over memoir-prefixed nodes
        auto results = store.recallByTopic("memoir:", 100);
        // Filter by query substring (basic)
        std::vector<imem::MemoryNode> hits;
        std::string ql = query;
        for (auto& c : ql) c = (char)::tolower((unsigned char)c);
        for (auto& n : results) {
            std::string c = n.topic + " " + n.content;
            for (auto& ch : c) ch = (char)::tolower((unsigned char)ch);
            if (c.find(ql) != std::string::npos) {
                hits.push_back(std::move(n));
                if ((int)hits.size() >= limit) break;
            }
        }
        if (hits.empty()) { std::cout << "No matches.\n"; return 0; }
        for (auto& n : hits) {
            std::cout << "[" << n.id << "] " << n.topic.substr(7) << "\n";
            std::cout << "    " << n.content.substr(0, 120);
            if (n.content.size() > 120) std::cout << "...";
            std::cout << "\n\n";
        }
        return 0;
    }

    int linkCmd(core::Db& db, const std::vector<std::string>& args) {
        if (args.empty()) { std::cerr << "icmg memoir link: <id> --to <other-id>\n"; return 1; }
        int64_t a, b;
        try { a = std::stoll(args[0]); b = std::stoll(flagValue(args, "--to")); } catch (...) {
            std::cerr << "Invalid IDs\n"; return 1;
        }
        // Append cross-ref keyword to both.
        for (auto pair : std::vector<std::pair<int64_t, int64_t>>{{a, b}, {b, a}}) {
            db.run("UPDATE memory_nodes SET keywords = COALESCE(keywords, '') || ',linked:' || ? "
                   "WHERE id = ?",
                   {std::to_string(pair.second), std::to_string(pair.first)});
        }
        std::cout << "Linked memoir #" << a << " <-> #" << b << "\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("memoir", MemoirCommand);

} // namespace icmg::cli
