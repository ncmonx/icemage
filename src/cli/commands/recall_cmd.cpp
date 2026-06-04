#include "../base_command.hpp"
#include "../recall_json.hpp"   // v1.70.0 #176
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/global_db.hpp"
#include "../../imem/memory_store.hpp"
#include "../../imem/atom_store.hpp"
#include "../../imem/scorer.hpp"
#include "../ref_registry.hpp"
#include <iostream>
#include <iomanip>
#include <string>
#include <chrono>
#include <algorithm>
#include <filesystem>

namespace icmg::cli {

static std::string timeAgo(int64_t epoch) {
    if (epoch <= 0) return "never";
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t diff = now - epoch;
    if (diff < 60)   return std::to_string(diff) + "s ago";
    if (diff < 3600) return std::to_string(diff/60) + "m ago";
    if (diff < 86400)return std::to_string(diff/3600) + "h ago";
    return std::to_string(diff/86400) + "d ago";
}

static void escapeJson(std::ostream& o, const std::string& s) {
    for (char c : s) {
        if      (c == '"')  o << "\\\"";
        else if (c == '\\') o << "\\\\";
        else if (c == '\n') o << "\\n";
        else if (c == '\t') o << "\\t";
        else                o << c;
    }
}

class RecallCommand : public BaseCommand {
public:
    std::string name()        const override { return "recall"; }
    std::string description() const override { return "Recall memory nodes by query"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg recall <query> [options]\n\n"
            "Options:\n"
            "  --limit N       Max results (default: 10)\n"
            "  --topic X       Filter by topic prefix\n"
            "  --zone Z        Restrict corpus to zone (sharper IDF, faster)\n"
            "  --semantic      Hybrid BM25+vec recall (Phase 23). Falls back to BM25 if no embedder.\n"
            "  --alpha N       Blend weight 0..1 (1=BM25 only, 0=vec only, default 0.5)\n"
            "  --pure          Equivalent to --semantic --alpha 0\n"
            "  --all-projects  Cross-project recall (aggregates from registered projects)\n"
            "  --fuzzy         Fuzzy search fallback\n"
            "  --at-commit SHA Filter to memories stored at a specific git commit (prefix ok)\n"
            "  --no-dedup      Show nodes already returned this session (default: suppress)\n"
            "  --explain       Show score breakdown\n"
            "  --history       Show recent queries\n"
            "  --json          JSON output\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            usage(); return 0;
        }

        bool history  = hasFlag(args, "--history");
        bool json     = hasFlag(args, "--json");
        bool explain  = hasFlag(args, "--explain");
        bool fuzzy    = hasFlag(args, "--fuzzy");
        bool no_dedup = hasFlag(args, "--no-dedup");
        bool semantic = hasFlag(args, "--semantic") || hasFlag(args, "--pure");
        bool pure_vec = hasFlag(args, "--pure");
        double alpha = 0.5;
        try { alpha = std::stod(flagValue(args, "--alpha", "0.5")); } catch (...) {}
        if (pure_vec) alpha = 0.0;
        bool all_projects = hasFlag(args, "--all-projects");
        // v1.1.0 Task 4: --unseen returns only entries not yet served in this
        // session. Session id from ICMG_SESSION_ID env (set by SessionStart hook)
        // or fallback to PID-based string.
        bool unseen = hasFlag(args, "--unseen");
        bool atoms    = hasFlag(args, "--atoms");  // v1.79 atom-FTS hybrid
        std::string session_id = flagValue(args, "--session", "");
        if (session_id.empty()) {
            const char* env_sid = std::getenv("ICMG_SESSION_ID");
            if (env_sid && *env_sid) session_id = env_sid;
        }
        std::string topic     = flagValue(args, "--topic");
        std::string zone      = flagValue(args, "--zone");
        std::string at_commit = flagValue(args, "--at-commit");
        int limit = 10;
        try { limit = std::stoi(flagValue(args, "--limit", "10")); } catch (...) {}

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore store(db);

        // History mode
        if (history) {
            auto qs = store.queryHistory(20);
            if (json) {
                std::cout << "[";
                for (size_t i = 0; i < qs.size(); ++i) {
                    if (i) std::cout << ",";
                    std::cout << "\""; escapeJson(std::cout, qs[i]); std::cout << "\"";
                }
                std::cout << "]\n";
            } else {
                std::cout << "Recent queries:\n";
                for (auto& q : qs) std::cout << "  " << q << "\n";
            }
            return 0;
        }

        // Need query
        std::string query;
        for (auto& a : args) {
            if (a.empty() || a[0] == '-') continue;
            query = a; break;
        }
        if (query.empty()) {
            std::cerr << "icmg recall: query required\n";
            return 1;
        }

        std::vector<imem::MemoryNode> results;
        if (atoms) {
            // v1.79 ICM dual-memory: match the semantic atom FTS, then return
            // the SOURCE memory nodes (clustered) so output shape is identical
            // to normal recall. Default recall path is untouched.
            imem::AtomStore as(db);
            for (int64_t sid : as.recallAtomSources(query, limit)) {
                imem::MemoryNode n = store.get(sid);
                if (n.id != 0) results.push_back(std::move(n));
            }
        } else if (all_projects) {
            // Phase 21 Task 5: aggregate top-K from each registered project DB.
            results = recallAllProjects(query, limit, fuzzy);
        } else if (!topic.empty()) {
            results = store.recallByTopic(topic, limit);
        } else if (!zone.empty()) {
            results = store.recallInZone(query, zone, limit, fuzzy);
        } else if (semantic) {
            results = store.recallSemantic(query, limit, alpha);
        } else if (unseen) {
            results = store.recallUnseen(query, session_id, limit, fuzzy);
        } else {
            results = store.recall(query, limit, fuzzy);
        }

        // Phase 82 T4: in-session recall dedup — suppress nodes already returned
        // this session. Prevents identical results flooding multi-turn context.
        // Bypass with --no-dedup.
        if (!no_dedup && !json) {
            try {
                RefRegistry refs(std::filesystem::current_path().string());
                std::vector<imem::MemoryNode> deduped;
                for (auto& n : results) {
                    std::string key = std::to_string(n.id);
                    if (!refs.seen("RECALL", key)) {
                        refs.getOrAssign("RECALL", key);
                        deduped.push_back(std::move(n));
                    }
                }
                if (deduped.size() < results.size()) {
                    size_t suppressed = results.size() - deduped.size();
                    std::cerr << "[icmg recall] " << suppressed
                              << " node(s) suppressed (seen this session; use --no-dedup to show)\n";
                }
                results = std::move(deduped);
            } catch (...) {}
        }

        // --at-commit: filter to memories whose git_sha starts with the given prefix.
        if (!at_commit.empty()) {
            results.erase(
                std::remove_if(results.begin(), results.end(),
                    [&](const imem::MemoryNode& n){
                        return n.git_sha.substr(0, at_commit.size()) != at_commit;
                    }),
                results.end());
        }

        if (json) {
            printJson(results);
        } else if (explain) {
            printExplain(query, results);
        } else {
            printDefault(results);
        }

        return 0;
    }

private:
    // Phase 21 Task 5: cross-project recall — iterate registered projects,
    // recall top-K from each, merge + re-sort by score, return top-K overall.
    // Each child Db opens read-only via the existing Config override path.
    std::vector<imem::MemoryNode>
    recallAllProjects(const std::string& query, int limit, bool fuzzy) {
        std::vector<imem::MemoryNode> all;
        try {
            auto& gdb = core::GlobalDb::instance();
            gdb.init();
            auto projects = gdb.listProjects();
            if (projects.empty()) {
                std::cerr << "icmg recall: no registered projects (use `icmg project register`)\n";
                return all;
            }
            for (auto& p : projects) {
                if (p.db_path.empty()) continue;
                try {
                    core::Db pdb(p.db_path);
                    imem::MemoryStore pstore(pdb);
                    auto sub = pstore.recall(query, limit, fuzzy);
                    // Tag results with project name in topic prefix for visibility
                    for (auto& n : sub) {
                        n.topic = "[" + p.name + "] " + n.topic;
                        all.push_back(std::move(n));
                    }
                } catch (...) { /* skip unreadable project DBs */ }
            }
        } catch (const std::exception& e) {
            std::cerr << "icmg recall --all-projects: " << e.what() << "\n";
            return all;
        }
        // Re-sort merged corpus by score desc; truncate to limit.
        std::sort(all.begin(), all.end(),
                  [](const imem::MemoryNode& a, const imem::MemoryNode& b){
                      return a.score > b.score;
                  });
        if ((int)all.size() > limit) all.resize(limit);
        return all;
    }

    void printDefault(const std::vector<imem::MemoryNode>& nodes) const {
        if (nodes.empty()) { std::cout << "No results.\n"; return; }
        for (auto& n : nodes) {
            std::cout << std::fixed << std::setprecision(1)
                      << "[" << n.score << "] " << n.topic << "\n";
            std::cout << "  \"";
            // Truncate long content
            if (n.content.size() > 120)
                std::cout << n.content.substr(0, 117) << "...";
            else
                std::cout << n.content;
            std::cout << "\"\n";
            if (!n.keywords.empty())
                std::cout << "  Keywords: " << n.keywords << "\n";
            std::cout << "  Used: " << n.frequency << "x"
                      << ", last: " << timeAgo(n.last_used);
            if (!n.git_sha.empty()) std::cout << "  @" << n.git_sha;
            if (n.source != "unknown" && !n.source.empty()) std::cout << "  [from: " << n.source << "]";
            std::cout << "\n\n";
        }
    }

    void printExplain(const std::string& query,
                      const std::vector<imem::MemoryNode>& nodes) const {
        if (nodes.empty()) { std::cout << "No results.\n"; return; }
        auto& scorer = imem::Scorer::instance();
        for (auto& n : nodes) {
            auto d = scorer.scoreDetailed(query, n);
            std::cout << std::fixed << std::setprecision(2)
                      << "[" << d.total << "] " << n.topic << " — \""
                      << n.content.substr(0, 60) << "\"\n";
            std::cout << "  BM25=" << d.bm25
                      << " × recency=" << d.recency
                      << " × freq="   << d.freq
                      << " × importance=" << d.importance_mult
                      << " = " << d.total << "\n";
            if (!d.matched_tokens.empty()) {
                std::cout << "  Matched: [";
                for (size_t i = 0; i < d.matched_tokens.size(); ++i) {
                    if (i) std::cout << ", ";
                    std::cout << d.matched_tokens[i];
                }
                std::cout << "]\n";
            }
            std::cout << "\n";
        }
    }

    void printJson(const std::vector<imem::MemoryNode>& nodes) const {
        // v1.70.0 #176: emit via nlohmann + safeDump so output is always
        // valid UTF-8 (memory content may hold raw non-UTF-8 bytes).
        std::cout << recallNodesToJson(nodes) << "\n";
    }
};

class ForgetCommand : public BaseCommand {
public:
    std::string name()        const override { return "forget"; }
    std::string description() const override { return "Soft-delete a memory node"; }

    int run(const std::vector<std::string>& args) override {
        // Phase 36 T3: --pattern bulk-delete by topic LIKE.
        std::string pattern = flagValue(args, "--pattern");
        if (!pattern.empty()) return forgetPattern(pattern, args);

        if (args.empty()) {
            std::cerr << "icmg forget: requires <id> OR --pattern <SQL-LIKE>\n"; return 1;
        }
        int64_t id;
        try { id = std::stoll(args[0]); } catch (...) {
            std::cerr << "icmg forget: invalid id\n"; return 1;
        }
        bool yes = hasFlag(args, "--yes");

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore store(db);

        if (!yes) {
            auto node = store.get(id);
            if (node.id == 0) {
                std::cerr << "icmg forget: node #" << id << " not found\n";
                return 1;
            }
            std::cerr << "Delete: [" << node.topic << "] \""
                      << node.content.substr(0, 80) << "\"?\n";
            std::cerr << "Add --yes to confirm.\n";
            return 1;
        }

        store.remove(id);
        std::cout << "Forgot node #" << id << "\n";
        return 0;
    }

private:
    int forgetPattern(const std::string& pattern, const std::vector<std::string>& args) {
        bool dry = hasFlag(args, "--dry-run");
        bool yes = hasFlag(args, "--yes");
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        int n = 0;
        db.query("SELECT COUNT(*) FROM memory_nodes WHERE topic LIKE ? AND deleted_at IS NULL",
                 {pattern},
                 [&](const core::Row& r){ if (!r.empty()) n = std::stoi(r[0]); });
        std::cout << "forget --pattern '" << pattern << "': " << n << " node(s) match\n";
        if (n == 0) return 0;
        if (dry || !yes) {
            if (dry) std::cout << "  [dry-run] no DB change\n";
            else     std::cout << "  Add --yes to confirm soft-delete.\n";
            return 0;
        }
        db.run("UPDATE memory_nodes SET deleted_at = strftime('%s','now') "
               "WHERE topic LIKE ? AND deleted_at IS NULL", {pattern});
        std::cout << "  soft-deleted " << n << " node(s).\n";
        return 0;
    }
};

class RestoreCommand : public BaseCommand {
public:
    std::string name()        const override { return "restore"; }
    std::string description() const override { return "Restore soft-deleted memory node"; }

    int run(const std::vector<std::string>& args) override {
        if (args.empty()) { std::cerr << "icmg restore: requires <id>\n"; return 1; }
        int64_t id;
        try { id = std::stoll(args[0]); } catch (...) {
            std::cerr << "icmg restore: invalid id\n"; return 1;
        }
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore store(db);
        store.restore(id);
        std::cout << "Restored node #" << id << "\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("recall",  RecallCommand);
ICMG_REGISTER_COMMAND("forget",  ForgetCommand);
ICMG_REGISTER_COMMAND("restore", RestoreCommand);

} // namespace icmg::cli
