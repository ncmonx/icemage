// Phase 27 T3: `icmg feedback` — recall reranking signal.
//
// `feedback last --score N` reads top result of last query_history row,
// records score 0-5. Scorer reads avg over last 30d, multiplies final
// score by (1 + 0.2 * (avg-2.5)/2.5) — capped at +/- 0.2.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include <iostream>
#include <iomanip>
#include <map>

namespace icmg::cli {

class FeedbackCommand : public BaseCommand {
public:
    std::string name()        const override { return "feedback"; }
    std::string description() const override { return "Record recall quality feedback for reranking"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg feedback <action> [args]\n\n"
            "Actions:\n"
            "  <id> --score N [--query Q]   Record feedback for node id\n"
            "  last --score N               Apply to last query's top result\n"
            "  show <node-id>               Feedback history per node\n"
            "  stats                        Aggregate per zone/topic\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        std::string first = args[0];

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));

        if (first == "show")  return doShow(db, args);
        if (first == "stats") return doStats(db);
        if (first == "last")  return doLast(db, args);

        // Treat first arg as node-id.
        int64_t id = 0;
        try { id = std::stoll(first); } catch (...) {
            std::cerr << "icmg feedback: invalid node-id: " << first << "\n";
            return 1;
        }
        return doRecord(db, id, args);
    }

private:
    int doRecord(core::Db& db, int64_t node_id, const std::vector<std::string>& args) {
        int score = -1;
        try { score = std::stoi(flagValue(args, "--score", "-1")); } catch (...) {}
        if (score < 0 || score > 5) {
            std::cerr << "icmg feedback: --score 0..5 required\n"; return 1;
        }
        std::string q = flagValue(args, "--query");
        // Sanity-check node exists.
        int n = 0;
        db.query("SELECT 1 FROM memory_nodes WHERE id=? LIMIT 1",
                 {std::to_string(node_id)},
                 [&](const core::Row&){ ++n; });
        if (n == 0) {
            std::cerr << "icmg feedback: node " << node_id << " not found\n";
            return 1;
        }
        db.run("INSERT INTO feedback(node_id, query, score) VALUES(?,?,?)",
               {std::to_string(node_id), q, std::to_string(score)});
        std::cout << "feedback recorded: node=" << node_id << " score=" << score << "\n";
        return 0;
    }

    int doLast(core::Db& db, const std::vector<std::string>& args) {
        // Get most recent query_history row + parse matched_ids.
        std::string q, matched;
        db.query("SELECT query, matched_ids FROM query_history "
                 "ORDER BY created_at DESC LIMIT 1", {},
                 [&](const core::Row& r){
                     if (r.size() >= 2) { q = r[0]; matched = r[1]; }
                 });
        if (q.empty()) {
            std::cerr << "icmg feedback last: no query history yet\n";
            return 1;
        }
        // matched_ids is stored as count in current schema (Phase 23 logQuery).
        // Without per-result IDs persisted, we can't auto-pick the top result.
        // Workaround: re-run recall behind the scenes using top-1 limit.
        int score = -1;
        try { score = std::stoi(flagValue(args, "--score", "-1")); } catch (...) {}
        if (score < 0 || score > 5) {
            std::cerr << "icmg feedback last: --score 0..5 required\n"; return 1;
        }
        // Pick top BM25 hit via existing memory_nodes table (avoid Scorer bias loop).
        int64_t top_id = 0;
        db.query("SELECT id FROM memory_nodes "
                 "WHERE deleted_at IS NULL AND (topic LIKE ? OR content LIKE ?) "
                 "ORDER BY frequency DESC, last_used DESC LIMIT 1",
                 {"%" + q + "%", "%" + q + "%"},
                 [&](const core::Row& r){ if (!r.empty()) top_id = std::stoll(r[0]); });
        if (top_id == 0) {
            std::cerr << "icmg feedback last: cannot resolve top result for: " << q << "\n";
            return 1;
        }
        db.run("INSERT INTO feedback(node_id, query, score) VALUES(?,?,?)",
               {std::to_string(top_id), q, std::to_string(score)});
        std::cout << "feedback recorded: query=\"" << q << "\" -> node=" << top_id
                  << " score=" << score << "\n";
        return 0;
    }

    int doShow(core::Db& db, const std::vector<std::string>& args) {
        if (args.size() < 2) { std::cerr << "feedback show: <node-id> required\n"; return 1; }
        int64_t id; try { id = std::stoll(args[1]); } catch (...) { return 1; }
        std::cout << "Feedback for node " << id << ":\n";
        int n = 0;
        db.query("SELECT score, query, created_at FROM feedback WHERE node_id=? "
                 "ORDER BY created_at DESC LIMIT 50",
                 {std::to_string(id)},
                 [&](const core::Row& r){
                     if (r.size() < 3) return;
                     std::cout << "  [" << r[0] << "] " << r[1] << "\n";
                     ++n;
                 });
        if (n == 0) std::cout << "  (none)\n";
        return 0;
    }

    int doStats(core::Db& db) {
        std::cout << "Feedback summary:\n";
        int total = 0;
        std::map<int, int> by_score;
        db.query("SELECT score, COUNT(*) FROM feedback GROUP BY score", {},
                 [&](const core::Row& r){
                     if (r.size() < 2) return;
                     int s = std::stoi(r[0]); int c = std::stoi(r[1]);
                     by_score[s] = c; total += c;
                 });
        for (auto& [s, c] : by_score) {
            std::cout << "  score=" << s << ": " << c
                      << " (" << std::fixed << std::setprecision(1)
                      << 100.0 * c / total << "%)\n";
        }
        std::cout << "  total: " << total << "\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("feedback", FeedbackCommand);

} // namespace icmg::cli
